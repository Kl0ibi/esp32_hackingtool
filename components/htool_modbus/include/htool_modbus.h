/*
Copyright (c) 2023 kl0ibi

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
#include <stdbool.h>

#define MODBUS_OK (1)
#define MODBUS_ERR (-1)

int32_t modbus_tcp_client_read_str(int32_t sock, uint8_t unit_id, uint16_t start_reg, uint8_t num_regs, bool reversed, char **data, uint8_t *len);

int32_t modbus_tcp_client_read(int32_t sock, uint8_t unit_id, uint16_t start_reg, uint8_t num_regs, bool big_endian, uint8_t *data);

int32_t modbus_tcp_client_read_raw(int32_t sock, uint8_t unit_id, uint16_t start_reg, uint8_t num_regs, uint8_t *data);

int32_t modbus_tcp_client_connect(const char *host, const uint16_t *port);

int32_t modbus_tcp_client_disconnect(int32_t sock);
