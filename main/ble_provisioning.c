#include "ble_provisioning.h"
#include "wifi_manager.h"
#include "provisioning_state.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "BLE_PROV";

// GATT Interface and Connection IDs
static uint16_t gatts_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id_global = 0xFFFF;
static bool is_connected = false;

// Attribute handles
static uint16_t service_handle = 0;
static uint16_t state_char_handle = 0;
static uint16_t wifi_cred_char_handle = 0;
static uint16_t status_char_handle = 0;
static uint16_t status_descr_handle = 0;

// Bonding/pairing state
static bool is_bonded = false;

// GAP advertising parameters
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Service and Characteristic declarations
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

// Match Raspberry Pi: write + write-without-response for WiFi credentials
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// 128-bit UUIDs matching Kotlin app
// Base: 00467768-6228-2272-4663-277478268000
static const uint8_t wifi_service_uuid[16] = {
    0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t state_char_uuid[16] = {
    0x01, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t wifi_creds_char_uuid[16] = {
    0x02, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t status_char_uuid[16] = {
    0x03, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

// Attribute table
enum {
    IDX_SVC,
    
    IDX_STATE_CHAR,
    IDX_STATE_VAL,
    IDX_STATE_CFG,  // CCCD for State notifications
    
    IDX_WIFI_CRED_CHAR,
    IDX_WIFI_CRED_VAL,
    
    IDX_STATUS_CHAR,
    IDX_STATUS_VAL,
    IDX_STATUS_CFG,  // CCCD for Status notifications
    
    HRS_IDX_NB,
};

static uint16_t attr_handle_table[HRS_IDX_NB];

// Write buffer for fragmented credential writes
#define MAX_CRED_BUFFER_SIZE 512
#define CRED_WRITE_TIMEOUT_MS 2000  // 2 seconds timeout for fragmented writes
static uint8_t cred_write_buffer[MAX_CRED_BUFFER_SIZE];
static uint16_t cred_write_len = 0;
static esp_timer_handle_t cred_timeout_timer = NULL;

// Forward declarations
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void handle_wifi_credentials(const uint8_t* data, uint16_t len);
static void cred_timeout_callback(void* arg);

// Parse WiFi credentials from JSON
static void handle_wifi_credentials(const uint8_t* data, uint16_t len)
{
    ESP_LOGI(TAG, "Received WiFi credentials (length: %d)", len);
    
    // Null-terminate the string
    char* json_str = (char*)malloc(len + 1);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON string");
        provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_INVALID_JSON, "Memory allocation failed");
        return;
    }
    
    memcpy(json_str, data, len);
    json_str[len] = '\0';
    
    ESP_LOGI(TAG, "JSON received: %s", json_str);
    
    // Parse JSON
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_INVALID_JSON, "Invalid JSON format");
        return;
    }
    
    // Extract SSID
    cJSON* ssid_item = cJSON_GetObjectItem(root, "ssid");
    if (ssid_item == NULL || !cJSON_IsString(ssid_item)) {
        ESP_LOGE(TAG, "Missing or invalid 'ssid' field");
        cJSON_Delete(root);
        provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_MISSING_SSID, "SSID field missing or invalid");
        return;
    }
    
    // Extract password
    cJSON* password_item = cJSON_GetObjectItem(root, "password");
    if (password_item == NULL || !cJSON_IsString(password_item)) {
        ESP_LOGE(TAG, "Missing or invalid 'password' field");
        cJSON_Delete(root);
        provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_MISSING_PASSWORD, "Password field missing or invalid");
        return;
    }
    
    const char* ssid = ssid_item->valuestring;
    const char* password = password_item->valuestring;
    
    ESP_LOGI(TAG, "Parsed credentials - SSID: %s", ssid);
    
    // Update state
    provisioning_state_set(PROV_STATE_CREDENTIALS_RECEIVED, STATUS_SUCCESS, "Credentials received successfully");
    
    // Connect to WiFi
    esp_err_t ret = wifi_manager_connect(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection");
        provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_WIFI_TIMEOUT, "Failed to initiate WiFi connection");
    }
    
    cJSON_Delete(root);
}

