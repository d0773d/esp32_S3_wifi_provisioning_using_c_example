/**
 * @file sensor_manager.c
 * @brief Sensor manager implementation for MAX17048 and EZO sensors
 */

#include "sensor_manager.h"
#include "i2c_scanner.h"
#include "max17048.h"
#include "ezo_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SENSOR_MGR";

// Sensor handles
static max17048_t s_battery_monitor;
static bool s_battery_available = false;

#define MAX_EZO_SENSORS 5
static ezo_sensor_t s_ezo_sensors[MAX_EZO_SENSORS];
static uint8_t s_ezo_count = 0;

// EZO sensor type indices
static int s_rtd_index = -1;  // Temperature
static int s_ph_index = -1;   // pH
static int s_ec_index = -1;   // Electrical conductivity
static int s_do_index = -1;   // Dissolved oxygen
static int s_orp_index = -1;  // ORP
static int s_hum_index = -1;  // Humidity

// Cached sensor readings (last successful values) - old per-sensor cache
typedef struct {
    float values[4];
    uint8_t count;
    bool valid;
    uint32_t timestamp_ms;
} cached_sensor_data_t;

static cached_sensor_data_t s_cached_readings[MAX_EZO_SENSORS] = {0};
#define CACHE_TIMEOUT_MS 300000  // 5 minutes - consider cached data stale after this

// Global sensor cache for API access
static sensor_cache_t s_sensor_cache = {0};
static bool s_cache_valid = false;
static SemaphoreHandle_t s_cache_mutex = NULL;

// Background reading task
static TaskHandle_t s_reading_task_handle = NULL;
static uint32_t s_reading_interval_sec = 10;
static bool s_reading_paused = false;
static bool s_reading_in_progress = false;

// Forward declaration
static void sensor_reading_task(void *arg);

/**
 * @brief Initialize all sensors
 */
