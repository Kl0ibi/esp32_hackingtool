/*

MIT License

Copyright (c) 2019-2021 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

This file is part of the MIPI DCS HAL for HAGL graphics library:
https://github.com/tuupola/hagl_esp_mipi/

SPDX-License-Identifier: MIT

*/

#ifndef _HAGL_HAL_H
#define _HAGL_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <hagl/backend.h>

#include "sdkconfig.h"

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_24BIT_SELECTED
typedef uint32_t color_t;
#endif

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_18BIT_SELECTED
typedef uint32_t color_t;
#endif

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_16BIT_SELECTED
/* Currently only this, ie. RGB565 is properly tested. */
typedef uint16_t color_t;
#endif

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_12BIT_SELECTED
typedef uint16_t color_t;
#endif

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_8BIT_SELECTED
typedef uint8_t color_t;
#endif

#ifdef CONFIG_MIPI_DCS_PIXEL_FORMAT_3BIT_SELECTED
typedef uint8_t color_t;
#endif


#ifdef CONFIG_HAGL_HAL_USE_DOUBLE_BUFFERING
#define HAGL_HAS_HAL_BACK_BUFFER
#endif

#ifdef CONFIG_HAGL_HAL_USE_TRIPLE_BUFFERING
#define HAGL_HAS_HAL_BACK_BUFFER
#endif

#ifdef CONFIG_HAGL_HAL_NO_BUFFERING
#undef HAGL_HAS_HAL_BACK_BUFFER
#endif

#define DISPLAY_WIDTH       (CONFIG_MIPI_DISPLAY_WIDTH)
#define DISPLAY_HEIGHT      (CONFIG_MIPI_DISPLAY_HEIGHT)
#define DISPLAY_DEPTH       (CONFIG_MIPI_DISPLAY_DEPTH)
#define MIPI_DISPLAY_WIDTH  (CONFIG_MIPI_DISPLAY_WIDTH)
#define MIPI_DISPLAY_HEIGHT (CONFIG_MIPI_DISPLAY_HEIGHT)
#define MIPI_DISPLAY_DEPTH  (CONFIG_MIPI_DISPLAY_DEPTH)

/**
 * Initialize the HAL
 */
void hagl_hal_init(hagl_backend_t *backend);

#ifdef __cplusplus
}
#endif
#endif /* _HAGL_HAL_H */