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

-cut-

This is the HAL used when triple buffering is enabled. The GRAM of the
display driver chip is the framebuffer. The two memory blocks allocated
by this HAL are the two back buffer. Total three buffers.

Note that all coordinates are already clipped in the main library itself.
HAL does not need to validate the coordinates, they can alway be assumed
valid.

*/

#include "sdkconfig.h"
#include "hagl_hal.h"

#ifdef CONFIG_HAGL_HAL_USE_TRIPLE_BUFFERING

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <mipi_display.h>
#include <hagl/bitmap.h>
#include <hagl.h>


static uint8_t *buffer1;
static uint8_t *buffer2;

static hagl_bitmap_t bb;

static spi_device_handle_t spi;
static const char *TAG = "hagl_esp_mipi";

static size_t
flush(void *self)
{
    uint8_t *buffer = bb.buffer;
    if (bb.buffer == buffer1) {
        bb.buffer = buffer2;
    } else {
        bb.buffer = buffer1;
    }
    return mipi_display_write(spi, 0, 0, bb.width, bb.height, (uint8_t *) buffer);
}

static void
put_pixel(void *self, int16_t x0, int16_t y0, color_t color)
{
    bb.put_pixel(&bb, x0, y0, color);
}

static color_t
get_pixel(void *self, int16_t x0, int16_t y0)
{
    return bb.get_pixel(&bb, x0, y0);
}

static void
blit(void *self, int16_t x0, int16_t y0, hagl_bitmap_t *src)
{
    bb.blit(&bb, x0, y0, src);
}

static void
scale_blit(void *self, uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, hagl_bitmap_t *src)
{
    bb.scale_blit(&bb, x0, y0, w, h, src);
}

static void
hline(void *self, int16_t x0, int16_t y0, uint16_t width, color_t color)
{
    bb.hline(&bb, x0, y0, width, color);
}


static void
vline(void *self, int16_t x0, int16_t y0, uint16_t height, color_t color)
{
    bb.vline(&bb, x0, y0, height, color);
}

// void hagl_hal_clear_screen()
// {
//     color_t *ptr1 = (color_t *) buffer1;
//     color_t *ptr2 = (color_t *) buffer2;
//     size_t count = DISPLAY_WIDTH * DISPLAY_HEIGHT;

//     while (--count) {
//         *ptr1++ = 0x0000;
//         *ptr2++ = 0x0000;
//     }
// }

void
hagl_hal_init(hagl_backend_t *backend)
{
    mipi_display_init(&spi);

    ESP_LOGI(TAG, "Triple buffering mode");
    ESP_LOGI(
        TAG, "Largest free (MALLOC_CAP_DMA | MALLOC_CAP_32BIT) block: %d",
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_32BIT)
    );
    ESP_LOGI(
        TAG, "Largest free (MALLOC_CAP_DMA) block: %d",
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA)
    );
    // heap_caps_print_heap_info(MALLOC_CAP_DMA | MALLOC_CAP_32BIT);

    buffer1 = (uint8_t *) heap_caps_malloc(
        BITMAP_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DEPTH),
        MALLOC_CAP_DMA | MALLOC_CAP_32BIT
    );
    if (NULL == buffer1) {
        ESP_LOGE(TAG, "Failed to alloc buffer 1.");
    } else {
        ESP_LOGI(TAG, "Buffer 1 at: %p", buffer1);
        memset(buffer1, 0x00, BITMAP_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DEPTH));
    };

    ESP_LOGI(
        TAG, "Largest free (MALLOC_CAP_DMA | MALLOC_CAP_32BIT) block: %d",
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_32BIT)
    );
    ESP_LOGI(
        TAG, "Largest free (MALLOC_CAP_DMA) block: %d",
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA)
    );
    //heap_caps_print_heap_info(MALLOC_CAP_DMA | MALLOC_CAP_32BIT);

    buffer2 = (uint8_t *) heap_caps_malloc(
        BITMAP_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DEPTH),
        MALLOC_CAP_DMA | MALLOC_CAP_32BIT
    );

    if (NULL == buffer2) {
        ESP_LOGE(TAG, "Failed to alloc buffer 2.");
    } else {
        ESP_LOGI(TAG, "Buffer 2 at: %p", buffer2);
        memset(buffer2, 0x00, BITMAP_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DEPTH));
    };

    /* Clear both and leave pointer to buffer1. */
    memset(&bb, 0, sizeof(hagl_bitmap_t));
    bb.width = DISPLAY_WIDTH;
    bb.height = DISPLAY_HEIGHT;
    bb.depth = DISPLAY_DEPTH;

    bitmap_init(&bb, buffer2);
    bitmap_init(&bb, buffer1);
    backend->buffer = buffer1;

    backend->width = MIPI_DISPLAY_WIDTH;
    backend->height = MIPI_DISPLAY_HEIGHT;
    backend->depth = MIPI_DISPLAY_DEPTH;
    backend->put_pixel = put_pixel;
    backend->get_pixel = get_pixel;
    backend->hline = hline;
    backend->vline = vline;
    backend->blit = blit;
    backend->scale_blit = scale_blit;

    backend->flush = flush;
}



#endif /* #ifdef CONFIG_HAGL_HAL_USE_TRIPLE_BUFFERING */