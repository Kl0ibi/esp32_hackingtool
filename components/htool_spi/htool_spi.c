/*
Copyright (c) 2022 kl0ibi

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
 */
#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// region global pin definitions
//display uses standard SPI pins
uint8_t gpio_miso = 27;
uint8_t gpio_mosi = 26;
uint8_t gpio_sck = 25;
uint8_t gpio_ss = 33;
// endregion

void htool_spi_master_ss(uint8_t level) {
    gpio_set_level(gpio_ss, level);
}

void htool_spi_master_write(uint8_t data) {
    gpio_set_level(gpio_sck, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    //printf("write:\n"); //DEBUG
    for (uint8_t i = 0; i < 8; i++) {
        gpio_set_level(gpio_mosi, (data >> (i)) & 0x01);
        //printf("%d", (data >> i) & 0x01); //DEBUG
        gpio_set_level(gpio_sck, 0);
        gpio_set_level(gpio_sck, 1);
    }
    //printf("\n"); //DEBUG
}

uint8_t htool_spi_master_read() {
    int8_t byte = 0x00;

    for (uint8_t i = 0; i < 8; i++) {
        byte |= (gpio_get_level(gpio_miso) << i); // read bit
        gpio_set_level(gpio_sck, 0); // clock high
        gpio_set_level(gpio_sck, 1); // clock low
    }
    //printf("read: %d", byte); //DEBUG

    return byte;
}

void htool_spi_master_init(uint8_t miso, uint8_t mosi, uint8_t sck, uint8_t ss) {
    gpio_pad_select_gpio(miso);
    gpio_pad_select_gpio(mosi);
    gpio_pad_select_gpio(sck);
    gpio_pad_select_gpio(ss);

    gpio_set_direction(miso, GPIO_MODE_INPUT);
    gpio_set_direction(mosi, GPIO_MODE_OUTPUT);
    gpio_set_direction(sck, GPIO_MODE_OUTPUT);
    gpio_set_direction(ss, GPIO_MODE_OUTPUT);

    gpio_set_level(ss, 1); // Deselect

    gpio_miso = miso;
    gpio_mosi = mosi;
    gpio_sck = sck;
    gpio_ss = ss;
}