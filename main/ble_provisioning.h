#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include "esp_err.h"
#include <stdbool.h>

// BLE device name
#define BLE_DEVICE_NAME "ESP32_WiFi_Prov"

// Service and Characteristic UUIDs (128-bit UUIDs matching Kotlin app)
// Service: 00467768-6228-2272-4663-277478268000
// State:   00467768-6228-2272-4663-277478268001
// Creds:   00467768-6228-2272-4663-277478268002
// Status:  00467768-6228-2272-4663-277478268003

/**
 * @brief Initialize BLE provisioning service with secure bonding
 * 
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_init(void);

/**
 * @brief Start BLE advertising
 * 
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_start_advertising(void);

/**
 * @brief Stop BLE advertising
 * 
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_stop_advertising(void);

/**
 * @brief Send provisioning state notification to connected device
 * 
 * @param state State value (0=AWAITING, 1=PROVISIONING, 2=SUCCESS, 3=FAILED)
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_send_state(uint8_t state);

/**
 * @brief Send provisioning status notification to connected device
 * 
 * @param status_json JSON string containing status information
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_send_status(const char* status_json);

/**
 * @brief Check if a BLE client is connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_provisioning_is_connected(void);

/**
 * @brief Deinitialize BLE provisioning (after successful provisioning)
 * 
 * @return ESP_OK on success
 */
esp_err_t ble_provisioning_deinit(void);

#endif // BLE_PROVISIONING_H
