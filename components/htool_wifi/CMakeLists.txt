idf_component_register(SRCS "htool_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES htool_api nvs_flash htool_display
        EMBED_FILES captive_portals/google/google.html captive_portals/mcdonalds/mcdonalds.html captive_portals/router/router.html captive_portals/router/1_1.html captive_portals/router/a1.html captive_portals/router/asus.html captive_portals/router/att.html
        captive_portals/router/fritzbox.html captive_portals/router/globe.html captive_portals/router/huawei.html captive_portals/router/magenta.html captive_portals/router/netgear.html captive_portals/router/o2.html captive_portals/router/pldt.html
        captive_portals/router/swisscom.html captive_portals/router/tplink.html captive_portals/router/verizon.html captive_portals/router/vodafone.html captive_portals/facebook/facebook.html captive_portals/apple/apple.html
        )
set_source_files_properties(htool_wifi.c
        PROPERTIES COMPILE_FLAGS
        -Wl,-allow-multiple-definition
        )
