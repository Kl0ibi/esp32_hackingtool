/*

MIT License

Copyright (c) 2017-2018 Espressif Systems (Shanghai) PTE LTD
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

This code is based on Espressif provided SPI Master example which was
released to Public Domain: https://goo.gl/ksC2Ln

This file is part of the MIPI DCS Display Driver:
https://github.com/tuupola/esp_mipi

SPDX-License-Identifier: MIT

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <driver/spi_master.h>
#include <driver/ledc.h>
#include <soc/gpio_struct.h>
#include <driver/gpio.h>
#include <esp_log.h>

#include "sdkconfig.h"
#include "mipi_dcs.h"
#include "mipi_display.h"

static const char *TAG = "mipi_display";
static SemaphoreHandle_t mutex;

static void mipi_display_write_command(spi_device_handle_t spi, const uint8_t command)
{
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));

     /* Length in bits. */
    transaction.length = 1 * 8;
    transaction.tx_buffer = &command;

    ESP_LOGD(TAG, "Sending command 0x%02x", (uint8_t)command);

    /* Set DC low to denote a command. */
    gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_DC, 0);
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &transaction));
}

static void mipi_display_write_data(spi_device_handle_t spi, const uint8_t *data, size_t length)
{
    if (0 == length) {
        return;
    };

    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));

     /* Length in bits. */
    transaction.length = length * 8;
    transaction.tx_buffer = data;

    /* Set DC high to denote data. */
    gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_DC, 1);

    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &transaction));
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, length, ESP_LOG_DEBUG);
}

static void mipi_display_read_data(spi_device_handle_t spi, uint8_t *data, size_t length)
{
    if (0 == length) {
        return;
    };

    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));

     /* Length in bits */
    transaction.length = length * 8;
    transaction.rxlength = length * 8;
    transaction.rx_buffer = data;
    //transaction.flags = SPI_TRANS_USE_RXDATA;

    /* Set DC high to denote data. */
    gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_DC, 1);

    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &transaction));
}


static void mipi_display_set_address(spi_device_handle_t spi, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    uint8_t data[4];
    static uint16_t prev_x1, prev_x2, prev_y1, prev_y2;

    x1 = x1 + CONFIG_MIPI_DISPLAY_OFFSET_X;
    y1 = y1 + CONFIG_MIPI_DISPLAY_OFFSET_Y;
    x2 = x2 + CONFIG_MIPI_DISPLAY_OFFSET_X;
    y2 = y2 + CONFIG_MIPI_DISPLAY_OFFSET_Y;

    /* Change column address only if it has changed. */
    if ((prev_x1 != x1 || prev_x2 != x2)) {
        mipi_display_write_command(spi, MIPI_DCS_SET_COLUMN_ADDRESS);
        data[0] = x1 >> 8;
        data[1] = x1 & 0xff;
        data[2] = x2 >> 8;
        data[3] = x2 & 0xff;
        mipi_display_write_data(spi, data, 4);

        prev_x1 = x1;
        prev_x2 = x2;
    }

    /* Change page address only if it has changed. */
    if ((prev_y1 != y1 || prev_y2 != y2)) {
        mipi_display_write_command(spi, MIPI_DCS_SET_PAGE_ADDRESS);
        data[0] = y1 >> 8;
        data[1] = y1 & 0xff;
        data[2] = y2 >> 8;
        data[3] = y2 & 0xff;
        mipi_display_write_data(spi, data, 4);

        prev_y1 = y1;
        prev_y2 = y2;
    }

    mipi_display_write_command(spi, MIPI_DCS_WRITE_MEMORY_START);
}

size_t mipi_display_write(spi_device_handle_t spi, uint16_t x1, uint16_t y1, uint16_t w, uint16_t h, uint8_t *buffer)
{
    if (0 == w || 0 == h) {
        return 0;
    }

    const int32_t x2 = x1 + w - 1;
    const int32_t y2 = y1 + h - 1;
    const size_t size = w * h * DISPLAY_DEPTH / 8;

    xSemaphoreTake(mutex, portMAX_DELAY);

    mipi_display_set_address(spi, x1, y1, x2, y2);
    mipi_display_write_data(spi, buffer, size);

    xSemaphoreGive(mutex);

    return size;
}

static void mipi_display_spi_master_init(spi_device_handle_t *spi)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_MIPI_DISPLAY_PIN_MISO,
        .mosi_io_num = CONFIG_MIPI_DISPLAY_PIN_MOSI,
        .sclk_io_num = CONFIG_MIPI_DISPLAY_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Max transfer size in bytes. */
        .max_transfer_sz = SPI_MAX_TRANSFER_SIZE
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_MIPI_DISPLAY_SPI_CLOCK_SPEED_HZ,
        .mode = CONFIG_MIPI_DISPLAY_SPI_MODE,
        .spics_io_num = CONFIG_MIPI_DISPLAY_PIN_CS,
        .queue_size = 64,
        .flags = SPI_DEVICE_NO_DUMMY
    };

    /* ESP32S2 requires DMA channel to match the SPI host. */
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_MIPI_DISPLAY_SPI_HOST, &buscfg, CONFIG_MIPI_DISPLAY_SPI_HOST));
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_MIPI_DISPLAY_SPI_HOST, &devcfg, spi));
}

