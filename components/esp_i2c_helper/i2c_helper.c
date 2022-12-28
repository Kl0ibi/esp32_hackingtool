/*

SPDX-License-Identifier: MIT

MIT License

Copyright (c) 2019-2020 Mika Tuupola

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

#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include <string.h>

#include "i2c_helper.h"

static const char* TAG = "i2c_helper";
static const uint8_t ACK_CHECK_EN = 1;

int32_t i2c_init(i2c_port_t port) {
    ESP_LOGI(TAG, "Starting I2C master at port %d.", port);

    i2c_config_t conf;
    memset(&conf, 0, sizeof(i2c_config_t));

    conf.mode = I2C_MODE_MASTER;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;

    if (I2C_NUM_0 == port) {
        conf.sda_io_num = I2C_HELPER_MASTER_0_SDA;
        conf.scl_io_num = I2C_HELPER_MASTER_0_SCL;
        conf.master.clk_speed = I2C_HELPER_MASTER_0_FREQ_HZ;
    } else {
        conf.sda_io_num = I2C_HELPER_MASTER_1_SDA;
        conf.scl_io_num = I2C_HELPER_MASTER_1_SCL;
        conf.master.clk_speed = I2C_HELPER_MASTER_1_FREQ_HZ;
    }

    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(
        i2c_driver_install(
            port,
            conf.mode,
            I2C_HELPER_MASTER_RX_BUF_LEN,
            I2C_HELPER_MASTER_TX_BUF_LEN,
            0
        )
    );

    return ESP_OK;
}

int32_t i2c_read(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t length) {

    esp_err_t result;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_port_t port = *(i2c_port_t *)handle;

    if (reg) {
        /* When reading specific register set the address pointer first. */
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write(cmd, &reg, 1, ACK_CHECK_EN);
        ESP_LOGD(TAG, "Reading address 0x%02x register 0x%02x port %d", address, reg, port);
    } else {
        ESP_LOGD(TAG, "Reading address 0x%02x port %d", address, port);
    }

    /* Read length bytes from the current pointer. */
    i2c_master_start(cmd);
    i2c_master_write_byte(
        cmd,
        (address << 1) | I2C_MASTER_READ,
        ACK_CHECK_EN
    );
    if (length > 1) {
        i2c_master_read(cmd, buffer, length - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, buffer + length - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    result = i2c_master_cmd_begin(
        port,
        cmd,
        1000 / portTICK_RATE_MS
    );
    i2c_cmd_link_delete(cmd);

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, length, ESP_LOG_DEBUG);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);

    return result;
}

int32_t i2c_write(void *handle, uint8_t address, uint8_t reg, const uint8_t *buffer, uint16_t size)
{
    esp_err_t result;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_port_t port = *(i2c_port_t *)handle;

    ESP_LOGD(TAG, "Writing address 0x%02x register 0x%02x port %d", address, reg, port);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, size, ESP_LOG_DEBUG);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_write(cmd, (uint8_t *)buffer, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    result = i2c_master_cmd_begin(
        port,
        cmd,
        1000 / portTICK_RATE_MS
    );
    i2c_cmd_link_delete(cmd);

    ESP_ERROR_CHECK_WITHOUT_ABORT(result);

    return result;
}

int32_t i2c_close(i2c_port_t port) {
    ESP_LOGI(TAG, "Closing I2C master at port %d", port);
    return i2c_driver_delete(port);
}