idf_component_register(
    SRCS "src/hagl_hal_single.c" "src/hagl_hal_double.c" "src/hagl_hal_triple.c" "src/mipi_display.c"
    INCLUDE_DIRS "./include"
    REQUIRES hagl
)