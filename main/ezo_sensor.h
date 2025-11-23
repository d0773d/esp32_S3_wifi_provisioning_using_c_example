/**
 * @file ezo_sensor.h
 * @brief Atlas Scientific EZO sensor driver for ESP32
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// EZO sensor response codes
#define EZO_RESP_SUCCESS        0x01    // Command successful
#define EZO_RESP_SYNTAX_ERROR   0x02    // Syntax error
#define EZO_RESP_NOT_READY      0xFE    // Still processing, not ready
#define EZO_RESP_NO_DATA        0xFF    // No data to send

// EZO sensor types
#define EZO_TYPE_RTD            "RTD"   // Temperature
#define EZO_TYPE_PH             "pH"    // pH sensor
#define EZO_TYPE_EC             "EC"    // Electrical conductivity
#define EZO_TYPE_DO             "DO"    // Dissolved oxygen
#define EZO_TYPE_ORP            "ORP"   // Oxidation-reduction potential
#define EZO_TYPE_HUM            "HUM"   // Humidity

// Timing constants (in milliseconds)
#define EZO_SHORT_WAIT_MS       300     // Short delay for simple commands
#define EZO_LONG_WAIT_MS        5000    // Long delay for readings
#define EZO_RESPONSE_TIMEOUT_MS 1000    // Timeout for I2C operations

// Buffer sizes
#define EZO_LARGEST_STRING      24      // Maximum response size
#define EZO_SMALLEST_STRING     4       // Minimum response size
#define EZO_MAX_SENSOR_NAME     16      // Maximum sensor name length
#define EZO_MAX_SENSOR_TYPE     8       // Maximum sensor type length
#define EZO_MAX_FW_VERSION      16      // Maximum firmware version length

/**
 * @brief EZO sensor configuration structure
 */
typedef struct {
    uint8_t i2c_address;                        // I2C address of the sensor
    char name[EZO_MAX_SENSOR_NAME];             // Sensor name
    char type[EZO_MAX_SENSOR_TYPE];             // Sensor type (RTD, pH, EC, etc.)
    char firmware_version[EZO_MAX_FW_VERSION];  // Firmware version
    bool led_control;                           // LED on/off
    bool protocol_lock;                         // Protocol lock status
    
    // EC-specific parameters
    struct {
        float probe_type;                       // K value (probe type)
        float tds_conversion_factor;            // TDS conversion factor
        bool param_ec;                          // EC parameter enabled
        bool param_tds;                         // TDS parameter enabled
        bool param_s;                           // Salinity parameter enabled
        bool param_sg;                          // Specific gravity parameter enabled
    } ec;
    
    // RTD-specific parameters
    struct {
        char temperature_scale;                 // 'C', 'F', or 'K'
    } rtd;
    
    // pH-specific parameters
    struct {
        bool extended_scale;                    // Extended pH scale enabled
    } ph;
} ezo_sensor_config_t;

/**
 * @brief EZO sensor handle structure
 */
typedef struct {
    i2c_master_bus_handle_t bus_handle;         // I2C bus handle
    i2c_master_dev_handle_t dev_handle;         // I2C device handle
    ezo_sensor_config_t config;                 // Sensor configuration
} ezo_sensor_t;

