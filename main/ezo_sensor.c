/**
 * @file ezo_sensor.c
 * @brief Atlas Scientific EZO sensor driver implementation
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ezo_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EZO_SENSOR";

/**
 * @brief Send command and read response from EZO sensor
 */
esp_err_t ezo_sensor_send_command(ezo_sensor_t *sensor, const char *command, 
                                   char *response, size_t response_size, uint32_t delay_ms) {
    if (sensor == NULL || command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending command to 0x%02X: %s", sensor->config.i2c_address, command);

    // Send command
    esp_err_t ret = i2c_master_transmit(sensor->dev_handle, (const uint8_t *)command, 
                                        strlen(command), EZO_RESPONSE_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send command: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if this is an I2C address change command (device will reboot)
    if (strncmp(command, "I2C,", 4) == 0) {
        ESP_LOGW(TAG, "I2C address change command sent - device will reboot");
        return ESP_OK;
    }

    // Wait for sensor to process command
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Read response if buffer provided
    if (response != NULL && response_size > 0) {
        uint8_t buffer[EZO_LARGEST_STRING] = {0};
        
        ret = i2c_master_receive(sensor->dev_handle, buffer, EZO_LARGEST_STRING, 
                                EZO_RESPONSE_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read response: %s", esp_err_to_name(ret));
            return ret;
        }

        // Check response code
        uint8_t status = buffer[0];
        
        if (status == EZO_RESP_SUCCESS) {
            // Copy response, skipping the status byte
            size_t copy_len = (response_size - 1 < EZO_LARGEST_STRING - 1) ? 
                             response_size - 1 : EZO_LARGEST_STRING - 1;
            
            // Copy and remove null terminators
            size_t j = 0;
            for (size_t i = 1; i < EZO_LARGEST_STRING && j < copy_len && buffer[i] != 0; i++) {
                response[j++] = (char)buffer[i];
            }
            response[j] = '\0';
            
            ESP_LOGI(TAG, "Response: %s", response);
            return ESP_OK;
            
        } else if (status == EZO_RESP_SYNTAX_ERROR) {
            ESP_LOGE(TAG, "Syntax error in command");
            return ESP_ERR_INVALID_ARG;
            
        } else if (status == EZO_RESP_NOT_READY) {
            ESP_LOGW(TAG, "Sensor not ready, still processing");
            return ESP_ERR_NOT_FINISHED;
            
        } else if (status == EZO_RESP_NO_DATA) {
            ESP_LOGW(TAG, "No data available");
            return ESP_ERR_NOT_FOUND;
            
        } else {
            ESP_LOGE(TAG, "Unknown response code: 0x%02X", status);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

/**
 * @brief Initialize EZO sensor
 */
esp_err_t ezo_sensor_init(ezo_sensor_t *sensor, i2c_master_bus_handle_t bus_handle, uint8_t i2c_address) {
    if (sensor == NULL || bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing EZO sensor at address 0x%02X", i2c_address);

    // Initialize configuration
    memset(&sensor->config, 0, sizeof(ezo_sensor_config_t));
    sensor->config.i2c_address = i2c_address;
    sensor->bus_handle = bus_handle;

    // Create I2C device handle
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &sensor->dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get device information
    ret = ezo_sensor_get_device_info(sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get device info, continuing anyway");
    }

    ESP_LOGI(TAG, "EZO sensor initialized: Type=%s, FW=%s", 
             sensor->config.type, sensor->config.firmware_version);

    return ESP_OK;
}

/**
 * @brief Deinitialize EZO sensor
 */
esp_err_t ezo_sensor_deinit(ezo_sensor_t *sensor) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sensor->dev_handle != NULL) {
        esp_err_t ret = i2c_master_bus_rm_device(sensor->dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove I2C device: %s", esp_err_to_name(ret));
            return ret;
        }
        sensor->dev_handle = NULL;
    }

    return ESP_OK;
}

/**
 * @brief Get device information
 */
esp_err_t ezo_sensor_get_device_info(ezo_sensor_t *sensor) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    // Send info command
    esp_err_t ret = ezo_sensor_send_command(sensor, "i", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?I,<type>,<version>
    char *token = strtok(response, ",");
    int field = 0;
    
    while (token != NULL) {
        if (field == 0 && strcmp(token, "?I") == 0) {
            // Valid info response
        } else if (field == 1) {
            // Sensor type
            strncpy(sensor->config.type, token, EZO_MAX_SENSOR_TYPE - 1);
        } else if (field == 2) {
            // Firmware version
            strncpy(sensor->config.firmware_version, token, EZO_MAX_FW_VERSION - 1);
        }
        token = strtok(NULL, ",");
        field++;
    }

    // Get sensor name
    ret = ezo_sensor_get_name(sensor, sensor->config.name, sizeof(sensor->config.name));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get sensor name");
    }

    // Get LED status
    ret = ezo_sensor_get_led(sensor, &sensor->config.led_control);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get LED status");
    }

    // Get protocol lock status
    ret = ezo_sensor_get_plock(sensor, &sensor->config.protocol_lock);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get protocol lock status");
    }

    // Get sensor-specific parameters
    if (strcmp(sensor->config.type, EZO_TYPE_RTD) == 0) {
        ezo_rtd_get_scale(sensor, &sensor->config.rtd.temperature_scale);
    } else if (strcmp(sensor->config.type, EZO_TYPE_PH) == 0) {
        ezo_ph_get_extended_scale(sensor, &sensor->config.ph.extended_scale);
    } else if (strcmp(sensor->config.type, EZO_TYPE_EC) == 0) {
        ezo_ec_get_probe_type(sensor, &sensor->config.ec.probe_type);
        ezo_ec_get_tds_factor(sensor, &sensor->config.ec.tds_conversion_factor);
    }

    return ESP_OK;
}

/**
 * @brief Read sensor value
 */
esp_err_t ezo_sensor_read(ezo_sensor_t *sensor, float *value) {
    if (sensor == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "R", response, sizeof(response), EZO_LONG_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse the numeric response
    *value = atof(response);
    
    ESP_LOGI(TAG, "Sensor 0x%02X read: %.2f", sensor->config.i2c_address, *value);
    
    return ESP_OK;
}

/**
 * @brief Get sensor name
 */
esp_err_t ezo_sensor_get_name(ezo_sensor_t *sensor, char *name, size_t name_size) {
    if (sensor == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "Name,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?Name,<name>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?Name") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            strncpy(name, token, name_size - 1);
            name[name_size - 1] = '\0';
        }
    }

    return ESP_OK;
}