// Timeout callback for fragmented writes
static void cred_timeout_callback(void* arg)
{
    ESP_LOGW(TAG, "Credential write timeout - processing buffered data (%d bytes)", cred_write_len);
    
    if (cred_write_len > 0) {
        // Process what we have, even if incomplete
        handle_wifi_credentials(cred_write_buffer, cred_write_len);
        cred_write_len = 0;
    }
}

// GAP event handler
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set complete");
            esp_ble_gap_start_advertising(&adv_params);
            break;
            
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising start failed");
            } else {
                ESP_LOGI(TAG, "Advertising started successfully");
            }
            break;
            
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped");
            break;
            
        case ESP_GAP_BLE_SEC_REQ_EVT:
            ESP_LOGI(TAG, "Security request received");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
            
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "Authentication complete - Bonding successful");
                is_bonded = true;
            } else {
                ESP_LOGE(TAG, "Authentication failed - reason: 0x%x", param->ble_security.auth_cmpl.fail_reason);
                is_bonded = false;
            }
            break;
            
        default:
            break;
    }
}

// GATTS event handler
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered, status: %d", param->reg.status);
            gatts_if_global = gatts_if;
            
            // Set device name
            esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
            
            // Configure advertising data
            esp_ble_adv_data_t adv_data = {
                .set_scan_rsp = false,
                .include_name = true,
                .include_txpower = true,
                .min_interval = 0x0006,
                .max_interval = 0x0010,
                .appearance = 0x00,
                .manufacturer_len = 0,
                .p_manufacturer_data = NULL,
                .service_data_len = 0,
                .p_service_data = NULL,
                .service_uuid_len = sizeof(wifi_service_uuid),
                .p_service_uuid = (uint8_t*)wifi_service_uuid,
                .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
            };
            esp_ble_gap_config_adv_data(&adv_data);
            
            // Create attribute table
            esp_ble_gatts_create_attr_tab((esp_gatts_attr_db_t[]){
                // Service Declaration
                [IDX_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&primary_service_uuid, 
                             ESP_GATT_PERM_READ, sizeof(wifi_service_uuid), sizeof(wifi_service_uuid), 
                             (uint8_t*)wifi_service_uuid}},
                
                // State Characteristic Declaration
                [IDX_STATE_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid, 
                                    ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), 
                                    (uint8_t*)&char_prop_read_notify}},
                
                // State Characteristic Value - ENCRYPTED READ REQUIRED (match Raspberry Pi)
                [IDX_STATE_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t*)state_char_uuid, 
                                   ESP_GATT_PERM_READ_ENCRYPTED, 128, 0, NULL}},
                
                // State CCCD for notifications - ENCRYPTED WRITE REQUIRED (match Raspberry Pi)
                [IDX_STATE_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_client_config_uuid, 
                                   ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, sizeof(uint16_t), 0, NULL}},
                
                // WiFi Credentials Characteristic Declaration
                [IDX_WIFI_CRED_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid, 
                                        ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t*)&char_prop_write}},
                
                // WiFi Credentials Characteristic Value - ENCRYPTED WRITE REQUIRED (match Raspberry Pi)
                [IDX_WIFI_CRED_VAL] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_128, (uint8_t*)wifi_creds_char_uuid, 
                                       ESP_GATT_PERM_WRITE_ENCRYPTED, 512, 0, NULL}},
                
                // Status Characteristic Declaration
                [IDX_STATUS_CHAR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid, 
                                     ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), 
                                     (uint8_t*)&char_prop_read_notify}},
                
                // Status Characteristic Value - ENCRYPTED READ REQUIRED (match Raspberry Pi)
                [IDX_STATUS_VAL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t*)status_char_uuid, 
                                    ESP_GATT_PERM_READ_ENCRYPTED, 512, 0, NULL}},
                
                // Status CCCD for notifications - ENCRYPTED WRITE REQUIRED (match Raspberry Pi)
                [IDX_STATUS_CFG] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_client_config_uuid, 
                                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, sizeof(uint16_t), 0, NULL}},
            }, gatts_if, HRS_IDX_NB, 0);
            break;
            
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Create attribute table failed, error code: 0x%x", param->add_attr_tab.status);
            } else {
                ESP_LOGI(TAG, "Attribute table created successfully");
                memcpy(attr_handle_table, param->add_attr_tab.handles, sizeof(attr_handle_table));
                service_handle = attr_handle_table[IDX_SVC];
                state_char_handle = attr_handle_table[IDX_STATE_VAL];
                wifi_cred_char_handle = attr_handle_table[IDX_WIFI_CRED_VAL];
                status_char_handle = attr_handle_table[IDX_STATUS_VAL];
                status_descr_handle = attr_handle_table[IDX_STATUS_CFG];
                
                ESP_LOGI(TAG, "Handle mapping: Service=%d, State=%d, State_CCCD=%d, WiFiCred=%d, Status=%d, Status_CCCD=%d",
                        attr_handle_table[IDX_SVC], attr_handle_table[IDX_STATE_VAL], 
                        attr_handle_table[IDX_STATE_CFG], attr_handle_table[IDX_WIFI_CRED_VAL],
                        attr_handle_table[IDX_STATUS_VAL], attr_handle_table[IDX_STATUS_CFG]);
                
                esp_ble_gatts_start_service(service_handle);
            }
            break;
            
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Client connected, conn_id: %d", param->connect.conn_id);
            conn_id_global = param->connect.conn_id;
            is_connected = true;
            gatts_if_global = gatts_if;
            
            // Update connection parameters
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.latency = 0;
            conn_params.max_int = 0x20;
            conn_params.min_int = 0x10;
            conn_params.timeout = 400;
            esp_ble_gap_update_conn_params(&conn_params);
            
            // Set bonding mode
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
            
            provisioning_state_set(PROV_STATE_BLE_CONNECTED, STATUS_SUCCESS, "BLE client connected");
            break;
            
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected");
            is_connected = false;
            is_bonded = false;
            conn_id_global = 0xFFFF;
            
            // Restart advertising if not provisioned
            if (provisioning_state_get() != PROV_STATE_PROVISIONED) {
                esp_ble_gap_start_advertising(&adv_params);
                provisioning_state_set(PROV_STATE_IDLE, STATUS_SUCCESS, "BLE disconnected, restarting advertising");
            }
            break;
            
        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep) {
                ESP_LOGI(TAG, "GATT write event, handle: %d, len: %d", param->write.handle, param->write.len);
                ESP_LOGI(TAG, "WiFi cred handle: %d, Status CCCD handle: %d, is_bonded: %d", 
                        attr_handle_table[IDX_WIFI_CRED_VAL], attr_handle_table[IDX_STATUS_CFG], is_bonded);
                
                // Log first few bytes of data for debugging
                if (param->write.len > 0) {
                    ESP_LOG_BUFFER_HEX(TAG, param->write.value, param->write.len < 20 ? param->write.len : 20);
                }
                
                // Check if this is the State CCCD
                if (param->write.handle == attr_handle_table[IDX_STATE_CFG]) {
                    uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                    if (descr_value == 0x0001) {
                        ESP_LOGI(TAG, "Notifications enabled for State characteristic");
                    } else if (descr_value == 0x0000) {
                        ESP_LOGI(TAG, "Notifications disabled for State characteristic");
                    }
                }
                // Check if this is the Status CCCD
                else if (param->write.handle == attr_handle_table[IDX_STATUS_CFG]) {
                    uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                    if (descr_value == 0x0001) {
                        ESP_LOGI(TAG, "Notifications enabled for Status characteristic");
                        // Send initial status notification - use IDLE state
                        provisioning_state_set(PROV_STATE_IDLE, STATUS_SUCCESS, 
                                             "Ready to receive WiFi credentials");
                    } else if (descr_value == 0x0000) {
                        ESP_LOGI(TAG, "Notifications disabled for Status characteristic");
                    }
                }
                // Check if this is the WiFi credentials characteristic
                else if (param->write.handle == attr_handle_table[IDX_WIFI_CRED_VAL]) {
                    
                    // Check if this is a prepare write (long write)
                    if (param->write.is_prep) {
                        ESP_LOGI(TAG, "PREP_WRITE: offset=%d, len=%d", param->write.offset, param->write.len);
                        
                        // Buffer this chunk at the specified offset
                        if (is_bonded && param->write.offset + param->write.len <= MAX_CRED_BUFFER_SIZE) {
                            memcpy(cred_write_buffer + param->write.offset, param->write.value, param->write.len);
                            if (param->write.offset + param->write.len > cred_write_len) {
                                cred_write_len = param->write.offset + param->write.len;
                            }
                            ESP_LOGI(TAG, "Buffered %d bytes at offset %d, total: %d", 
                                    param->write.len, param->write.offset, cred_write_len);
                        }
                        
                        // Send response for prepare write
                        if (param->write.need_rsp) {
                            esp_gatt_rsp_t rsp;
                            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
                            rsp.attr_value.handle = param->write.handle;
                            rsp.attr_value.offset = param->write.offset;
                            rsp.attr_value.len = param->write.len;
                            memcpy(rsp.attr_value.value, param->write.value, param->write.len);
                            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, 
                                                       ESP_GATT_OK, &rsp);
                        }
                    } else {
                        // Normal write (not prepare write)
                        ESP_LOGI(TAG, "Write to WiFi credentials characteristic (fragment %d bytes)", param->write.len);
                        
                        // Only send response if it's a write WITH response (not write-without-response)
                        if (param->write.need_rsp) {
                            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, 
                                                       ESP_GATT_OK, NULL);
                        }
                        
                        // Only accept credentials if bonded
                        if (is_bonded) {
                        // Buffer this write fragment
                        if (cred_write_len + param->write.len <= MAX_CRED_BUFFER_SIZE) {
                            memcpy(cred_write_buffer + cred_write_len, param->write.value, param->write.len);
                            cred_write_len += param->write.len;
                            
                            ESP_LOGI(TAG, "Buffered %d bytes, total: %d", param->write.len, cred_write_len);
                            
                            // Check if we have a complete JSON (ends with '}')
                            if (cred_write_len > 0 && cred_write_buffer[cred_write_len - 1] == '}') {
                                ESP_LOGI(TAG, "Complete JSON detected, processing...");
                                
                                // Stop timeout timer if running
                                if (cred_timeout_timer != NULL) {
                                    esp_timer_stop(cred_timeout_timer);
                                }
                                
                                handle_wifi_credentials(cred_write_buffer, cred_write_len);
                                cred_write_len = 0;  // Reset buffer for next write
                            } else {
                                ESP_LOGI(TAG, "Waiting for more fragments (last char: 0x%02x)", 
                                        cred_write_buffer[cred_write_len - 1]);
                                
                                // Start/restart timeout timer
                                if (cred_timeout_timer == NULL) {
                                    const esp_timer_create_args_t timer_args = {
                                        .callback = &cred_timeout_callback,
                                        .name = "cred_timeout"
                                    };
                                    esp_timer_create(&timer_args, &cred_timeout_timer);
                                }
                                esp_timer_stop(cred_timeout_timer);  // Stop if already running
                                esp_timer_start_once(cred_timeout_timer, CRED_WRITE_TIMEOUT_MS * 1000);  // microseconds
                            }
                        } else {
                            ESP_LOGE(TAG, "Credential buffer overflow!");
                            cred_write_len = 0;  // Reset
                            provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_INVALID_JSON, 
                                                 "Credentials too long");
                        }
                        } else {
                            ESP_LOGW(TAG, "Credentials received but device not bonded");
                            provisioning_state_set(PROV_STATE_ERROR, STATUS_ERROR_INVALID_JSON, 
                                                 "Device must be bonded before sending credentials");
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "Write to handle %d (not WiFi creds or CCCD)", param->write.handle);
                }
            }
            break;
            
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU exchange complete, MTU: %d", param->mtu.mtu);
            ESP_LOGI(TAG, "Client can now send up to %d bytes per write", param->mtu.mtu - 3);
            break;
            
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(TAG, "EXEC_WRITE: exec_write_flag=%d", param->exec_write.exec_write_flag);
            
            // Process the complete buffered write
            if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && cred_write_len > 0) {
                ESP_LOGI(TAG, "Executing long write, processing %d bytes", cred_write_len);
                handle_wifi_credentials(cred_write_buffer, cred_write_len);
                cred_write_len = 0;
            } else if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_CANCEL) {
                ESP_LOGW(TAG, "Long write cancelled, discarding buffer");
                cred_write_len = 0;
            }
            
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, 
                                       ESP_GATT_OK, NULL);
            break;
            
        default:
            break;
    }
}

