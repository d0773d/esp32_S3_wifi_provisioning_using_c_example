#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ble_provisioning.h"
#include "wifi_manager.h"
#include "provisioning_state.h"
#include "security.h"
#include "reset_button.h"
#include "time_sync.h"
#include "cloud_provisioning.h"
#include "http_server.h"
#include "api_key_manager.h"
#include "mdns_service.h"
#include "mqtt_telemetry.h"
#include "i2c_scanner.h"
#include "sensor_manager.h"

static const char *TAG = "MAIN";

// Forward declarations
static void state_change_handler(provisioning_state_t state, provisioning_status_code_t status, const char* message);
static void reset_button_handler(reset_button_event_t event, uint32_t press_duration_ms);
static void time_sync_handler(bool synced, struct tm *current_time);
static void cloud_prov_handler(bool success, const char *message);

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "ESP32-S3 WiFi BLE Provisioning");
    ESP_LOGI(TAG, "=================================");
    
    // Initialize security features (NVS encryption with eFuse protection)
    esp_err_t ret = security_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Security initialization failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Device will continue but credentials may not be secure!");
    }
    
    // Initialize reset button (GPIO0 - BOOT button)
    ret = reset_button_init(RESET_BUTTON_GPIO, reset_button_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize reset button: %s", esp_err_to_name(ret));
    }
    
    // Initialize provisioning state machine
    provisioning_state_init();
    
    // Register state change callback
    provisioning_state_register_callback(state_change_handler);
    
    // Initialize WiFi manager
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager");
        return;
    }
    
    // Check if already provisioned
    char stored_ssid[33] = {0};
    char stored_password[64] = {0};
    
    if (wifi_manager_get_stored_credentials(stored_ssid, stored_password) == ESP_OK) {
        ESP_LOGI(TAG, "Found stored credentials, attempting to connect to: %s", stored_ssid);
        
        ret = wifi_manager_connect(stored_ssid, stored_password);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Connecting to stored WiFi network...");
            
            // Wait for connection (with timeout)
            int wait_time = 0;
            while (!wifi_manager_is_connected() && wait_time < 30) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                wait_time++;
            }
            
            if (wifi_manager_is_connected()) {
                ESP_LOGI(TAG, "Successfully connected to stored WiFi network");
                provisioning_state_set(PROV_STATE_PROVISIONED, STATUS_SUCCESS, "Connected using stored credentials");
                
                // Initialize NTP time synchronization
                ESP_LOGI(TAG, "Initializing NTP time synchronization...");
                ret = time_sync_init(NULL, time_sync_handler); // NULL = UTC timezone
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize time sync: %s", esp_err_to_name(ret));
                }
                
                // Wait for time sync (required for HTTPS certificate validation)
                ESP_LOGI(TAG, "Waiting for time synchronization...");
                int sync_wait = 0;
                while (!time_sync_is_synced() && sync_wait < 10) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    sync_wait++;
                }
                
                // Initialize API key manager
                ESP_LOGI(TAG, "Initializing API key manager...");
                api_key_manager_init();
                
                // Initialize cloud provisioning
                ESP_LOGI(TAG, "Initializing cloud provisioning...");
                cloud_prov_init(cloud_prov_handler);
                
                // Start automatic provisioning (get certificates)
                ESP_LOGI(TAG, "Starting cloud provisioning...");
                ret = cloud_prov_provision_device();
                if (ret == ESP_OK) {
                    // Download MQTT CA certificate for MQTTS
                    ESP_LOGI(TAG, "Downloading MQTT CA certificate...");
                    ret = cloud_prov_download_mqtt_ca_cert();
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to download MQTT CA certificate: %s", esp_err_to_name(ret));
                    }
                    
                    // Initialize mDNS for local network discovery
                    ESP_LOGI(TAG, "Initializing mDNS service...");
                    ret = mdns_service_init("kc", "KannaCloud Device");
                    if (ret == ESP_OK) {
                        mdns_service_add_https(443);
                    } else {
                        ESP_LOGW(TAG, "mDNS initialization failed, device accessible by IP only");
                    }
                    
                    // Start HTTPS server with downloaded certificates
                    ESP_LOGI(TAG, "Starting HTTPS dashboard server...");
                    ret = http_server_start();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✓ HTTPS dashboard is ready!");
                        ESP_LOGI(TAG, "✓ Access at: https://kc.local");
                    } else {
                        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
                    }
                
                // Initialize and scan I2C bus for sensors
                ESP_LOGI(TAG, "Initializing I2C scanner...");
                ret = i2c_scanner_init();
                if (ret == ESP_OK) {
                    i2c_scanner_scan();
                    
                    // Initialize sensor manager for real sensor data
                    ESP_LOGI(TAG, "Initializing sensor manager...");
                    ret = sensor_manager_init();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✓ Sensors initialized: Battery=%s, EZO sensors=%d",
                                 sensor_manager_has_battery_monitor() ? "YES" : "NO",
                                 sensor_manager_get_ezo_count());
                    } else {
                        ESP_LOGW(TAG, "Failed to initialize sensors: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
                }
                    
                // Start sensor reading task (10 second interval)
                ESP_LOGI(TAG, "Starting sensor reading task...");
                ret = sensor_manager_start_reading_task(10);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to start sensor reading task: %s", esp_err_to_name(ret));
                }
                
                // Initialize and start MQTT client for KannaCloud telemetry
                ESP_LOGI(TAG, "Initializing MQTT client...");
                const char *mqtt_broker = "mqtts://mqtt.kannacloud.com:8883";
                const char *mqtt_username = "sensor01";
                const char *mqtt_password = "xkKKYQWxiT83Ni3";
                ret = mqtt_client_init(mqtt_broker, mqtt_username, mqtt_password);
                    if (ret == ESP_OK) {
                        ret = mqtt_client_start();
                        if (ret == ESP_OK) {
                            ESP_LOGI(TAG, "✓ MQTT telemetry enabled");
                        } else {
                            ESP_LOGW(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
                        }
                    } else {
                        ESP_LOGW(TAG, "Failed to initialize MQTT client: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGW(TAG, "Cloud provisioning failed, dashboard not available");
                }
                
                // No need to start BLE provisioning
                ESP_LOGI(TAG, "Device is provisioned and connected. BLE provisioning not started.");
                return;
            } else {
                ESP_LOGW(TAG, "Failed to connect with stored credentials, starting BLE provisioning");
            }
        }
        
        // Clear sensitive data
        memset(stored_password, 0, sizeof(stored_password));
    } else {
        ESP_LOGI(TAG, "No stored credentials found");
    }
    
    // Initialize BLE provisioning
    ESP_LOGI(TAG, "Starting BLE provisioning...");
    ret = ble_provisioning_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE provisioning");
        return;
    }
    
    // Start BLE advertising
    ret = ble_provisioning_start_advertising();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE advertising");
        return;
    }
    
    ESP_LOGI(TAG, "BLE provisioning started successfully");
    ESP_LOGI(TAG, "Device name: %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "Waiting for provisioning app to connect...");
    
    // Main loop - monitor provisioning status
    while (1) {
        provisioning_state_t current_state = provisioning_state_get();
        
        // If provisioned successfully, we can optionally stop BLE
        if (current_state == PROV_STATE_PROVISIONED) {
            ESP_LOGI(TAG, "Provisioning completed successfully!");
            
            // Initialize NTP time synchronization
            ESP_LOGI(TAG, "Initializing NTP time synchronization...");
            ret = time_sync_init(NULL, time_sync_handler); // NULL = UTC timezone
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize time sync: %s", esp_err_to_name(ret));
            }
            
            // Wait for time sync (required for HTTPS certificate validation)
            ESP_LOGI(TAG, "Waiting for time synchronization...");
            int sync_wait = 0;
            while (!time_sync_is_synced() && sync_wait < 10) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                sync_wait++;
            }
            
            // Initialize API key manager
            ESP_LOGI(TAG, "Initializing API key manager...");
            api_key_manager_init();
            
            // Initialize cloud provisioning
            ESP_LOGI(TAG, "Initializing cloud provisioning...");
            cloud_prov_init(cloud_prov_handler);
            
            // Start automatic provisioning (get certificates)
            ESP_LOGI(TAG, "Starting cloud provisioning...");
            ret = cloud_prov_provision_device();
            if (ret == ESP_OK) {
                // Download MQTT CA certificate for MQTTS
                ESP_LOGI(TAG, "Downloading MQTT CA certificate...");
                ret = cloud_prov_download_mqtt_ca_cert();
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to download MQTT CA certificate: %s", esp_err_to_name(ret));
                }
                
                // Initialize mDNS for local network discovery
                ESP_LOGI(TAG, "Initializing mDNS service...");
                ret = mdns_service_init("kc", "KannaCloud Device");
                if (ret == ESP_OK) {
                    mdns_service_add_https(443);
                } else {
                    ESP_LOGW(TAG, "mDNS initialization failed, device accessible by IP only");
                }
                
                // Start HTTPS server with downloaded certificates
                ESP_LOGI(TAG, "Starting HTTPS dashboard server...");
                ret = http_server_start();
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "✓ HTTPS dashboard is ready!");
                    ESP_LOGI(TAG, "✓ Access at: https://kc.local");
                } else {
                    ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
                }
                
                // Initialize and start MQTT client for KannaCloud telemetry
                ESP_LOGI(TAG, "Initializing MQTT client...");
                const char *mqtt_broker = "mqtts://mqtt.kannacloud.com:8883";
                const char *mqtt_username = "sensor01";
                const char *mqtt_password = "xkKKYQWxiT83Ni3";
                ret = mqtt_client_init(mqtt_broker, mqtt_username, mqtt_password);
                if (ret == ESP_OK) {
                    ret = mqtt_client_start();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✓ MQTT telemetry enabled");
                        // Set telemetry interval to 10 seconds for dashboard testing
                        mqtt_set_telemetry_interval(10);
                    } else {
                        ESP_LOGW(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to initialize MQTT client: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "Cloud provisioning failed, dashboard not available");
            }
            
            // Wait a bit to ensure final notifications are sent
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // Stop BLE to save power
            ESP_LOGI(TAG, "Stopping BLE provisioning service...");
            ble_provisioning_deinit();
            
            ESP_LOGI(TAG, "Device is now fully provisioned and connected to WiFi");
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Device is now provisioned - continue with normal operation
    ESP_LOGI(TAG, "Entering normal operation mode");
    
    // Here you can add your main application logic
    while (1) {
        // Check WiFi connection status periodically
        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "WiFi connection lost, attempting to reconnect...");
            wifi_manager_connect(stored_ssid, stored_password);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}

