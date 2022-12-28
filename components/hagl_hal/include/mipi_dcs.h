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

This file is part of the MIPI DCS Display Driver:
https://github.com/tuupola/esp_mipi

SPDX-License-Identifier: MIT

*/

#ifndef _MIPI_DCS_H
#define _MIPI_DCS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MIPI_DCS_NOP                        0x00
#define MIPI_DCS_SOFT_RESET                 0x01
#define MIPI_DCS_GET_COMPRESSION_MODE       0x03
#define MIPI_DCS_GET_DISPLAY_ID             0x04 /* Not in spec? */
#define MIPI_DCS_GET_RED_CHANNEL            0x06
#define MIPI_DCS_GET_GREEN_CHANNEL          0x07
#define MIPI_DCS_GET_BLUE_CHANNEL           0x08
#define MIPI_DCS_GET_DISPLAY_STATUS         0x09 /* Not in spec? */
#define MIPI_DCS_GET_POWER_MODE             0x0A
#define MIPI_DCS_GET_ADDRESS_MODE           0x0B
#define MIPI_DCS_GET_PIXEL_FORMAT           0x0C
#define MIPI_DCS_GET_DISPLAY_MODE           0x0D
#define MIPI_DCS_GET_SIGNAL_MODE            0x0E
#define MIPI_DCS_GET_DIAGNOSTIC_RESULT      0x0F
#define MIPI_DCS_ENTER_SLEEP_MODE           0x10
#define MIPI_DCS_EXIT_SLEEP_MODE            0x11
#define MIPI_DCS_ENTER_PARTIAL_MODE         0x12
#define MIPI_DCS_ENTER_NORMAL_MODE          0x13
#define MIPI_DCS_EXIT_INVERT_MODE           0x20
#define MIPI_DCS_ENTER_INVERT_MODE          0x21
#define MIPI_DCS_SET_GAMMA_CURVE            0x26
#define MIPI_DCS_SET_DISPLAY_OFF            0x28
#define MIPI_DCS_SET_DISPLAY_ON             0x29
#define MIPI_DCS_SET_COLUMN_ADDRESS         0x2A
#define MIPI_DCS_SET_PAGE_ADDRESS           0x2B
#define MIPI_DCS_WRITE_MEMORY_START         0x2C
#define MIPI_DCS_WRITE_LUT                  0x2D
#define MIPI_DCS_READ_MEMORY_START          0x2E
#define MIPI_DCS_SET_PARTIAL_ROWS           0x30
#define MIPI_DCS_SET_PARTIAL_COLUMNS        0x31
#define MIPI_DCS_SET_SCROLL_AREA            0x33
#define MIPI_DCS_SET_TEAR_OFF               0x34
#define MIPI_DCS_SET_TEAR_ON                0x35
#define MIPI_DCS_SET_ADDRESS_MODE           0x36
#define MIPI_DCS_SET_SCROLL_START           0x37
#define MIPI_DCS_EXIT_IDLE_MODE             0x38
#define MIPI_DCS_ENTER_IDLE_MODE            0x39
#define MIPI_DCS_SET_PIXEL_FORMAT           0x3A
#define MIPI_DCS_WRITE_MEMORY_CONTINUE      0x3C
#define MIPI_DCS_SET_3D_CONTROL             0x3D
#define MIPI_DCS_READ_MEMORY_CONTINUE       0x3E
#define MIPI_DCS_GET_3D_CONTROL             0x3F
#define MIPI_DCS_SET_VSYNC_TIMING           0x40
#define MIPI_DCS_SET_TEAR_SCANLINE          0x44
#define MIPI_DCS_GET_SCANLINE               0x45
#define MIPI_DCS_SET_DISPLAY_BRIGHTNESS     0x51
#define MIPI_DCS_GET_DISPLAY_BRIGHTNESS     0x52
#define MIPI_DCS_WRITE_CONTROL_DISPLAY      0x53
#define MIPI_DCS_GET_CONTROL_DISPLAY        0x54
#define MIPI_DCS_WRITE_POWER_SAVE           0x55
#define MIPI_DCS_GET_POWER_SAVE             0x56
#define MIPI_DCS_SET_CABC_MIN_BRIGHTNESS    0x5E
#define MIPI_DCS_GET_CABC_MIN_BRIGHTNESS    0x5F
#define MIPI_DCS_READ_DDB_START             0xA1
#define MIPI_DCS_READ_DDB_CONTINUE          0xA8

#define MIPI_DCS_PIXEL_FORMAT_24BIT         0x77 /* 0b01110111 */
#define MIPI_DCS_PIXEL_FORMAT_18BIT         0x66 /* 0b01100110 */
#define MIPI_DCS_PIXEL_FORMAT_16BIT         0x55 /* 0b01010101 */
#define MIPI_DCS_PIXEL_FORMAT_12BIT         0x33 /* 0b00110011 */
#define MIPI_DCS_PIXEL_FORMAT_8BIT          0x22 /* 0b00100010 */
#define MIPI_DCS_PIXEL_FORMAT_3BIT          0x11 /* 0b00010001 */

#define MIPI_DCS_ADDRESS_MODE_MIRROR_Y      0x80
#define MIPI_DCS_ADDRESS_MODE_MIRROR_X      0x40
#define MIPI_DCS_ADDRESS_MODE_SWAP_XY       0x20
#define MIPI_DCS_ADDRESS_MODE_REFRESH_BT    0x10 /* Does not affect image */
#define MIPI_DCS_ADDRESS_MODE_BGR           0x08
#define MIPI_DCS_ADDRESS_MODE_RGB           0x00
#define MIPI_DCS_ADDRESS_MODE_LATCH_RL      0x04 /* Does not affect image */
#define MIPI_DCS_ADDRESS_MODE_FLIP_X        0x02
#define MIPI_DCS_ADDRESS_MODE_FLIP_Y        0x01

#ifdef __cplusplus
}
#endif
#endif /* _MIPI_DCS_H */
