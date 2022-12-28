/*

MIT License

Copyright (c) 2020 Mika Tuupola

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

This file is part of hardware agnostic I2C driver for AXP202:
https://github.com/tuupola/axp202

SPDX-License-Identifier: MIT

*/

#include <stdint.h>

#include "axp202_config.h"
#include "axp202.h"

static axp202_err_t read_coloumb_counter(const axp202_t *axp, float *buffer);
static axp202_err_t read_battery_power(const axp202_t *axp, float *buffer);
static axp202_err_t read_fuel_gauge(const axp202_t *axp, float *buffer);

static const axp202_init_command_t init_commands[] = {
    {AXP202_LDO24_VOLTAGE, {CONFIG_AXP202_LDO24_VOLTAGE}, 1},
    {AXP202_LDO3_VOLTAGE, {CONFIG_AXP202_LDO3_VOLTAGE}, 1},
    {AXP202_DCDC2_VOLTAGE, {CONFIG_AXP202_DCDC2_VOLTAGE}, 1},
    {AXP202_DCDC3_VOLTAGE, {CONFIG_AXP202_DCDC3_VOLTAGE}, 1},
    {AXP202_DCDC23_LDO234_EXTEN_CONTROL, {CONFIG_AXP202_DCDC23_LDO234_EXTEN_CONTROL}, 1},
    {AXP202_ADC_ENABLE_1, {CONFIG_AXP202_ADC_ENABLE_1}, 1},
    {AXP202_ADC_ENABLE_2, {CONFIG_AXP202_ADC_ENABLE_1}, 2},
    {AXP202_CHARGE_CONTROL_1, {CONFIG_AXP202_CHARGE_CONTROL_1}, 1},
    /* End of commands. */
    {0, {0}, 0xff},
};

axp202_err_t axp202_init(const axp202_t *axp)
{
    uint8_t cmd = 0;
    axp202_err_t status;

    /* Send all the commands. */
    while (init_commands[cmd].count != 0xff) {
        status = axp->write(
            axp->handle,
            AXP202_ADDRESS,
            init_commands[cmd].command,
            init_commands[cmd].data,
            init_commands[cmd].count & 0x1f
        );
        if (AXP202_OK != status) {
            return status;
        }
        cmd++;
    }

    return AXP202_OK;
}

axp202_err_t axp202_read(const axp202_t *axp, uint8_t reg, float *buffer)
{
    uint8_t tmp[4];
    float sensitivity = 1.0;
    float offset = 0.0;
    axp202_err_t status;

    switch (reg) {
    case AXP202_ACIN_VOLTAGE:
    case AXP202_VBUS_VOLTAGE:
        /* 1.7mV per LSB */
        sensitivity = 1.7 / 1000;
        break;
    case AXP202_ACIN_CURRENT:
        /* 0.625mA per LSB */
        sensitivity = 0.625 / 1000;
        break;
    case AXP202_VBUS_CURRENT:
        /* 0.375mA per LSB */
        sensitivity = 0.375 / 1000;
        break;
    case AXP202_TEMP:
        /* 0.1C per LSB, 0x00 = -144.7C */
        sensitivity = 0.1;
        offset = -144.7;
        break;
    case AXP202_TS_INPUT:
        /* 0.8mV per LSB */
        sensitivity = 0.8 / 1000;
        /* Either 0.0 or 0.7 */
        offset = 0.7;
        break;
    case AXP202_GPIO0_VOLTAGE:
    case AXP202_GPIO1_VOLTAGE:
        /* 0.5mV per LSB */
        sensitivity = 0.5 / 1000;
        /* TODO: either 0.0 or 0.7 */
        offset = 0.0;
        break;
    case AXP202_BATTERY_POWER:
        /* 2 * 1.1mV * 0.5mA per LSB */
        return read_battery_power(axp, buffer);
        break;
    case AXP202_BATTERY_VOLTAGE:
        /* 1.1mV per LSB */
        sensitivity = 1.1 / 1000;
        break;
    case AXP202_CHARGE_CURRENT:
    case AXP202_DISCHARGE_CURRENT:
        /* 0.5mV per LSB */
        sensitivity = 0.5 / 1000;
        break;
    case AXP202_IPSOUT_VOLTAGE:
        /* 1.4mV per LSB */
        sensitivity = 1.4 / 1000;
        break;
    case AXP202_COULOMB_COUNTER:
        /* This is currently untested. */
        return read_coloumb_counter(axp, buffer);
        break;
    case AXP202_FUEL_GAUGE:
        return read_fuel_gauge(axp, buffer);
        break;
    }

    status = axp->read(axp->handle, AXP202_ADDRESS, reg, tmp, 2);
    if (AXP202_OK != status) {
        return status;
    }
    *buffer = (((tmp[0] << 4) + tmp[1]) * sensitivity) + offset;

    return AXP202_OK;
}

