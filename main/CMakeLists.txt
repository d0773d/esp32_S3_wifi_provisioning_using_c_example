idf_component_register(SRCS "main.c" 
                             "ble_provisioning.c"
                             "wifi_manager.c"
                             "provisioning_state.c"
                             "security.c"
                             "reset_button.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_wifi json esp_partition bootloader_support efuse driver)
