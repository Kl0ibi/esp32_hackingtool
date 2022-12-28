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

#ifndef _AXP202_H
#define _AXP202_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

//#define	AXP202_ADDRESS	                (0x68)
#define	AXP202_ADDRESS	                (0x35)

/* Power control registers */
#define AXP202_POWER_STATUS             (0x00)
#define AXP202_CHARGE_STATUS            (0x01)
#define AXP202_OTG_VBUS_STATUS          (0x02)
#define AXP202_DATA_BUFFER0             (0x04)
#define AXP202_DATA_BUFFER1             (0x05)
#define AXP202_DATA_BUFFER2             (0x06)
#define AXP202_DATA_BUFFER3             (0x07)
#define AXP202_DATA_BUFFER4             (0x08)
#define AXP202_DATA_BUFFER5             (0x09)
#define AXP202_DATA_BUFFER6             (0x0a)
#define AXP202_DATA_BUFFER7             (0x0b)
#define AXP202_DATA_BUFFER8             (0x0c)
#define AXP202_DATA_BUFFER9             (0x0d)
#define AXP202_DATA_BUFFER10            (0x0e)
#define AXP202_DATA_BUFFER11            (0x0f)
#define AXP202_DCDC23_LDO234_EXTEN_CONTROL (0x12)
#define AXP202_DCDC2_VOLTAGE            (0x23)
#define AXP202_DCDC2_LDO3_SLOPE         (0x25)
#define AXP202_DCDC3_VOLTAGE            (0x27)
#define AXP202_LDO24_VOLTAGE            (0x28)
#define AXP202_LDO3_VOLTAGE             (0x29)
#define AXP202_VBUS_IPSOUT_CHANNEL      (0x30)
#define AXP202_SHUTDOWN_VOLTAGE         (0x31)
#define AXP202_SHUTDOWN_BATTERY_CHGLED_CONTROL (0x32)
#define AXP202_CHARGE_CONTROL_1         (0x33)
#define AXP202_CHARGE_CONTROL_2         (0x34)
#define AXP202_BATTERY_CHARGE_CONTROL   (0x35)
#define AXP202_PEK                      (0x36)
#define AXP202_DCDC_FREQUENCY           (0x37)
#define AXP202_BATTERY_CHARGE_LOW_TEMP  (0x38)
#define AXP202_BATTERY_CHARGE_HIGH_TEMP (0x39)
#define AXP202_APS_LOW_POWER1           (0x3A)
#define AXP202_APS_LOW_POWER2           (0x3B)
#define AXP202_BATTERY_DISCHARGE_LOW_TEMP  (0x3c)
#define AXP202_BATTERY_DISCHARGE_HIGH_TEMP (0x3d)
#define AXP202_DCDC_MODE                (0x80)
#define AXP202_ADC_ENABLE_1             (0x82)
#define AXP202_ADC_ENABLE_2             (0x83)
#define AXP202_ADC_RATE_TS_PIN          (0x84)
#define AXP202_GPIO10_INPUT_RANGE       (0x85)
#define AXP202_GPIO0_ADC_IRQ_RISING     (0x86)
#define AXP202_GPIO0_ADC_IRQ_FALLING    (0x87)
#define AXP202_TIMER_CONTROL            (0x8a)
#define AXP202_VBUS_MONITOR             (0x8b)
#define AXP202_TEMP_SHUTDOWN_CONTROL    (0x8f)

/* GPIO control registers */
#define AXP202_GPIO0_CONTROL            (0x90)
#define AXP202_GPIO0_LDO5_VOLTAGE       (0x91)
#define AXP202_GPIO1_CONTROL            (0x92)
#define AXP202_GPIO2_CONTROL            (0x93)
#define AXP202_GPIO20_SIGNAL_STATUS     (0x94)
#define AXP202_GPIO3_CONTROL            (0x95)

/* Interrupt control registers */
#define AXP202_ENABLE_CONTROL_1         (0x40)
#define AXP202_ENABLE_CONTROL_2         (0x41)
#define AXP202_ENABLE_CONTROL_3         (0x42)
#define AXP202_ENABLE_CONTROL_4         (0x43)
#define AXP202_ENABLE_CONTROL_5         (0x44)
#define AXP202_IRQ_STATUS_1             (0x48)
#define AXP202_IRQ_STATUS_2             (0x49)
#define AXP202_IRQ_STATUS_3             (0x4a)
#define AXP202_IRQ_STATUS_4             (0x4b)
#define AXP202_IRQ_STATUS_5             (0x4c)

/* ADC data registers */
#define AXP202_ACIN_VOLTAGE             (0x56)
#define AXP202_ACIN_CURRENT             (0x58)
#define AXP202_VBUS_VOLTAGE             (0x5a)
#define AXP202_VBUS_CURRENT             (0x5c)
#define AXP202_TEMP                     (0x5e)
#define AXP202_TS_INPUT                 (0x62)
#define AXP202_GPIO0_VOLTAGE            (0x64)
#define AXP202_GPIO1_VOLTAGE            (0x66)
#define AXP202_BATTERY_POWER            (0x70)
#define AXP202_BATTERY_VOLTAGE          (0x78)
#define AXP202_CHARGE_CURRENT           (0x7a)
#define AXP202_DISCHARGE_CURRENT        (0x7c)
#define AXP202_IPSOUT_VOLTAGE           (0x7e)
#define AXP202_CHARGE_COULOMB           (0xb0)
#define AXP202_DISCHARGE_COULOMB        (0xb4)
#define AXP202_COULOMB_COUNTER_CONTROL  (0xb8)
#define AXP202_FUEL_GAUGE               (0xb9)

/* Computed ADC */
#define AXP202_COULOMB_COUNTER          (0xff)

/* IOCTL commands */
#define	AXP202_READ_POWER_STATUS        (0x0001)
#define	AXP202_READ_CHARGE_STATUS       (0x0101)

#define AXP202_COULOMB_COUNTER_ENABLE   (0xb801)
#define AXP202_COULOMB_COUNTER_DISABLE  (0xb802)
#define AXP202_COULOMB_COUNTER_SUSPEND  (0xb803)
#define AXP202_COULOMB_COUNTER_CLEAR    (0xb804)

/* Error codes */
#define AXP202_OK                       (0)
#define AXP202_ERROR_NOTTY              (-1)

typedef struct {
    uint8_t command;
    uint8_t data[2];
    uint8_t count;
} axp202_init_command_t;

/* These should be provided by the HAL. */
typedef struct {
    int32_t (* read)(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size);
    int32_t (* write)(void *handle, uint8_t address, uint8_t reg, const uint8_t *buffer, uint16_t size);
    void *handle;
} axp202_t;

typedef int32_t axp202_err_t;

axp202_err_t axp202_init(const axp202_t *axp);
axp202_err_t axp202_read(const axp202_t *axp, uint8_t reg, float *buffer);
axp202_err_t axp202_ioctl(const axp202_t *axp, uint16_t command, uint8_t *buffer);

#ifdef __cplusplus
}
#endif
#endif
