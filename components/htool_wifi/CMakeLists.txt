idf_component_register(SRCS "htool_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_api nvs_flash htool_display
        EMBED_FILES captive_portals/google/google.html captive_portals/mcdonalds/mcdonalds.html captive_portals/router/router.html
        )
set_source_files_properties(htool_wifi.c
        PROPERTIES COMPILE_FLAGS
        -Wl,-allow-multiple-definition
        )
