idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        PsychicHttp
        esp_timer
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_https_server
        mdns
        littlefs
        #ArduinoJson
)
# generat data partition and flash manaual
# littlefs_create_partition_image(littlefs ${project_dir}/data)
# generat data partition and flash the time with project
littlefs_create_partition_image(littlefs ${project_dir}/data FLASH_IN_PROJECT)