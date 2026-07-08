idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES
                            hardware_driver
                            player
                            esp_adc
                            driver
                            esp_timer
                            esp_wifi
                            esp_http_client
                            nvs_flash
                            esp_event
                            esp_netif
                            mbedtls)