idf_component_register(SRCS "airhive_networking.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_driver_gpio nvs_flash)
