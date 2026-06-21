idf_component_register(SRCS  
                            "main.cpp"
                            "app_state.cpp"
                            "web_server.cpp"
                            "llcc68.cpp"
                            "config_storage.cpp"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS 
                            "."
                       REQUIRES 
                            driver
                            esp_wifi
                            esp_event
                            esp_netif
                            esp_http_server
                            spiffs
                            led_strip
                            nvs_flash)