void mipi_display_init(spi_device_handle_t *spi)
{
    mutex = xSemaphoreCreateMutex();

    if (CONFIG_MIPI_DISPLAY_PIN_CS > 0) {
        gpio_pad_select_gpio(CONFIG_MIPI_DISPLAY_PIN_CS);
        gpio_set_direction(CONFIG_MIPI_DISPLAY_PIN_CS, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_CS, 0);
    };

    gpio_pad_select_gpio(CONFIG_MIPI_DISPLAY_PIN_DC);
    gpio_set_direction(CONFIG_MIPI_DISPLAY_PIN_DC, GPIO_MODE_OUTPUT);

    mipi_display_spi_master_init(spi);
    vTaskDelay(100 / portTICK_RATE_MS);

    /* Reset the display. */
    if (CONFIG_MIPI_DISPLAY_PIN_RST > 0) {
        gpio_pad_select_gpio(CONFIG_MIPI_DISPLAY_PIN_RST);
        gpio_set_direction(CONFIG_MIPI_DISPLAY_PIN_RST, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_RST, 0);
        vTaskDelay(100 / portTICK_RATE_MS);
        gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_RST, 1);
        vTaskDelay(100 / portTICK_RATE_MS);
    }

    /* Send minimal init commands. */
    mipi_display_write_command(*spi, MIPI_DCS_SOFT_RESET);
    vTaskDelay(200 / portTICK_RATE_MS);

    mipi_display_write_command(*spi, MIPI_DCS_SET_ADDRESS_MODE);
    mipi_display_write_data(*spi, &(uint8_t){MIPI_DISPLAY_ADDRESS_MODE}, 1);

    mipi_display_write_command(*spi, MIPI_DCS_SET_PIXEL_FORMAT);
    mipi_display_write_data(*spi, &(uint8_t){CONFIG_MIPI_DISPLAY_PIXEL_FORMAT}, 1);

#ifdef CONFIG_MIPI_DISPLAY_INVERT
    mipi_display_write_command(*spi, MIPI_DCS_ENTER_INVERT_MODE);
#else
    mipi_display_write_command(*spi, MIPI_DCS_EXIT_INVERT_MODE);
#endif

    mipi_display_write_command(*spi, MIPI_DCS_EXIT_SLEEP_MODE);
    vTaskDelay(200 / portTICK_RATE_MS);

    mipi_display_write_command(*spi, MIPI_DCS_SET_DISPLAY_ON);
    vTaskDelay(200 / portTICK_RATE_MS);

    /* Enable backlight. */
    if (CONFIG_MIPI_DISPLAY_PIN_BL > 0) {
        gpio_set_direction(CONFIG_MIPI_DISPLAY_PIN_BL, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_MIPI_DISPLAY_PIN_BL, 1);

        /* Enable backlight PWM. */
        if (CONFIG_MIPI_DISPLAY_PWM_BL > 0) {
            ESP_LOGI(TAG, "Initializing backlight PWM");
            ledc_timer_config_t timercfg = {
                .duty_resolution = LEDC_TIMER_13_BIT,
                .freq_hz = 9765,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = LEDC_TIMER_0,
                .clk_cfg = LEDC_AUTO_CLK,
            };

            ledc_timer_config(&timercfg);

            ledc_channel_config_t channelcfg = {
                .channel    = LEDC_CHANNEL_0,
                .duty       = CONFIG_MIPI_DISPLAY_PWM_BL,
                .gpio_num   = CONFIG_MIPI_DISPLAY_PIN_BL,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .hpoint     = 0,
                .timer_sel  = LEDC_TIMER_0,
            };

            ledc_channel_config(&channelcfg);
        }

    }

    ESP_LOGI(TAG, "Display initialized.");

    spi_device_acquire_bus(*spi, portMAX_DELAY);
}

void mipi_display_ioctl(spi_device_handle_t spi, const uint8_t command, uint8_t *data, size_t size)
{
    xSemaphoreTake(mutex, portMAX_DELAY);

    switch (command) {
        case MIPI_DCS_GET_COMPRESSION_MODE:
        case MIPI_DCS_GET_DISPLAY_ID:
        case MIPI_DCS_GET_RED_CHANNEL:
        case MIPI_DCS_GET_GREEN_CHANNEL:
        case MIPI_DCS_GET_BLUE_CHANNEL:
        case MIPI_DCS_GET_DISPLAY_STATUS:
        case MIPI_DCS_GET_POWER_MODE:
        case MIPI_DCS_GET_ADDRESS_MODE:
        case MIPI_DCS_GET_PIXEL_FORMAT:
        case MIPI_DCS_GET_DISPLAY_MODE:
        case MIPI_DCS_GET_SIGNAL_MODE:
        case MIPI_DCS_GET_DIAGNOSTIC_RESULT:
        case MIPI_DCS_GET_SCANLINE:
        case MIPI_DCS_GET_DISPLAY_BRIGHTNESS:
        case MIPI_DCS_GET_CONTROL_DISPLAY:
        case MIPI_DCS_GET_POWER_SAVE:
        case MIPI_DCS_READ_DDB_START:
        case MIPI_DCS_READ_DDB_CONTINUE:
            mipi_display_write_command(spi, command);
            mipi_display_read_data(spi, data, size);
            break;
        default:
            mipi_display_write_command(spi, command);
            mipi_display_write_data(spi, data, size);
    }

    xSemaphoreGive(mutex);
}

void mipi_display_close(spi_device_handle_t spi)
{
    spi_device_release_bus(spi);
}