axp202_err_t axp202_ioctl(const axp202_t *axp, uint16_t command, uint8_t *buffer)
{
    uint8_t reg = command >> 8;
    uint8_t tmp;

    switch (command) {
    case AXP202_READ_POWER_STATUS:
    case AXP202_READ_CHARGE_STATUS:
        return axp->read(axp->handle, AXP202_ADDRESS, reg, buffer, 1);
        break;
    case AXP202_COULOMB_COUNTER_ENABLE:
        tmp = 0b10000000;
        return axp->write(axp->handle, AXP202_ADDRESS, reg, &tmp, 1);
        break;
    case AXP202_COULOMB_COUNTER_DISABLE:
        tmp = 0b00000000;
        return axp->write(axp->handle, AXP202_ADDRESS, reg, &tmp, 1);
        break;
    case AXP202_COULOMB_COUNTER_SUSPEND:
        tmp = 0b11000000;
        return axp->write(axp->handle, AXP202_ADDRESS, reg, &tmp, 1);
        break;
    case AXP202_COULOMB_COUNTER_CLEAR:
        tmp = 0b10100000;
        return axp->write(axp->handle, AXP202_ADDRESS, reg, &tmp, 1);
        break;
    }

    return AXP202_ERROR_NOTTY;
}

static axp202_err_t read_coloumb_counter(const axp202_t *axp, float *buffer)
{
    uint8_t tmp[4];
    int32_t coin, coout;
    axp202_err_t status;

    status = axp->read(axp->handle, AXP202_ADDRESS, AXP202_CHARGE_COULOMB, tmp, sizeof(coin));
    if (AXP202_OK != status) {
        return status;
    }
    coin = (tmp[0] << 24) + (tmp[1] << 16) + (tmp[2] << 8) + tmp[3];

    status = axp->read(axp->handle, AXP202_ADDRESS, AXP202_DISCHARGE_COULOMB, tmp, sizeof(coout));
    if (AXP202_OK != status) {
        return status;
    }
    coout = (tmp[0] << 24) + (tmp[1] << 16) + (tmp[2] << 8) + tmp[3];

    /* CmAh = 65536 * 0.5mA *（coin - cout) / 3600 / ADC sample rate */
    *buffer = 32768 * (coin - coout) / 3600 / 25;

    return AXP202_OK;
}

static axp202_err_t read_battery_power(const axp202_t *axp, float *buffer)
{
    uint8_t tmp[4];
    float sensitivity;
    axp202_err_t status;

    /* 2 * 1.1mV * 0.5mA per LSB */
    sensitivity = 2 * 1.1 * 0.5 / 1000;
    status = axp->read(axp->handle, AXP202_ADDRESS, AXP202_BATTERY_POWER, tmp, 3);
    if (AXP202_OK != status) {
        return status;
    }
    *buffer = (((tmp[0] << 16) + (tmp[1] << 8) + tmp[2]) * sensitivity);
    return AXP202_OK;
}

static axp202_err_t read_fuel_gauge(const axp202_t *axp, float *buffer)
{
    axp202_err_t status;
    uint8_t tmp;

    status = axp->read(axp->handle, AXP202_ADDRESS, AXP202_FUEL_GAUGE, &tmp, 1);
    if (AXP202_OK != status) {
        return status;
    }
    tmp &= ~0b10000000;
    *buffer = (float)(tmp);
    return AXP202_OK;
}
