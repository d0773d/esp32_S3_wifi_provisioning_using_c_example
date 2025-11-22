/**
 * @file sensor_manager.h
 * @brief Sensor manager for handling MAX17048 and EZO sensors
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all sensors
 * 
 * Scans I2C bus and initializes MAX17048 battery monitor and EZO water sensors
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_init(void);

/**
 * @brief Deinitialize all sensors
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_deinit(void);

/**
 * @brief Read battery voltage from MAX17048
 * 
 * @param voltage Pointer to store voltage value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_battery_voltage(float *voltage);

/**
 * @brief Read battery percentage from MAX17048
 * 
 * @param percentage Pointer to store battery percentage (0-100%)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_battery_percentage(float *percentage);

/**
 * @brief Read temperature from EZO-RTD sensor
 * 
 * @param temperature Pointer to store temperature value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_temperature(float *temperature);

/**
 * @brief Read pH from EZO-pH sensor
 * 
 * @param ph Pointer to store pH value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_ph(float *ph);

/**
 * @brief Read electrical conductivity from EZO-EC sensor
 * 
 * @param ec Pointer to store EC value (µS/cm)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_ec(float *ec);

/**
 * @brief Read dissolved oxygen from EZO-DO sensor
 * 
 * @param dox Pointer to store DO value (mg/L)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_do(float *dox);

/**
 * @brief Read ORP (oxidation-reduction potential) from EZO-ORP sensor
 * 
 * @param orp Pointer to store ORP value (mV)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_read_orp(float *orp);

/**
 * @brief Get number of detected EZO sensors
 * 
 * @return uint8_t Number of EZO sensors found
 */
uint8_t sensor_manager_get_ezo_count(void);

/**
 * @brief Check if battery monitor is available
 * 
 * @return true if MAX17048 is present
 */
bool sensor_manager_has_battery_monitor(void);

/**
 * @brief Get EZO sensor handle by index
 * 
 * @param index Sensor index (0 to sensor_manager_get_ezo_count()-1)
 * @return Pointer to EZO sensor handle, NULL if invalid index
 */
void* sensor_manager_get_ezo_sensor(uint8_t index);

/**
 * @brief Rescan I2C bus and reinitialize all sensors
 * 
 * Useful after hot-swapping sensors (with power cycle)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sensor_manager_rescan(void);

#ifdef __cplusplus
}
#endif