esp_err_t sensor_manager_init(void) {
    ESP_LOGI(TAG, "Initializing sensor manager");
    
    i2c_master_bus_handle_t bus_handle = i2c_scanner_get_bus_handle();
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize MAX17048 battery monitor at 0x36
    if (i2c_scanner_device_exists(0x36)) {
        ESP_LOGI(TAG, "MAX17048 battery monitor detected at 0x36");
        esp_err_t ret = max17048_init(&s_battery_monitor, bus_handle);
        if (ret == ESP_OK) {
            s_battery_available = true;
            ESP_LOGI(TAG, "✓ MAX17048 initialized successfully");
            
            // Read initial values
            float voltage, soc;
            if (max17048_read_voltage(&s_battery_monitor, &voltage) == ESP_OK &&
                max17048_read_soc(&s_battery_monitor, &soc) == ESP_OK) {
                ESP_LOGI(TAG, "  Battery: %.2f V, %.1f%%", voltage, soc);
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize MAX17048");
        }
    }
    
    // Initialize EZO sensors
    // Scan for EZO sensors at known addresses (excluding 0x36 which is MAX17048)
    uint8_t ezo_addresses[] = {0x16, 0x63, 0x64, 0x6F};
    
    for (int i = 0; i < sizeof(ezo_addresses) && s_ezo_count < MAX_EZO_SENSORS; i++) {
        uint8_t addr = ezo_addresses[i];
        
        if (i2c_scanner_device_exists(addr)) {
            ESP_LOGI(TAG, "EZO sensor detected at 0x%02X", addr);
            
            esp_err_t ret = ezo_sensor_init(&s_ezo_sensors[s_ezo_count], bus_handle, addr);
            if (ret == ESP_OK) {
                ezo_sensor_t *sensor = &s_ezo_sensors[s_ezo_count];
                
                ESP_LOGI(TAG, "✓ EZO sensor initialized: Type=%s, Name=%s, FW=%s", 
                         sensor->config.type, sensor->config.name, sensor->config.firmware_version);
                
                // Map sensor type to index
                if (strcmp(sensor->config.type, EZO_TYPE_RTD) == 0) {
                    s_rtd_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → Temperature sensor (RTD)");
                } else if (strcmp(sensor->config.type, EZO_TYPE_PH) == 0) {
                    s_ph_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → pH sensor");
                } else if (strcmp(sensor->config.type, EZO_TYPE_EC) == 0) {
                    s_ec_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → Electrical Conductivity sensor");
                } else if (strcmp(sensor->config.type, EZO_TYPE_DO) == 0) {
                    s_do_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → Dissolved Oxygen sensor");
                } else if (strcmp(sensor->config.type, EZO_TYPE_ORP) == 0) {
                    s_orp_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → ORP sensor");
                } else if (strcmp(sensor->config.type, EZO_TYPE_HUM) == 0) {
                    s_hum_index = s_ezo_count;
                    ESP_LOGI(TAG, "  → Humidity sensor");
                }
                
                s_ezo_count++;
            } else {
                ESP_LOGW(TAG, "Failed to initialize EZO sensor at 0x%02X", addr);
            }
        }
    }
    
    ESP_LOGI(TAG, "Sensor manager initialized: Battery=%s, EZO sensors=%d",
             s_battery_available ? "YES" : "NO", s_ezo_count);
    
    return ESP_OK;
}

/**
 * @brief Deinitialize all sensors
 */
esp_err_t sensor_manager_deinit(void) {
    // Deinitialize MAX17048
    if (s_battery_available) {
        max17048_deinit(&s_battery_monitor);
        s_battery_available = false;
    }
    
    // Deinitialize EZO sensors
    for (int i = 0; i < s_ezo_count; i++) {
        ezo_sensor_deinit(&s_ezo_sensors[i]);
    }
    s_ezo_count = 0;
    s_rtd_index = -1;
    s_ph_index = -1;
    s_ec_index = -1;
    s_do_index = -1;
    s_orp_index = -1;
    s_hum_index = -1;
    
    // Clear cached readings
    memset(s_cached_readings, 0, sizeof(s_cached_readings));
    
    ESP_LOGI(TAG, "Sensor manager deinitialized");
    return ESP_OK;
}

/**
 * @brief Read battery voltage
 */
esp_err_t sensor_manager_read_battery_voltage(float *voltage) {
    if (!s_battery_available) {
        ESP_LOGW(TAG, "Battery monitor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return max17048_read_voltage(&s_battery_monitor, voltage);
}

/**
 * @brief Read battery percentage
 */
esp_err_t sensor_manager_read_battery_percentage(float *percentage) {
    if (!s_battery_available) {
        ESP_LOGW(TAG, "Battery monitor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return max17048_read_soc(&s_battery_monitor, percentage);
}

/**
 * @brief Read temperature from EZO-RTD
 */
esp_err_t sensor_manager_read_temperature(float *temperature) {
    if (s_rtd_index < 0) {
        ESP_LOGD(TAG, "RTD sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_rtd_index], temperature);
}

/**
 * @brief Read pH from EZO-pH
 */
esp_err_t sensor_manager_read_ph(float *ph) {
    if (s_ph_index < 0) {
        ESP_LOGD(TAG, "pH sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_ph_index], ph);
}

/**
 * @brief Read EC from EZO-EC
 */
esp_err_t sensor_manager_read_ec(float *ec) {
    if (s_ec_index < 0) {
        ESP_LOGD(TAG, "EC sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_ec_index], ec);
}

/**
 * @brief Read DO from EZO-DO
 */
esp_err_t sensor_manager_read_do(float *dox) {
    if (s_do_index < 0) {
        ESP_LOGD(TAG, "DO sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_do_index], dox);
}

/**
 * @brief Read ORP from EZO-ORP
 */
esp_err_t sensor_manager_read_orp(float *orp) {
    if (s_orp_index < 0) {
        ESP_LOGD(TAG, "ORP sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_orp_index], orp);
}

/**
 * @brief Read humidity from EZO-HUM
 */
esp_err_t sensor_manager_read_humidity(float *humidity) {
    if (s_hum_index < 0) {
        ESP_LOGD(TAG, "Humidity sensor not available");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ezo_sensor_read(&s_ezo_sensors[s_hum_index], humidity);
}

/**
 * @brief Get number of EZO sensors
 */
uint8_t sensor_manager_get_ezo_count(void) {
    return s_ezo_count;
}

/**
 * @brief Check if battery monitor is available
 */
bool sensor_manager_has_battery_monitor(void) {
    return s_battery_available;
}

/**
 * @brief Get EZO sensor handle by index
 */
void* sensor_manager_get_ezo_sensor(uint8_t index) {
    if (index >= s_ezo_count) {
        return NULL;
    }
    return &s_ezo_sensors[index];
}

/**
 * @brief Read all values from an EZO sensor by index
 * 
 * This function attempts to read fresh sensor data. If the read fails or the sensor
 * is not ready, it returns cached data from the last successful read (if available).
 */
esp_err_t sensor_manager_read_ezo_sensor(uint8_t index, char *sensor_type, float values[4], uint8_t *count) {
    if (index >= s_ezo_count || sensor_type == NULL || values == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ezo_sensor_t *sensor = &s_ezo_sensors[index];
    
    // Debug: Log the sensor type from config
    ESP_LOGD(TAG, "Reading sensor index %d: type='%s' (addr=0x%02X)", 
             index, sensor->config.type, sensor->config.i2c_address);
    
    strncpy(sensor_type, sensor->config.type, 15);
    sensor_type[15] = '\0';
    
    // Try to read fresh data from sensor
    esp_err_t ret = ezo_sensor_read_all(sensor, values, count);
    
    if (ret == ESP_OK) {
        // Success - cache the new readings
        cached_sensor_data_t *cache = &s_cached_readings[index];
        for (uint8_t i = 0; i < *count && i < 4; i++) {
            cache->values[i] = values[i];
        }
        cache->count = *count;
        cache->valid = true;
        cache->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        return ESP_OK;
    } else {
        // Read failed - try to use cached data
        cached_sensor_data_t *cache = &s_cached_readings[index];
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (cache->valid && (now_ms - cache->timestamp_ms) < CACHE_TIMEOUT_MS) {
            // Return cached data
            for (uint8_t i = 0; i < cache->count && i < 4; i++) {
                values[i] = cache->values[i];
            }
            *count = cache->count;
            ESP_LOGD(TAG, "Sensor 0x%02X read failed, using cached data (%lu ms old)", 
                     sensor->config.i2c_address, now_ms - cache->timestamp_ms);
            return ESP_OK;
        }
        
        // No valid cache available
        return ret;
    }
}

/**
 * @brief Rescan I2C bus and reinitialize sensors
 */
esp_err_t sensor_manager_rescan(void) {
    ESP_LOGI(TAG, "Rescanning I2C bus for sensors");
    
    // Deinitialize existing sensors
    sensor_manager_deinit();
    
    // Reinitialize all sensors
    return sensor_manager_init();
}

/**
 * @brief Background sensor reading task
 */
static void sensor_reading_task(void *arg) {
    ESP_LOGI(TAG, "Sensor reading task started (interval: %lu seconds)", s_reading_interval_sec);
    
    // Perform first read immediately
    bool first_read = true;
    
    while (1) {
        // Check if reading is paused
        if (s_reading_paused) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        
        // Wait before next reading (except for first read)
        if (!first_read) {
            vTaskDelay(pdMS_TO_TICKS(s_reading_interval_sec * 1000));
        }
        first_read = false;
        
        // Read all sensors
        if (s_cache_mutex != NULL && xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_reading_in_progress = true;
            s_sensor_cache.sensor_count = 0;
            s_sensor_cache.battery_valid = false;
            s_sensor_cache.timestamp_us = esp_timer_get_time();
            
            // Read battery
            if (s_battery_available) {
                float battery_pct;
                if (sensor_manager_read_battery_percentage(&battery_pct) == ESP_OK) {
                    s_sensor_cache.battery_percentage = battery_pct;
                    s_sensor_cache.battery_valid = true;
                }
            }
            
            // Read RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_sensor_cache.rssi = ap_info.rssi;
            }
            
            // Read all EZO sensors
            for (uint8_t i = 0; i < s_ezo_count && i < 8; i++) {
                cached_sensor_t *cached = &s_sensor_cache.sensors[i];
                
                if (sensor_manager_read_ezo_sensor(i, cached->sensor_type, cached->values, &cached->value_count) == ESP_OK) {
                    cached->valid = true;
                    s_sensor_cache.sensor_count++;
                } else {
                    cached->valid = false;
                }
                
                // Yield between sensor reads
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            s_cache_valid = true;
            s_reading_in_progress = false;
            xSemaphoreGive(s_cache_mutex);
            
            if (s_sensor_cache.sensor_count > 0) {
                ESP_LOGI(TAG, "✓ Cache updated with %u sensors", s_sensor_cache.sensor_count);
            }
        }
    }
}

esp_err_t sensor_manager_start_reading_task(uint32_t interval_sec) {
    if (s_reading_task_handle != NULL) {
        ESP_LOGW(TAG, "Reading task already running");
        return ESP_OK;
    }
    
    s_reading_interval_sec = interval_sec;
    
    // Create mutex for cache access
    if (s_cache_mutex == NULL) {
        s_cache_mutex = xSemaphoreCreateMutex();
        if (s_cache_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create cache mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Initialize cache as valid but empty to prevent startup warnings
    s_cache_valid = false;  // Will be set true after first read completes
    
    // Create reading task on Core 1
    BaseType_t ret = xTaskCreatePinnedToCore(
        sensor_reading_task,
        "sensor_read",
        4096,
        NULL,
        5,
        &s_reading_task_handle,
        1  // Core 1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create reading task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sensor reading task started");
    return ESP_OK;
}

esp_err_t sensor_manager_stop_reading_task(void) {
    if (s_reading_task_handle != NULL) {
        vTaskDelete(s_reading_task_handle);
        s_reading_task_handle = NULL;
        ESP_LOGI(TAG, "Sensor reading task stopped");
    }
    return ESP_OK;
}

esp_err_t sensor_manager_get_cached_data(sensor_cache_t *cache) {
    if (cache == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_cache_valid) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (s_cache_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Copy cached data (thread-safe)
    if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(cache, &s_sensor_cache, sizeof(sensor_cache_t));
        xSemaphoreGive(s_cache_mutex);
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

esp_err_t sensor_manager_set_reading_interval(uint32_t interval_sec) {
    s_reading_interval_sec = interval_sec;
    ESP_LOGI(TAG, "Reading interval updated to %lu seconds", interval_sec);
    return ESP_OK;
}

esp_err_t sensor_manager_pause_reading(void) {
    s_reading_paused = true;
    ESP_LOGI(TAG, "Sensor reading paused");
    return ESP_OK;
}

esp_err_t sensor_manager_resume_reading(void) {
    s_reading_paused = false;
    ESP_LOGI(TAG, "Sensor reading resumed");
    return ESP_OK;
}

bool sensor_manager_is_reading_in_progress(void) {
    return s_reading_in_progress;
}
