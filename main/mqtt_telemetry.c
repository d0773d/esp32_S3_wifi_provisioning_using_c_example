/**
 * @file mqtt_client.c
 * @brief MQTT client implementation for cloud telemetry
 */

#include "mqtt_telemetry.h"
#include "cloud_provisioning.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h" // ESP-IDF MQTT client
#include <string.h>
#include <sys/time.h>

static const char *TAG = "MQTT_CLIENT";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_state_t s_mqtt_state = MQTT_STATE_DISCONNECTED;
static esp_timer_handle_t s_telemetry_timer = NULL;
static uint32_t s_telemetry_interval_sec = 15; // Default: 15 seconds for testing
static uint32_t s_mqtt_reconnects = 0;
static char s_device_id[32] = {0};

// Forward declarations
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void telemetry_timer_callback(void *arg);

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✓ Connected to MQTT broker");
            s_mqtt_state = MQTT_STATE_CONNECTED;
            
            // Subscribe to kannacloud/test topic
            esp_mqtt_client_subscribe(s_mqtt_client, "kannacloud/test", 1);
            ESP_LOGI(TAG, "Subscribed to: kannacloud/test");
            
            // Publish test message
            esp_mqtt_client_publish(s_mqtt_client, "kannacloud/test", "Hello World!!!", 0, 1, 0);
            ESP_LOGI(TAG, "Published test message: Hello World!!!");
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT broker");
            s_mqtt_state = MQTT_STATE_DISCONNECTED;
            s_mqtt_reconnects++;
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received message on topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);
            
            // Parse command (could be extended to handle different commands)
            if (event->data_len > 0) {
                cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
                if (root) {
                    cJSON *cmd = cJSON_GetObjectItem(root, "command");
                    if (cmd && cJSON_IsString(cmd)) {
                        ESP_LOGI(TAG, "Command received: %s", cmd->valuestring);
                        
                        // Handle commands here (reboot, update settings, etc.)
                        if (strcmp(cmd->valuestring, "reboot") == 0) {
                            ESP_LOGW(TAG, "Reboot command received, restarting in 3 seconds...");
                            mqtt_publish_status("rebooting");
                            vTaskDelay(pdMS_TO_TICKS(3000));
                            esp_restart();
                        } else if (strcmp(cmd->valuestring, "ping") == 0) {
                            mqtt_publish_status("pong");
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            s_mqtt_state = MQTT_STATE_ERROR;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error");
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused");
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief Telemetry timer callback - publishes test message periodically
 */
static void telemetry_timer_callback(void *arg)
{
    if (s_mqtt_state != MQTT_STATE_CONNECTED) {
        return;
    }
    
    // Publish "Hello World!!!" message
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, "kannacloud/test", "Hello World!!!", 0, 1, 0);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Published: Hello World!!! (msg_id=%d)", msg_id);
    } else {
        ESP_LOGW(TAG, "Failed to publish Hello World message");
    }
}

esp_err_t mqtt_client_init(const char *broker_uri, const char *username, const char *password)
{
    if (s_mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return ESP_OK;
    }
    
    // Get device ID from cloud provisioning
    cloud_prov_get_device_id(s_device_id, sizeof(s_device_id));
    
    ESP_LOGI(TAG, "Initializing MQTT client");
    ESP_LOGI(TAG, "Broker URI: %s", broker_uri);
    ESP_LOGI(TAG, "Device ID: %s", s_device_id);
    if (username) {
        ESP_LOGI(TAG, "Username: %s", username);
    }
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = s_device_id,
        .credentials.username = username,
        .credentials.authentication.password = password,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
        .network.reconnect_timeout_ms = 10000,
        .network.timeout_ms = 10000,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
    };
    
    // Create MQTT client
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    // Register event handler
    esp_err_t ret = esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "✓ MQTT client initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting MQTT client...");
    s_mqtt_state = MQTT_STATE_CONNECTING;
    
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        s_mqtt_state = MQTT_STATE_ERROR;
        return ret;
    }
    
    // Create telemetry timer if interval is set
    if (s_telemetry_interval_sec > 0 && s_telemetry_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = telemetry_timer_callback,
            .arg = NULL,
            .name = "telemetry_timer",
            .skip_unhandled_events = true
        };
        
        ret = esp_timer_create(&timer_args, &s_telemetry_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create telemetry timer: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = esp_timer_start_periodic(s_telemetry_timer, s_telemetry_interval_sec * 1000000ULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start telemetry timer: %s", esp_err_to_name(ret));
            esp_timer_delete(s_telemetry_timer);
            s_telemetry_timer = NULL;
            return ret;
        }
        
        ESP_LOGI(TAG, "✓ Telemetry timer started (interval: %lu seconds)", s_telemetry_interval_sec);
    }
    
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_OK;
    }
    
    // Stop telemetry timer
    if (s_telemetry_timer != NULL) {
        esp_timer_stop(s_telemetry_timer);
        esp_timer_delete(s_telemetry_timer);
        s_telemetry_timer = NULL;
    }
    
    // Publish offline status before disconnecting
    if (s_mqtt_state == MQTT_STATE_CONNECTED) {
        mqtt_publish_status("offline");
        vTaskDelay(pdMS_TO_TICKS(500)); // Give time for message to send
    }
    
    ESP_LOGI(TAG, "Stopping MQTT client");
    esp_err_t ret = esp_mqtt_client_stop(s_mqtt_client);
    s_mqtt_state = MQTT_STATE_DISCONNECTED;
    
    return ret;
}

