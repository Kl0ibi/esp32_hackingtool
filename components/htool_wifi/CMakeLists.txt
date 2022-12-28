idf_component_register(SRCS "htool_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_api nvs_flash htool_display
        )
set_source_files_properties(htool_wifi.c
        PROPERTIES COMPILE_FLAGS
        -Wl,-allow-multiple-definition
        )