/**
 * @brief Handle state changes and send BLE notifications
 */
static void state_change_handler(provisioning_state_t state, provisioning_status_code_t status, const char* message)
{
    ESP_LOGI(TAG, "State changed: %s | Status: %s | Message: %s",
             provisioning_state_to_string(state),
             provisioning_status_to_string(status),
             message ? message : "N/A");
    
    // Send state notification if BLE client is connected
    if (ble_provisioning_is_connected()) {
        // Map internal states to app states (0=AWAITING, 1=PROVISIONING, 2=SUCCESS, 3=FAILED)
        uint8_t app_state = 0; // AWAITING
        
        switch (state) {
            case PROV_STATE_IDLE:
            case PROV_STATE_BLE_CONNECTED:
            case PROV_STATE_CREDENTIALS_RECEIVED:
                app_state = 0; // AWAITING
                break;
                
            case PROV_STATE_WIFI_CONNECTING:
                app_state = 1; // PROVISIONING
                break;
                
            case PROV_STATE_WIFI_CONNECTED:
            case PROV_STATE_PROVISIONED:
                app_state = 2; // SUCCESS
                break;
                
            case PROV_STATE_WIFI_FAILED:
            case PROV_STATE_ERROR:
                app_state = 3; // FAILED
                break;
                
            default:
                app_state = 0;
                break;
        }
        
        ble_provisioning_send_state(app_state);
        
        // Send status message as JSON or plain text
        if (message) {
            ble_provisioning_send_status(message);
        }
    }
}