esp_err_t mqtt_client_deinit(void)
{
    mqtt_client_stop();
    
    if (s_mqtt_client != NULL) {
        ESP_LOGI(TAG, "Deinitializing MQTT client");
        esp_err_t ret = esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_mqtt_state = MQTT_STATE_DISCONNECTED;
        return ret;
    }
    
    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return s_mqtt_state == MQTT_STATE_CONNECTED;
}

mqtt_state_t mqtt_client_get_state(void)
{
    return s_mqtt_state;
}

esp_err_t mqtt_publish_telemetry(const telemetry_data_t *data)
{
    if (s_mqtt_client == NULL || s_mqtt_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create JSON payload
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddNumberToObject(root, "uptime", data->uptime_sec);
    cJSON_AddNumberToObject(root, "free_heap", data->free_heap);
    cJSON_AddNumberToObject(root, "rssi", data->rssi);
    cJSON_AddNumberToObject(root, "cpu_temp", data->cpu_temp);
    cJSON_AddNumberToObject(root, "wifi_reconnects", data->wifi_reconnects);
    cJSON_AddNumberToObject(root, "mqtt_reconnects", data->mqtt_reconnects);
    
    // Add timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Publish to telemetry topic
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/telemetry", s_device_id);
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_str, 0, 1, 0);
    free(json_str);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish telemetry");
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Telemetry published (msg_id: %d)", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_status(const char *status)
{
    if (s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create JSON payload
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(root, "status", status);
    
    // Add timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Publish to status topic
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/status", s_device_id);
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_str, 0, 1, 1); // QoS 1, Retain
    free(json_str);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish status");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Status published: %s (msg_id: %d)", status, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_json(const char *topic, const char *json_data, int qos, bool retain)
{
    if (s_mqtt_client == NULL || s_mqtt_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (topic == NULL || json_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, json_data, 0, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Published to %s (msg_id: %d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_subscribe(const char *topic, int qos)
{
    if (s_mqtt_client == NULL || s_mqtt_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Subscribed to %s (msg_id: %d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_unsubscribe(const char *topic)
{
    if (s_mqtt_client == NULL || s_mqtt_state != MQTT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_unsubscribe(s_mqtt_client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Unsubscribed from %s (msg_id: %d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_set_telemetry_interval(uint32_t interval_sec)
{
    s_telemetry_interval_sec = interval_sec;
    
    if (s_telemetry_timer != NULL) {
        esp_timer_stop(s_telemetry_timer);
        
        if (interval_sec > 0) {
            esp_err_t ret = esp_timer_start_periodic(s_telemetry_timer, interval_sec * 1000000ULL);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restart telemetry timer: %s", esp_err_to_name(ret));
                return ret;
            }
            ESP_LOGI(TAG, "Telemetry interval updated to %lu seconds", interval_sec);
        } else {
            ESP_LOGI(TAG, "Telemetry timer disabled");
        }
    }
    
    return ESP_OK;
}

esp_err_t mqtt_get_device_id(char *device_id, size_t size)
{
    if (device_id == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(device_id, s_device_id, size - 1);
    device_id[size - 1] = '\0';
    
    return ESP_OK;
}
