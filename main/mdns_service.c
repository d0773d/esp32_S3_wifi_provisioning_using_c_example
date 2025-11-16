/**
 * @file mdns_service.c
 * @brief mDNS service implementation
 */

#include "mdns_service.h"
#include "mdns.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MDNS";

esp_err_t mdns_service_init(const char *hostname, const char *instance_name)
{
    if (hostname == NULL || instance_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing mDNS service");
    ESP_LOGI(TAG, "Hostname: %s.local", hostname);
    
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set hostname
    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }
    
    // Set instance name
    err = mdns_instance_name_set(instance_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set instance name: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }
    
    ESP_LOGI(TAG, "mDNS service started successfully");
    ESP_LOGI(TAG, "Device accessible at: https://%s.local", hostname);
    
    return ESP_OK;
}

esp_err_t mdns_service_add_https(uint16_t port)
{
    ESP_LOGI(TAG, "Adding HTTPS service on port %d", port);
    
    // Add HTTPS service
    esp_err_t err = mdns_service_add(NULL, "_https", "_tcp", port, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add HTTPS service: %s", esp_err_to_name(err));
        return err;
    }
    
    // Add HTTP service for discovery
    err = mdns_service_add(NULL, "_http", "_tcp", port, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add HTTP service: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "HTTPS service added to mDNS");
    
    return ESP_OK;
}

void mdns_service_deinit(void)
{
    ESP_LOGI(TAG, "Stopping mDNS service");
    mdns_free();
}