/**
 * @brief Set sensor name
 */
esp_err_t ezo_sensor_set_name(ezo_sensor_t *sensor, const char *name) {
    if (sensor == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[32];
    snprintf(command, sizeof(command), "Name,%s", name);
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        strncpy(sensor->config.name, name, EZO_MAX_SENSOR_NAME - 1);
    }
    
    return ret;
}

/**
 * @brief Get LED status
 */
esp_err_t ezo_sensor_get_led(ezo_sensor_t *sensor, bool *enabled) {
    if (sensor == NULL || enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "L,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?L,<0|1>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?L") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            *enabled = (atoi(token) == 1);
        }
    }

    return ESP_OK;
}

/**
 * @brief Set LED control
 */
esp_err_t ezo_sensor_set_led(ezo_sensor_t *sensor, bool enabled) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *command = enabled ? "L,1" : "L,0";
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.led_control = enabled;
    }
    
    return ret;
}

/**
 * @brief Get protocol lock status
 */
esp_err_t ezo_sensor_get_plock(ezo_sensor_t *sensor, bool *locked) {
    if (sensor == NULL || locked == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "Plock,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?Plock,<0|1>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?Plock") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            *locked = (atoi(token) == 1);
        }
    }

    return ESP_OK;
}

/**
 * @brief Set protocol lock
 */
