# MIPI DCS HAL for HAGL Graphics Library

HAL for HAGL graphics library for display drivers supporting the [MIPI Display Command Set](https://www.mipi.org/specifications/display-command-set). This covers most displays currently used by hobbyists. Tested with ST7735S, ST7789V, ST7789V2, ILI9341, ILI9342C and ILI9163. Works with both ESP32 and ESP32-S2.

[![Software License](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat-square)](LICENSE.md)

## Usage

To use with an ESP-IDF project you include this HAL and the [HAGL graphics library](https://github.com/tuupola/hagl) itself.  If you are using CMake based build the HAL **must** be in folder named `hagl_hal`.

```
$ cd components
$ git submodule add git@github.com:tuupola/hagl_esp_mipi.git hagl_hal
$ git submodule add git@github.com:tuupola/hagl.git
```

You can alter display behaviour via `menuconfig`. If you choose to use back buffer all drawing operations will be fast. Downside is that back buffer requires lot of memory. To reduce flickering you can also choose to lock back buffer while flushing. Locking will slow down draw operations though.

```
$ idf.py menuconfig
```

You can also use the older GNU Make based build system.

```
$ make menuconfig
```

[Default configs](https://github.com/tuupola/hagl_esp_mipi/tree/master/sdkconfig/) are provided for popular dev boards. For example to compile for M5Stack do something like the following:

```
$ cp components/hagl_hal/sdkconfig/m5stack.defaults sdkconfig.defaults
$ idf.py menuconfig
```

For example usage see [ESP GFX](https://github.com/tuupola/esp_gfx), [ESP effects](https://github.com/tuupola/esp_effects) and [Mandelbrot](https://github.com/tuupola/esp-examples/tree/master/014-mandelbrot).


## Speed

 First table numbers are operations per second with double buffering. Bigger number is better. T-Display and M5StickC have higher numbers because they have smaller resolution. Smaller resolution means less bytes to push to the display.

|                               | T4     | T-Display | M5Stack | M5StickC |
|-------------------------------|--------|-----------|---------|----------|
| hagl_put_pixel()              | 304400 |    304585 |  340850 |   317094 |
| hagl_draw_line()              |  10485 |     14942 |   12145 |    31293 |
| hagl_draw_circle()            |  15784 |     16430 |   17730 |    18928 |
| hagl_fill_circle()            |   8712 |      9344 |    9982 |    13910 |
| hagl_draw_ellipse()           |   8187 |      8642 |    9168 |    10019 |
| hagl_fill_ellipse()           |   3132 |      3457 |    3605 |     5590 |
| hagl_draw_triangle()          |   3581 |      5137 |    4160 |    11186 |
| hagl_fill_triangle()          |   1246 |      1993 |    1654 |     6119 |
| hagl_draw_rectangle()         |  22759 |     30174 |   26910 |    64259 |
| hagl_fill_rectangle()         |   2191 |      4849 |    2487 |    16146 |
| hagl_draw_rounded_rectangle() |  17660 |     21993 |   20736 |    39102 |
| hagl_fill_rounded_rectangle() |   2059 |      4446 |    2313 |    13270 |
| hagl_draw_polygon()           |   2155 |      3096 |    2494 |     6763 |
| hagl_fill_polygon()           |    692 |      1081 |     938 |     3295 |
| hagl_put_char()               |  29457 |     29131 |   32429 |    27569 |
| hagl_flush()                  |     32 |        76 |      32 |       96 |

Second table numbers are operations per second with single buffering ie. writing directly to the display controller memory.

|                               | T4    | T-Display | M5Stack | M5StickC |
|-------------------------------|-------|-----------|---------|----------|
| hagl_put_pixel()              | 16041 |     15252 |   16044 |    24067 |
| hagl_draw_line()              |   113 |       172 |     112 |      289 |
| hagl_draw_circle()            |   148 |       173 |     145 |      230 |
| hagl_fill_circle()            |   264 |       278 |     261 |      341 |
| hagl_draw_ellipse()           |    84 |       103 |      85 |      179 |
| hagl_fill_ellipse()           |   114 |       128 |     116 |      191 |
| hagl_draw_triangle()          |    37 |        54 |      37 |      114 |
| hagl_fill_triangle()          |    72 |       111 |      72 |      371 |
| hagl_draw_rectangle()         |  2378 |      2481 |    2374 |     3482 |
| hagl_fill_rectangle()         |    91 |       146 |      91 |      454 |
| hagl_draw_rounded_rectangle() |   458 |       535 |     459 |      808 |
| hagl_fill_rounded_rectangle() |    87 |       139 |      79 |      400 |
| hagl_draw_polygon()           |    21 |        33 |      19 |       71 |
| hagl_fill_polygon()           |    43 |        66 |      49 |      228 |
| hagl_put_char)                |  4957 |      4264 |    4440 |     2474 |
| hagl_flush()                  |     x |         x |       x |        x |

You can run the speed tests yourself by checking out the [speedtest repository](https://github.com/tuupola/esp_gfx).


## License

The MIT License (MIT). Please see [License File](LICENSE) for more information.
