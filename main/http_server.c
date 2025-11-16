/**
 * @file http_server.c
 * @brief HTTPS web server implementation
 */

#include "http_server.h"
#include "cloud_provisioning.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "api_key_manager.h"
#include "esp_log.h"
#include "esp_https_server.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP_SERVER";

static httpd_handle_t s_server = NULL;

// HTML dashboard content (embedded)
static const char *HTML_DASHBOARD = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 Device Dashboard</title>"
"<style>"
"* { margin: 0; padding: 0; box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; "
"       background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); "
"       min-height: 100vh; padding: 20px; }"
".container { max-width: 1000px; margin: 0 auto; }"
".card { background: white; border-radius: 12px; padding: 24px; margin-bottom: 20px; "
"        box-shadow: 0 4px 6px rgba(0,0,0,0.1); }"
".header { text-align: center; color: white; margin-bottom: 30px; }"
".header h1 { font-size: 2.5em; margin-bottom: 10px; }"
".header p { opacity: 0.9; font-size: 1.1em; }"
".status { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }"
".status-dot { width: 12px; height: 12px; border-radius: 50%; }"
".status-dot.online { background: #10b981; box-shadow: 0 0 10px #10b981; }"
".status-dot.offline { background: #ef4444; }"
".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
".info-item { padding: 15px; background: #f3f4f6; border-radius: 8px; }"
".info-label { font-size: 0.875em; color: #6b7280; margin-bottom: 5px; }"
".info-value { font-size: 1.25em; font-weight: 600; color: #111827; }"
".btn { background: #667eea; color: white; border: none; padding: 12px 24px; "
"       border-radius: 8px; cursor: pointer; font-size: 1em; transition: all 0.3s; }"
".btn:hover { background: #5568d3; transform: translateY(-2px); }"
".btn-danger { background: #ef4444; }"
".btn-danger:hover { background: #dc2626; }"
"h2 { color: #111827; margin-bottom: 20px; font-size: 1.5em; }"
".refresh-btn { float: right; }"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<div class='header'>"
"<h1>🌐 ESP32 Device Dashboard</h1>"
"<p>Secure Device Management Interface</p>"
"</div>"
"<div class='card'>"
"<div class='status'>"
"<div class='status-dot online'></div>"
"<h2>Device Status: Online</h2>"
"<button class='btn refresh-btn' onclick='location.reload()'>🔄 Refresh</button>"
"</div>"
"<div class='info-grid'>"
"<div class='info-item'>"
"<div class='info-label'>Device ID</div>"
"<div class='info-value' id='device-id'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>WiFi SSID</div>"
"<div class='info-value' id='wifi-ssid'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>IP Address</div>"
"<div class='info-value' id='ip-addr'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>Uptime</div>"
"<div class='info-value' id='uptime'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>Current Time</div>"
"<div class='info-value' id='current-time'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>Free Heap</div>"
"<div class='info-value' id='free-heap'>Loading...</div>"
"</div>"
"</div>"
"</div>"
"<div class='card'>"
"<h2>⚙️ Actions</h2>"
"<button class='btn' onclick='testConnection()'>Test Cloud Connection</button>"
"<button class='btn btn-danger' onclick='if(confirm(\"Clear WiFi credentials and restart?\")) clearWiFi()'>Clear WiFi</button>"
"</div>"
"</div>"
"<script>"
"async function loadStatus() {"
"  try {"
"    const res = await fetch('/api/status');"
"    const data = await res.json();"
"    document.getElementById('device-id').textContent = data.device_id;"
"    document.getElementById('wifi-ssid').textContent = data.wifi_ssid;"
"    document.getElementById('ip-addr').textContent = data.ip_address;"
"    document.getElementById('uptime').textContent = Math.floor(data.uptime / 60) + ' min';"
"    document.getElementById('current-time').textContent = data.current_time;"
"    document.getElementById('free-heap').textContent = (data.free_heap / 1024).toFixed(1) + ' KB';"
"  } catch(e) { console.error('Failed to load status:', e); }"
"}"
"async function testConnection() {"
"  alert('Testing cloud connection...');"
"}"
"async function clearWiFi() {"
"  await fetch('/api/clear-wifi', {method: 'POST'});"
"  alert('WiFi cleared. Device will restart.');"
"}"
"loadStatus();"
"setInterval(loadStatus, 5000);"
"</script>"
"</body>"
"</html>";

