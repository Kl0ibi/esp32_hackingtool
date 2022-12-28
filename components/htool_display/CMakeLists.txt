idf_component_register(SRCS "htool_display.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_api hagl esp_i2c_helper hagl_hal htool_wifi
        )