/**
 * @brief Handle reset button events
 */
static void reset_button_handler(reset_button_event_t event, uint32_t press_duration_ms)
{
    switch (event) {
        case RESET_BUTTON_EVENT_SHORT_PRESS:
            ESP_LOGW(TAG, "====================================");
            ESP_LOGW(TAG, "SHORT PRESS DETECTED (%lu ms)", (unsigned long)press_duration_ms);
            ESP_LOGW(TAG, "Clearing WiFi credentials...");
            ESP_LOGW(TAG, "====================================");
            
            // Clear WiFi credentials
            esp_err_t ret = wifi_manager_clear_credentials();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "WiFi credentials cleared successfully");
                ESP_LOGI(TAG, "Restarting device to begin reprovisioning...");
                
                // Disconnect WiFi first
                wifi_manager_disconnect();
                
                // Wait a moment then restart
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "Failed to clear credentials: %s", esp_err_to_name(ret));
            }
            break;
            
        case RESET_BUTTON_EVENT_LONG_PRESS:
            ESP_LOGW(TAG, "====================================");
            ESP_LOGW(TAG, "LONG PRESS DETECTED (%lu ms)", (unsigned long)press_duration_ms);
            ESP_LOGW(TAG, "Performing FACTORY RESET...");
            ESP_LOGW(TAG, "====================================");
            
            // Erase entire NVS partition (factory reset)
            ret = nvs_flash_erase();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "NVS erased successfully (factory reset)");
                ESP_LOGI(TAG, "Restarting device...");
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown button event: %d", event);
            break;
    }
}

/**
 * @brief Handle time synchronization events
 */
static void time_sync_handler(bool synced, struct tm *current_time)
{
    if (synced && current_time != NULL) {
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", current_time);
        ESP_LOGI(TAG, "====================================");
        ESP_LOGI(TAG, "Time Synchronized Successfully!");
        ESP_LOGI(TAG, "Current time: %s UTC", time_str);
        ESP_LOGI(TAG, "====================================");
    } else {
        ESP_LOGW(TAG, "Time synchronization failed");
    }
}

/**
 * @brief Handle cloud provisioning events
 */
static void cloud_prov_handler(bool success, const char *message)
{
    if (success) {
        ESP_LOGI(TAG, "====================================");
        ESP_LOGI(TAG, "Cloud Provisioning Successful!");
        ESP_LOGI(TAG, "Message: %s", message ? message : "N/A");
        ESP_LOGI(TAG, "====================================");
    } else {
        ESP_LOGW(TAG, "====================================");
        ESP_LOGW(TAG, "Cloud Provisioning Failed");
        ESP_LOGW(TAG, "Error: %s", message ? message : "Unknown");
        ESP_LOGW(TAG, "====================================");
    }
}
