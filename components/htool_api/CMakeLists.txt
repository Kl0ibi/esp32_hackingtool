idf_component_register(SRCS "htool_api.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_wifi htool_display esp_http_server htool_pn532_spi console htool_uart htool_ble
        )
