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

#ifndef _I2C_HELPER_H
#define _I2C_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <driver/i2c.h>

#include "sdkconfig.h"

#define I2C_HELPER_MASTER_RX_BUF_LEN     (0)
#define I2C_HELPER_MASTER_TX_BUF_LEN     (0)

#define I2C_HELPER_MASTER_0_SDA          (CONFIG_I2C_HELPER_MASTER_0_SDA)
#define I2C_HELPER_MASTER_0_SCL          (CONFIG_I2C_HELPER_MASTER_0_SCL)
#define I2C_HELPER_MASTER_0_FREQ_HZ      (CONFIG_I2C_HELPER_MASTER_0_FREQ_HZ)

#define I2C_HELPER_MASTER_1_SDA          (CONFIG_I2C_HELPER_MASTER_1_SDA)
#define I2C_HELPER_MASTER_1_SCL          (CONFIG_I2C_HELPER_MASTER_1_SCL)
#define I2C_HELPER_MASTER_1_FREQ_HZ      (CONFIG_I2C_HELPER_MASTER_1_FREQ_HZ)

int32_t i2c_init(i2c_port_t port);
int32_t i2c_read(void *port, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size);
int32_t i2c_write(void *port, uint8_t address, uint8_t reg, const uint8_t *buffer, uint16_t size);
int32_t i2c_close(i2c_port_t port);

#ifdef __cplusplus
}
#endif
#endif
