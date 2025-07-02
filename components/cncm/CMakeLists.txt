idf_component_register(SRCS "cncm.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio nvs_flash)
