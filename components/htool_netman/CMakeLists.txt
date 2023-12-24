idf_component_register(SRCS "htool_netman.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_api htool_wifi json esp_http_client htool_system htool_modbus)
