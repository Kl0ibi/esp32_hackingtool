# Platform agnostic I2C driver for AXP202 PMU

To use this library you must to provide functions for both reading and writing the I2C bus. Function definitions must be the following.

```c
int32_t i2c_read(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size);
int32_t i2c_write(void *handle, uint8_t address, uint8_t reg, const uint8_t *buffer, uint16_t size);
```

Where `address` is the I2C address, `reg` is the register to read or write, `buffer` holds the data to write or read into and `size` is the amount of data to read or write. The `handle` parameter is an optional customizable argument. You can use it if your I2C implementation requires any additional information such has number of the hardware I2C driver. For example HAL implementation see [ESP I2C helper](https://github.com/tuupola/esp_i2c_helper). For working example see [TTGO T-Watch 2020 kitchen sink](https://github.com/tuupola/esp_twatch2020).

## Usage

```
$ make menuconfig
```

```c
#include "axp202.h"
#include "user_i2c.h"

float vacin, iacin, vvbus, ivbus, vts, vgpio0, vgpio1, temp, pbat;
float vbat, icharge, idischarge, ipsout, cbat, fuel;
uint8_t power, charge;
axp202_t axp;

/* Add pointers to HAL functions. */
axp.read = &user_i2c_read;
axp.write = &user_i2c_write;

/* You could set the handle here. It can be pointer to anything. */
axp.handle = NULL;

axp202_init(&axp);

axp202_read(&axp, AXP202_ACIN_VOLTAGE, &vacin);
axp202_read(&axp, AXP202_ACIN_CURRENT, &iacin);
axp202_read(&axp, AXP202_VBUS_VOLTAGE, &vvbus);
axp202_read(&axp, AXP202_VBUS_CURRENT, &ivbus);
axp202_read(&axp, AXP202_TEMP, &temp);
axp202_read(&axp, AXP202_TS_INPUT, &vts);
axp202_read(&axp, AXP202_GPIO0_VOLTAGE, &vgpio0);
axp202_read(&axp, AXP202_GPIO1_VOLTAGE, &vgpio1);
axp202_read(&axp, AXP202_BATTERY_POWER, &pbat);
axp202_read(&axp, AXP202_BATTERY_VOLTAGE, &vbat);
axp202_read(&axp, AXP202_CHARGE_CURRENT, &icharge);
axp202_read(&axp, AXP202_DISCHARGE_CURRENT, &idischarge);
axp202_read(&axp, AXP202_IPSOUT_VOLTAGE, &ipsout);
axp202_read(&axp, AXP202_COULOMB_COUNTER, &cbat);
axp202_read(&axp, AXP202_FUEL_GAUGE, &fuel);


printf(
    "vacin: %.2fV iacin: %.2fA vvbus: %.2fV ivbus: %.2fA vts: %.2fV temp: %.0fC "
    "vgpio0: %.2fV vgpio1: %.2fV pbat: %.2fmW vbat: %.2fV icharge: %.2fA "
    "idischarge: %.2fA, ipsout: %.2fV cbat: %.2fmAh fuel: %.0f%%",
    vacin, iacin, vvbus, ivbus, vts, temp, vgpio0, vgpio1, pbat, vbat, icharge,
    idischarge, ipsout, cbat, fuel
);

axp202_ioctl(&axp, AXP202_READ_POWER_STATUS, &power);
axp202_ioctl(&axp, AXP202_READ_CHARGE_STATUS, &charge);

printf("power: 0x%02x charge: 0x%02x", power, charge);

axp202_ioctl(&axp, AXP202_COULOMB_COUNTER_ENABLE, NULL);
axp202_read(&axp, AXP202_COULOMB_COUNTER, &cbat);

printf("cbat: %.2fmAh", cbat);
```