esp_err_t ble_provisioning_init(void)
{
    esp_err_t ret;
    
    // Release BT Classic memory (we only need BLE)
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release BT Classic memory: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BT controller: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable BT controller: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register callbacks
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GATTS callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GAP callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register GATT application
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GATT app: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set MTU
    ret = esp_ble_gatt_set_local_mtu(517);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set local MTU: %s", esp_err_to_name(ret));
    }
    
    // Configure security
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND; // Bonding without MITM (prevents double Android pairing notification)
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE; // Just Works pairing
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t auth_option = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_DISABLE;
    
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &auth_option, sizeof(uint8_t));
    
    ESP_LOGI(TAG, "BLE provisioning initialized successfully");
    return ESP_OK;
}

esp_err_t ble_provisioning_start_advertising(void)
{
    return esp_ble_gap_start_advertising(&adv_params);
}

esp_err_t ble_provisioning_stop_advertising(void)
{
    return esp_ble_gap_stop_advertising();
}

esp_err_t ble_provisioning_send_state(uint8_t state)
{
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot send state - no client connected");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sending state notification: %d", state);
    
    return esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global, 
                                       state_char_handle, 1, 
                                       &state, false);
}

esp_err_t ble_provisioning_send_status(const char* status_json)
{
    if (!is_connected) {
        ESP_LOGW(TAG, "Cannot send status - no client connected");
        return ESP_FAIL;
    }
    
    uint16_t len = strlen(status_json);
    ESP_LOGI(TAG, "Sending status notification: %s", status_json);
    
    return esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global, 
                                       status_char_handle, len, 
                                       (uint8_t*)status_json, false);
}

bool ble_provisioning_is_connected(void)
{
    return is_connected;
}

esp_err_t ble_provisioning_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE provisioning");
    
    // Stop advertising
    esp_ble_gap_stop_advertising();
    ESP_LOGI(TAG, "Advertising stopped");
    
    // Give time for any pending operations to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Disable and deinitialize Bluedroid stack
    esp_err_t ret = esp_bluedroid_disable();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable Bluedroid: %s", esp_err_to_name(ret));
    }
    
    ret = esp_bluedroid_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinit Bluedroid: %s", esp_err_to_name(ret));
    }
    
    // Disable and deinitialize BT controller
    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable BT controller: %s", esp_err_to_name(ret));
    }
    
    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinit BT controller: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "BLE provisioning deinitialized successfully");
    
    return ESP_OK;
}