esp_err_t ezo_sensor_set_plock(ezo_sensor_t *sensor, bool locked) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *command = locked ? "Plock,1" : "Plock,0";
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.protocol_lock = locked;
    }
    
    return ret;
}

/**
 * @brief Factory reset
 */
esp_err_t ezo_sensor_factory_reset(ezo_sensor_t *sensor) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "Factory resetting sensor at 0x%02X", sensor->config.i2c_address);
    
    return ezo_sensor_send_command(sensor, "Factory", NULL, 0, EZO_SHORT_WAIT_MS);
}

/**
 * @brief Change I2C address
 */
esp_err_t ezo_sensor_change_i2c_address(ezo_sensor_t *sensor, uint8_t new_address) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[16];
    snprintf(command, sizeof(command), "I2C,%d", new_address);
    
    ESP_LOGW(TAG, "Changing I2C address from 0x%02X to 0x%02X (device will reboot)", 
             sensor->config.i2c_address, new_address);
    
    return ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
}

// EC-specific functions
esp_err_t ezo_ec_get_probe_type(ezo_sensor_t *sensor, float *probe_type) {
    if (sensor == NULL || probe_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "K,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?K,<value>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?K") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            *probe_type = atof(token);
        }
    }

    return ESP_OK;
}

esp_err_t ezo_ec_set_probe_type(ezo_sensor_t *sensor, float probe_type) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[16];
    snprintf(command, sizeof(command), "K,%.2f", probe_type);
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.ec.probe_type = probe_type;
    }
    
    return ret;
}

esp_err_t ezo_ec_get_tds_factor(ezo_sensor_t *sensor, float *factor) {
    if (sensor == NULL || factor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "TDS,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?TDS,<value>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?TDS") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            *factor = atof(token);
        }
    }

    return ESP_OK;
}

esp_err_t ezo_ec_set_tds_factor(ezo_sensor_t *sensor, float factor) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[16];
    snprintf(command, sizeof(command), "TDS,%.2f", factor);
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.ec.tds_conversion_factor = factor;
    }
    
    return ret;
}

esp_err_t ezo_ec_set_output_parameter(ezo_sensor_t *sensor, const char *param, bool enabled) {
    if (sensor == NULL || param == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[16];
    snprintf(command, sizeof(command), "O,%s,%d", param, enabled ? 1 : 0);
    
    return ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
}

// RTD-specific functions
esp_err_t ezo_rtd_get_scale(ezo_sensor_t *sensor, char *scale) {
    if (sensor == NULL || scale == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "S,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?S,<scale>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?S") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL && strlen(token) > 0) {
            *scale = token[0];
        }
    }

    return ESP_OK;
}

esp_err_t ezo_rtd_set_scale(ezo_sensor_t *sensor, char scale) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[8];
    snprintf(command, sizeof(command), "S,%c", scale);
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.rtd.temperature_scale = scale;
    }
    
    return ret;
}

// pH-specific functions
esp_err_t ezo_ph_get_extended_scale(ezo_sensor_t *sensor, bool *enabled) {
    if (sensor == NULL || enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[EZO_LARGEST_STRING] = {0};
    
    esp_err_t ret = ezo_sensor_send_command(sensor, "pHext,?", response, sizeof(response), EZO_SHORT_WAIT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse response: ?pHext,<0|1>
    char *token = strtok(response, ",");
    if (token != NULL && strcmp(token, "?pHext") == 0) {
        token = strtok(NULL, ",");
        if (token != NULL) {
            *enabled = (atoi(token) == 1);
        }
    }

    return ESP_OK;
}

esp_err_t ezo_ph_set_extended_scale(ezo_sensor_t *sensor, bool enabled) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[16];
    snprintf(command, sizeof(command), "pHext,%d", enabled ? 1 : 0);
    
    esp_err_t ret = ezo_sensor_send_command(sensor, command, NULL, 0, EZO_SHORT_WAIT_MS);
    if (ret == ESP_OK) {
        sensor->config.ph.extended_scale = enabled;
    }
    
    return ret;
}
