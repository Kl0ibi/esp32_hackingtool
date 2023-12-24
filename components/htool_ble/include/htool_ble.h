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
#include <stdint.h>
#include <stdbool.h>

#define HTOOL_BLE_OK 0
#define HTOOL_BLE_ERR 1


typedef enum {
    ADV_APPLE_RANDOM = 0,
    ADV_APPLE_AIRPODS = 1,
    ADV_APPLE_AIRPODS_PRO = 2,
    ADV_APPLE_AIRPODS_MAX = 3,
    ADV_APPLE_AIRPODS_GEN2 = 4,
    ADV_APPLE_AIRPODS_GEN3 = 5,
    ADV_APPLE_AIRPODS_PRO_GEN2 = 6,
    ADV_APPLE_POWER_BEATS = 7,
    ADV_APPLE_POWERBEATSPRO = 8,
    ADV_APPLE_BEATSSOLOPRO = 9,
    ADV_APPLE_BEATSSTUDIOBUDS = 10,
    ADV_APPLE_BEATSFLEX = 11,
    ADV_APPLE_BEATSX = 12,
    ADV_APPLE_BEATSSOLO3 = 13,
    ADV_APPLE_BEATSSTUDIO3 = 14,
    ADV_APPLE_BEATSSTUDIOPRO = 15,
    ADV_APPLE_BEATSFITPRO = 16,
    ADV_APPLE_BEATSSTUDIOBUDSPLUS = 17,
    ADV_APPLE_APPLETVSETUP = 18,
    ADV_APPLE_APPLETVPAIR = 19,
    ADV_APPLE_APPLETVNEWUSER = 20,
    ADV_APPLE_APPLETVAPPLEIDSETUP = 21,
    ADV_APPLE_APPLETVWIRELESSAUDIOSYNC = 22,
    ADV_APPLE_APPLETVHOMEKITSETUP = 23,
    ADV_APPLE_APPLETVKEYBOARD = 24,
    ADV_APPLE_APPLETVCONNECTINGTONETWORK = 25,
    ADV_APPLE_HOMEPODSETUP = 26,
    ADV_APPLE_SETUPNEWPHONE = 27,
    ADV_APPLE_TRANSFERNUMBER = 28,
    ADV_APPLE_TVCOLORBALANCE = 29,
    ADV_GOOGLE = 30,
    ADV_SAMSUNG_RANDOM = 31,
    ADV_SAMSUNG_WATCH4 = 32,
    ADV_SAMSUNG_FRENCH_WATCH4 = 33,
    ADV_SAMSUNG_FOX_WATCH5 = 34,
    ADV_SAMSUNG_WATCH5 = 35,
    ADV_SAMSUNG_WATCH5_PRO = 36,
    ADV_SAMSUNG_WATCH6 = 37,
    ADV_MICROSOFT = 38,
    ADV_RANDOM = 39,
} adv_t;


bool htool_ble_adv_running();


void htool_ble_stop_adv();


void htool_ble_start_adv();


void htool_ble_set_adv_data(adv_t index);


uint8_t htool_ble_deinit();


uint8_t htool_ble_init();
