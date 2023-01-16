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
//datasheet: https://www.nxp.com/docs/en/user-guide/141520.pdf
#include <stdio.h>
#include <esp_log.h>
#include "htool_pn532_spi.h"
#include "htool_spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

char *TAG = "HTOOL_PN532_SPI";

void pn532_read_data(uint8_t *data, uint8_t len) {
    htool_spi_master_ss(0);

    vTaskDelay(pdMS_TO_TICKS(10));
    htool_spi_master_write(0x03); // read data command
    for (uint8_t i = 0; i < len; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        data[i] = htool_spi_master_read();
    }

    htool_spi_master_ss(1);
}

bool pn532_ack_check() {
    uint8_t ack[6] = {0};

    pn532_read_data(ack, sizeof(ack));

    if (ack[0] == 0x00 && ack[1] == 0x00 && ack[2] == 0xFF && ack[3] == 0x00 && ack[4] == 0xFF && ack[5] == 0x00) {
        return true;
    }

    return false;
}

void pn532_write_data(uint8_t *data, uint8_t len) {
    uint8_t checksum = 0x00; // only 8bit /-> resets after 255

    htool_spi_master_ss(0); // select

    vTaskDelay(pdMS_TO_TICKS(10));

    htool_spi_master_write(0x01); // write command
    htool_spi_master_write(0x00); // preamble
    htool_spi_master_write(0x00); // start code 0x00
    htool_spi_master_write(0xFF); // start code 0x01
    htool_spi_master_write(len + 1); // len
    htool_spi_master_write(~(len +1) + 1); // len checksum (LCS)
    htool_spi_master_write(0xD4); // TFI (host to pn532)
    checksum += 0xD4 - 0x01; //only 8bit
    for (uint8_t i = 0; i < len; i++) {
        htool_spi_master_write(data[i]);
        checksum += data[i];
    }
    htool_spi_master_write(~checksum); // DCS
    htool_spi_master_write(0x00); // postamble

    htool_spi_master_ss(1); // deselect
}

bool pn532_is_ready() {
    uint8_t data = 0x00;
    htool_spi_master_ss(0);

    vTaskDelay(pdMS_TO_TICKS(10));

    htool_spi_master_write(0x02); // read status command
    data = htool_spi_master_read();

    htool_spi_master_ss(1);

    return data == 0x01;
}

bool pn532_wait_ready_timeout(uint16_t timeout) { //Timeout is counter in 100ms steps
    /*uint16_t _timeout = 1000;
    uint16_t timer = 0;
    while (!pn532_is_ready())
    {
        if (_timeout != 0)
        {
            timer += 10;
            if (timer > _timeout)
            {
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;*/

    while (!pn532_is_ready()) {
        if (100 * timeout-- == 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return true;
}

//TODO: add wait funtions beacuse pn532 is hella slow / and of course better return codes one for no ack and one that pn532 is busy
uint8_t pn532_write_data_ack_check(uint8_t *data, uint8_t len, uint8_t timeout) { //with acknwoledgement check so we check that the pn532 has received the data
    pn532_write_data(data, len);
    if (!pn532_wait_ready_timeout(timeout)) {
        ESP_LOGE(TAG, "PN532 is busy or not responding (0)");
        return 0;
    }
    ESP_LOGI(TAG, "PN532 is ready");
    if (!pn532_ack_check()) {
        ESP_LOGE(TAG, "did not receive acknowledgement from PN532");
        return 0;
    }
    if (!pn532_wait_ready_timeout(timeout)) {
        ESP_LOGE(TAG, "PN532 is busy or not responding (1)");
        return 0;
    }
    ESP_LOGI(TAG, "PN532 is ready again");
    return 1;
}

uint16_t pn532_get_firmware_version() {
    // 0x00 preamble, 0xFF startcode, 0x06 len, 0xFA LCS, 0xD5 TFI, 0x03 output from cmd 0x02, 0x32 IC (pn532), 3 bytes ver/rev/support, 0x01 DCS, 0x00 postamble
    uint8_t data[12] = {0x00};
    data[0] = 0x02; // get firmware version command
    if (pn532_write_data_ack_check(data, 1, 10) != 1) {
        ESP_LOGE(TAG, "did not receive acknowledgement from PN532");
        return 0;
    }
    pn532_read_data(data, 12);
    if (data[0] == 0x00 && data[1] == 0xFF && data[2] == 0x06 && data[3] == 0xFA && data[4] == 0xD5 && data[5] == 0x03 && data[6] == 0x32) {
        ESP_LOGD(TAG, "got a valid firmware revision");
        //since we only want the firmware data we have a offset of 7
        return (data[7] | data[8] << 8);
    }
    else  {
        ESP_LOGE(TAG, "got invalid firmware revision %s", data[6] == 0x32 ? "" : "wrong IC (not using pn532)");
        return 0x00;
    }
}

void htool_pn532_spi_start() {
    pn532_wait_ready_timeout(1);
    uint16_t firmware_version;
    if ((firmware_version = pn532_get_firmware_version()) == 0x00) {
        ESP_LOGE(TAG, "could not get firmware version! Did you connect the pn532 correctly?");
        return;
    }
    ESP_LOGI(TAG, "pn532 started: v%d.%d", firmware_version & 0xFF, (firmware_version >> 8) & 0xFF);
}

void htool_pn532_spi_init(uint8_t miso, uint8_t mosi, uint8_t sck, uint8_t ss) {
    htool_spi_master_init(miso, mosi, sck, ss);
}
