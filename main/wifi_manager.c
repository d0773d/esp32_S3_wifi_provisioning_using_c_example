#include "wifi_manager.h"
#include "provisioning_state.h"
#include "ble_provisioning.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";

// NVS keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_PROVISIONED "provisioned"

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Maximum retry attempts
#define MAX_RETRY_ATTEMPTS 5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_connected = false;

// Store credentials temporarily before saving
static char pending_ssid[33] = {0};
static char pending_password[64] = {0};

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t save_credentials_to_nvs(const char* ssid, const char* password);
static void send_status_notification(provisioning_state_t state, provisioning_status_code_t status, const char* message);

// Send status as JSON notification via BLE
static void send_status_notification(provisioning_state_t state, provisioning_status_code_t status, const char* message)
{
    if (!ble_provisioning_is_connected()) {
        return;
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", provisioning_state_to_string(state));
    cJSON_AddStringToObject(root, "status", provisioning_status_to_string(status));
    cJSON_AddStringToObject(root, "message", message ? message : "");
    cJSON_AddNumberToObject(root, "timestamp", (double)esp_log_timestamp());
    
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        ble_provisioning_send_status(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, attempting to connect...");
        esp_wifi_connect();
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconn_event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "WiFi disconnected (reason: %d)", disconn_event->reason);
        
        s_is_connected = false;
        
        if (s_retry_num < MAX_RETRY_ATTEMPTS) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connection attempt %d/%d", s_retry_num, MAX_RETRY_ATTEMPTS);
            
            char msg[64];
            snprintf(msg, sizeof(msg), "Connecting... (attempt %d/%d)", s_retry_num, MAX_RETRY_ATTEMPTS);
            provisioning_state_set(PROV_STATE_WIFI_CONNECTING, STATUS_SUCCESS, msg);
            send_status_notification(PROV_STATE_WIFI_CONNECTING, STATUS_SUCCESS, msg);
            
        } else {
            ESP_LOGE(TAG, "Failed to connect after %d attempts", MAX_RETRY_ATTEMPTS);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            
            // Determine failure reason
            provisioning_status_code_t status_code = STATUS_ERROR_WIFI_TIMEOUT;
            const char* error_msg = "Connection timeout";
            
            switch (disconn_event->reason) {
                case WIFI_REASON_AUTH_FAIL:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    status_code = STATUS_ERROR_WIFI_AUTH_FAILED;
                    error_msg = "Authentication failed - check password";
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                case WIFI_REASON_BEACON_TIMEOUT:
                    status_code = STATUS_ERROR_WIFI_NO_AP_FOUND;
                    error_msg = "Access point not found - check SSID";
                    break;
                default:
                    break;
            }
            
            provisioning_state_set(PROV_STATE_WIFI_FAILED, status_code, error_msg);
            send_status_notification(PROV_STATE_WIFI_FAILED, status_code, error_msg);
        }
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Save credentials to NVS
        esp_err_t ret = save_credentials_to_nvs(pending_ssid, pending_password);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Credentials saved to NVS successfully");
            
            // Clear sensitive data from memory
            memset(pending_ssid, 0, sizeof(pending_ssid));
            memset(pending_password, 0, sizeof(pending_password));
            
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            
            provisioning_state_set(PROV_STATE_PROVISIONED, STATUS_SUCCESS, ip_str);
            send_status_notification(PROV_STATE_PROVISIONED, STATUS_SUCCESS, ip_str);
            
        } else {
            ESP_LOGE(TAG, "Failed to save credentials to NVS");
            provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_STORAGE_FAILED, "Failed to save credentials");
            send_status_notification(PROV_STATE_ERROR, STATUS_ERROR_STORAGE_FAILED, "Failed to save credentials");
        }
    }
}

// Save credentials to NVS
static esp_err_t save_credentials_to_nvs(const char* ssid, const char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Write SSID
    ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write SSID to NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Write password
    ret = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write password to NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Set provisioned flag
    ret = nvs_set_u8(nvs_handle, NVS_KEY_PROVISIONED, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write provisioned flag to NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Commit changes
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit to NVS: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager");
    
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi station
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password)
{
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "SSID or password is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    
    // Store credentials temporarily
    strncpy(pending_ssid, ssid, sizeof(pending_ssid) - 1);
    strncpy(pending_password, password, sizeof(pending_password) - 1);
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Stop WiFi if already running
    esp_wifi_stop();
    
    // Set configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Reset retry counter
    s_retry_num = 0;
    
    // Update state
    provisioning_state_set(PROV_STATE_WIFI_CONNECTING, STATUS_SUCCESS, "Initiating WiFi connection");
    send_status_notification(PROV_STATE_WIFI_CONNECTING, STATUS_SUCCESS, "Initiating WiFi connection");
    
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

esp_err_t wifi_manager_get_stored_credentials(char* ssid, char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Check if provisioned
    uint8_t provisioned = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_PROVISIONED, &provisioned);
    if (ret != ESP_OK || provisioned == 0) {
        ESP_LOGI(TAG, "No stored credentials found");
        nvs_close(nvs_handle);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    
    // Read SSID
    size_t ssid_len = 33;
    ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Read password
    size_t password_len = 64;
    ret = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &password_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read password from NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Retrieved stored credentials for SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from WiFi");
    s_is_connected = false;
    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Clearing stored credentials");
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Erase all keys in namespace
    ret = nvs_erase_all(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Credentials cleared successfully");
    return ret;
}
