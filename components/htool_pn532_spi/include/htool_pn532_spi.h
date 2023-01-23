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

typedef enum {
    PN532_NORMAL = 0x01,
    PN532_VIRTUAL_CARD = 0x02,
    PN532_WIRED_CARD = 0x03,
    PN532_DUAL_CARD = 0x04,
} pn532_sam_config_type_t;

typedef enum {
    PN532_BAUD_TYPE_ISO14443A = 0x00,
    PN532_BAUD_TYPE_FELICA_212 = 0x01,
    PN532_BAUD_TYPE_FELICA_424 = 0x02,
    PN532_BAUD_TYPE_ISO14443B = 0x03,
    PN532_BAUD_TYPE_JEWEL = 0x04,
} pn532_baud_type_t;

void htool_pn532_spi_start();

void htool_pn532_spi_init(uint8_t miso, uint8_t mosi, uint8_t sck, uint8_t ss);
