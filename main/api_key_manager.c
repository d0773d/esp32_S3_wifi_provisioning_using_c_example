/**
 * @file api_key_manager.c
 * @brief API key management implementation
 */

#include "api_key_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
#include <string.h>
#include <time.h>

static const char *TAG = "API_KEY_MGR";

// NVS namespace for API keys
#define NVS_NAMESPACE "api_keys"
#define NVS_KEY_COUNT "key_count"
#define NVS_KEY_PREFIX "key_"

// In-memory cache of API keys
static api_key_t s_api_keys[API_KEY_MAX_COUNT];
static size_t s_key_count = 0;
static bool s_initialized = false;

// Helper function to save keys to NVS
static esp_err_t save_keys_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save key count
    err = nvs_set_u32(nvs_handle, NVS_KEY_COUNT, s_key_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save key count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Save each key
    for (size_t i = 0; i < s_key_count; i++) {
        char key_name[16];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_PREFIX, (int)i);
        
        err = nvs_set_blob(nvs_handle, key_name, &s_api_keys[i], sizeof(api_key_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save key %zu: %s", i, esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved %zu API keys to NVS", s_key_count);
    }
    
    return err;
}

// Helper function to load keys from NVS
static esp_err_t load_keys_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored API keys found");
        s_key_count = 0;
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Load key count
    uint32_t count = 0;
    err = nvs_get_u32(nvs_handle, NVS_KEY_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored API keys found");
        s_key_count = 0;
        nvs_close(nvs_handle);
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load key count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    s_key_count = count > API_KEY_MAX_COUNT ? API_KEY_MAX_COUNT : count;
    
    // Load each key
    for (size_t i = 0; i < s_key_count; i++) {
        char key_name[16];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_PREFIX, (int)i);
        
        size_t required_size = sizeof(api_key_t);
        err = nvs_get_blob(nvs_handle, key_name, &s_api_keys[i], &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load key %zu: %s", i, esp_err_to_name(err));
        }
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Loaded %zu API keys from NVS", s_key_count);
    
    return ESP_OK;
}

esp_err_t api_key_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "API key manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing API key manager");
    
    // Clear in-memory keys
    memset(s_api_keys, 0, sizeof(s_api_keys));
    s_key_count = 0;
    
    // Load keys from NVS
    esp_err_t err = load_keys_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load keys from NVS");
        return err;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "API key manager initialized with %zu keys", s_key_count);
    
    return ESP_OK;
}

esp_err_t api_key_manager_add(const char *name, const char *key, api_key_type_t type)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "API key manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_key_count >= API_KEY_MAX_COUNT) {
        ESP_LOGE(TAG, "Maximum API key count reached");
        return ESP_ERR_NO_MEM;
    }
    
    // Check if key name already exists
    for (size_t i = 0; i < s_key_count; i++) {
        if (strcmp(s_api_keys[i].name, name) == 0) {
            ESP_LOGW(TAG, "API key with name '%s' already exists", name);
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    // Create new key
    api_key_t new_key = {0};
    strncpy(new_key.name, name, API_KEY_NAME_MAX_LENGTH - 1);
    strncpy(new_key.key, key, API_KEY_MAX_LENGTH - 1);
    new_key.type = type;
    new_key.enabled = true;
    new_key.created_timestamp = (uint32_t)time(NULL);
    new_key.last_used_timestamp = 0;
    new_key.use_count = 0;
    
    // Add to array
    s_api_keys[s_key_count] = new_key;
    s_key_count++;
    
    // Save to NVS
    esp_err_t err = save_keys_to_nvs();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Added API key '%s' (type: %d)", name, type);
    }
    
    return err;
}

esp_err_t api_key_manager_delete(const char *name)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find key by name
    int found_index = -1;
    for (size_t i = 0; i < s_key_count; i++) {
        if (strcmp(s_api_keys[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        ESP_LOGW(TAG, "API key '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining keys
    for (size_t i = found_index; i < s_key_count - 1; i++) {
        s_api_keys[i] = s_api_keys[i + 1];
    }
    
    s_key_count--;
    
    // Clear last slot
    memset(&s_api_keys[s_key_count], 0, sizeof(api_key_t));
    
    // Save to NVS
    esp_err_t err = save_keys_to_nvs();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted API key '%s'", name);
    }
    
    return err;
}

esp_err_t api_key_manager_set_enabled(const char *name, bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find key by name
    for (size_t i = 0; i < s_key_count; i++) {
        if (strcmp(s_api_keys[i].name, name) == 0) {
            s_api_keys[i].enabled = enabled;
            esp_err_t err = save_keys_to_nvs();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "API key '%s' %s", name, enabled ? "enabled" : "disabled");
            }
            return err;
        }
    }
    
    ESP_LOGW(TAG, "API key '%s' not found", name);
    return ESP_ERR_NOT_FOUND;
}

bool api_key_manager_validate(const char *key, api_key_type_t type)
{
    if (!s_initialized || key == NULL) {
        return false;
    }
    
    // Check against all keys
    for (size_t i = 0; i < s_key_count; i++) {
        // Skip disabled keys
        if (!s_api_keys[i].enabled) {
            continue;
        }
        
        // Check type if specified (type == -1 means check all types)
        if (type != -1 && s_api_keys[i].type != type) {
            continue;
        }
        
        // Compare keys
        if (strcmp(s_api_keys[i].key, key) == 0) {
            // Update usage stats
            s_api_keys[i].last_used_timestamp = (uint32_t)time(NULL);
            s_api_keys[i].use_count++;
            
            // Save updated stats (non-blocking, don't check error)
            save_keys_to_nvs();
            
            ESP_LOGI(TAG, "API key '%s' validated successfully", s_api_keys[i].name);
            return true;
        }
    }
    
    ESP_LOGW(TAG, "Invalid API key provided");
    return false;
}

esp_err_t api_key_manager_get(const char *name, api_key_t *key_out)
{
    if (!s_initialized || name == NULL || key_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < s_key_count; i++) {
        if (strcmp(s_api_keys[i].name, name) == 0) {
            *key_out = s_api_keys[i];
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t api_key_manager_get_all(api_key_t *keys, size_t *count)
{
    if (!s_initialized || keys == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(keys, s_api_keys, sizeof(api_key_t) * s_key_count);
    *count = s_key_count;
    
    return ESP_OK;
}

esp_err_t api_key_manager_get_by_type(api_key_type_t type, api_key_t *key_out)
{
    if (!s_initialized || key_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < s_key_count; i++) {
        if (s_api_keys[i].type == type && s_api_keys[i].enabled) {
            *key_out = s_api_keys[i];
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t api_key_manager_generate(char *key_out, size_t length)
{
    if (key_out == NULL || length == 0 || length >= API_KEY_MAX_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Character set for API keys (alphanumeric)
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const size_t charset_size = sizeof(charset) - 1;
    
    // Generate random key
    for (size_t i = 0; i < length; i++) {
        uint32_t random_val = esp_random();
        key_out[i] = charset[random_val % charset_size];
    }
    key_out[length] = '\0';
    
    ESP_LOGI(TAG, "Generated %zu-character API key", length);
    return ESP_OK;
}

esp_err_t api_key_manager_clear_all(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Clearing all API keys");
    
    // Clear in-memory keys
    memset(s_api_keys, 0, sizeof(s_api_keys));
    s_key_count = 0;
    
    // Clear NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_erase_all(nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "All API keys cleared");
    }
    
    return err;
}