/**
 * @brief Root handler - serve dashboard
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_DASHBOARD, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief API status endpoint - return device status as JSON
 */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
    // Device ID
    char device_id[32];
    cloud_prov_get_device_id(device_id, sizeof(device_id));
    cJSON_AddStringToObject(root, "device_id", device_id);
    
    // WiFi SSID
    char ssid[33];
    char password[64];
    if (wifi_manager_get_stored_credentials(ssid, password) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", ssid);
        memset(password, 0, sizeof(password)); // Clear password
    } else {
        cJSON_AddStringToObject(root, "wifi_ssid", "Not configured");
    }
    
    // IP Address
    if (wifi_manager_is_connected()) {
        // TODO: Get actual IP address from WiFi manager
        cJSON_AddStringToObject(root, "ip_address", "Connected");
    } else {
        cJSON_AddStringToObject(root, "ip_address", "Disconnected");
    }
    
    // Uptime
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    
    // Current time
    char time_str[64];
    if (time_sync_get_time_string(time_str, sizeof(time_str), NULL) == ESP_OK) {
        cJSON_AddStringToObject(root, "current_time", time_str);
    } else {
        cJSON_AddStringToObject(root, "current_time", "Not synced");
    }
    
    // Free heap
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    
    // Send JSON response
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief API clear WiFi endpoint
 */
static esp_err_t api_clear_wifi_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "WiFi clear requested via dashboard");
    
    // Clear WiFi credentials
    wifi_manager_clear_credentials();
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    
    // Restart device after a delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

// URI handlers
static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_clear_wifi_uri = {
    .uri = "/api/clear-wifi",
    .method = HTTP_POST,
    .handler = api_clear_wifi_handler,
    .user_ctx = NULL
};

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "HTTPS server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting HTTPS server...");
    
    // Get certificates from cloud provisioning
    char *certificate = malloc(CLOUD_PROV_MAX_CERT_SIZE);
    char *private_key = malloc(CLOUD_PROV_MAX_KEY_SIZE);
    
    if (certificate == NULL || private_key == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificates");
        free(certificate);
        free(private_key);
        return ESP_ERR_NO_MEM;
    }
    
    size_t cert_len, key_len;
    esp_err_t err = cloud_prov_get_certificate(certificate, &cert_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate: %s", esp_err_to_name(err));
        free(certificate);
        free(private_key);
        return err;
    }
    
    err = cloud_prov_get_private_key(private_key, &key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get private key: %s", esp_err_to_name(err));
        free(certificate);
        free(private_key);
        return err;
    }
    
    ESP_LOGI(TAG, "Certificate length: %zu bytes", cert_len);
    ESP_LOGI(TAG, "Private key length: %zu bytes", key_len);
    
    // Debug certificate format
    ESP_LOGI(TAG, "Cert first 100 chars: %.100s", certificate);
    if (cert_len > 100) {
        ESP_LOGI(TAG, "Cert last 100 chars: %s", certificate + cert_len - 100);
    }
    
    // Configure HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.max_uri_handlers = 10;
    config.httpd.stack_size = 10240;
    
    // Set certificates (PEM format from NVS is already null-terminated)
    // mbedTLS needs length + 1 to include the null terminator
    config.servercert = (const uint8_t *)certificate;
    config.servercert_len = cert_len + 1;
    config.prvtkey_pem = (const uint8_t *)private_key;
    config.prvtkey_len = key_len + 1;
    
    // Start server
    err = httpd_ssl_start(&s_server, &config);
    
    // Clean up certificate buffers
    free(certificate);
    free(private_key);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
        return err;
    }
    
    // Register URI handlers
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &api_status_uri);
    httpd_register_uri_handler(s_server, &api_clear_wifi_uri);
    
    ESP_LOGI(TAG, "✓ HTTPS server started successfully");
    ESP_LOGI(TAG, "Dashboard accessible at: https://<device-ip>");
    
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping HTTPS server");
    httpd_ssl_stop(s_server);
    s_server = NULL;
    
    return ESP_OK;
}

bool http_server_is_running(void)
{
    return (s_server != NULL);
}