/**
 * @brief Initialize an EZO sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param bus_handle I2C bus handle
 * @param i2c_address I2C address of the sensor
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_init(ezo_sensor_t *sensor, i2c_master_bus_handle_t bus_handle, uint8_t i2c_address);

/**
 * @brief Deinitialize an EZO sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_deinit(ezo_sensor_t *sensor);

/**
 * @brief Get device information (name, type, firmware version)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_device_info(ezo_sensor_t *sensor);

/**
 * @brief Send a command to the EZO sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param command Command string to send
 * @param response Buffer to store response (can be NULL)
 * @param response_size Size of response buffer
 * @param delay_ms Delay in milliseconds before reading response
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_send_command(ezo_sensor_t *sensor, const char *command, 
                                   char *response, size_t response_size, uint32_t delay_ms);

/**
 * @brief Read sensor value
 * 
 * @param sensor Pointer to sensor handle
 * @param value Pointer to store reading
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_read(ezo_sensor_t *sensor, float *value);

/**
 * @brief Read all sensor values (for multi-value sensors like HUM)
 * 
 * @param sensor Pointer to sensor handle
 * @param values Array to store readings (up to 4 values)
 * @param count Pointer to store number of values read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_read_all(ezo_sensor_t *sensor, float values[4], uint8_t *count);

/**
 * @brief Get sensor name
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param name Buffer to store name
 * @param name_size Size of name buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_name(ezo_sensor_t *sensor, char *name, size_t name_size);

/**
 * @brief Set sensor name
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param name New name for the sensor
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_set_name(ezo_sensor_t *sensor, const char *name);

/**
 * @brief Get LED control status
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param enabled Pointer to store LED status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_led(ezo_sensor_t *sensor, bool *enabled);

/**
 * @brief Set LED control
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param enabled true to enable LED, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_set_led(ezo_sensor_t *sensor, bool enabled);

/**
 * @brief Get protocol lock status
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param locked Pointer to store lock status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_plock(ezo_sensor_t *sensor, bool *locked);

/**
 * @brief Set protocol lock
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param locked true to lock protocol, false to unlock
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_set_plock(ezo_sensor_t *sensor, bool locked);

/**
 * @brief Factory reset the sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_factory_reset(ezo_sensor_t *sensor);

/**
 * @brief Change sensor I2C address
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param new_address New I2C address (device will reboot after change)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_change_i2c_address(ezo_sensor_t *sensor, uint8_t new_address);

// EC-specific functions
/**
 * @brief Get EC probe type (K value)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param probe_type Pointer to store probe type
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_get_probe_type(ezo_sensor_t *sensor, float *probe_type);

/**
 * @brief Set EC probe type (K value)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param probe_type Probe type value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_set_probe_type(ezo_sensor_t *sensor, float probe_type);

/**
 * @brief Get TDS conversion factor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param factor Pointer to store conversion factor
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_get_tds_factor(ezo_sensor_t *sensor, float *factor);

/**
 * @brief Set TDS conversion factor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param factor TDS conversion factor
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_set_tds_factor(ezo_sensor_t *sensor, float factor);

/**
 * @brief Enable/disable EC output parameters
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param param Parameter name ("EC", "TDS", "S", "SG")
 * @param enabled true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled);

// RTD-specific functions
/**
 * @brief Get RTD temperature scale
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param scale Pointer to store scale ('C', 'F', or 'K')
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_rtd_get_scale(ezo_sensor_t *sensor, char *scale);

/**
 * @brief Set RTD temperature scale
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param scale Temperature scale ('C', 'F', or 'K')
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_rtd_set_scale(ezo_sensor_t *sensor, char scale);

// pH-specific functions
/**
 * @brief Get pH extended scale status
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param enabled Pointer to store extended scale status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ph_get_extended_scale(ezo_sensor_t *sensor, bool *enabled);

/**
 * @brief Set pH extended scale
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param enabled true to enable extended scale, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ph_set_extended_scale(ezo_sensor_t *sensor, bool enabled);

// Calibration functions
/**
 * @brief Calibrate pH sensor (mid-point, low, high, or clear)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param point Calibration point: "mid", "low", "high", or "clear"
 * @param value Calibration value (e.g., 7.00 for mid, 4.00 for low, 10.00 for high). Ignored for "clear".
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ph_calibrate(ezo_sensor_t *sensor, const char *point, float value);

/**
 * @brief Calibrate RTD sensor (temperature calibration or clear)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param temperature Temperature value for calibration, or -1000.0 to clear calibration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_rtd_calibrate(ezo_sensor_t *sensor, float temperature);

/**
 * @brief Calibrate EC sensor (dry, single-point, low, high, or clear)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param point Calibration point: "dry", "low", "high", or "clear"
 * @param value Calibration value in µS (e.g., 12880 for low, 80000 for high). Use 0 for "dry" or "clear".
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ec_calibrate(ezo_sensor_t *sensor, const char *point, uint32_t value);

/**
 * @brief Calibrate DO sensor (atmospheric or zero point calibration, or clear)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param point Calibration point: "atm" (atmospheric), "0" (zero), or "clear"
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_do_calibrate(ezo_sensor_t *sensor, const char *point);

/**
 * @brief Calibrate ORP sensor (single point calibration or clear)
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param value ORP calibration value in mV, or -1000.0 to clear calibration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_orp_calibrate(ezo_sensor_t *sensor, float value);

/**
 * @brief Query calibration status
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param status Buffer to store calibration status string
 * @param status_size Size of status buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_calibration_status(ezo_sensor_t *sensor, char *status, size_t status_size);

// Output string control functions
/**
 * @brief Enable or disable specific output parameters for RTD sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param param Parameter name ("T" for temperature)
 * @param enabled true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_rtd_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled);

/**
 * @brief Enable or disable specific output parameters for humidity sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param param Parameter name ("HUM" for humidity, "T" for temperature, "Dew" for dew point)
 * @param enabled true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_hum_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled);

/**
 * @brief Enable or disable specific output parameters for pH sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param param Parameter name ("pH" for pH value)
 * @param enabled true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_ph_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled);

/**
 * @brief Enable or disable specific output parameters for DO sensor
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param param Parameter name ("DO" for dissolved oxygen, "%" for saturation)
 * @param enabled true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_do_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled);

/**
 * @brief Get output string configuration
 * 
 * @param sensor Pointer to EZO sensor handle
 * @param config Buffer to store output configuration string
 * @param config_size Size of config buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ezo_sensor_get_output_config(ezo_sensor_t *sensor, char *config, size_t config_size);

#ifdef __cplusplus
}
#endif
