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
#ifndef HTOOL_WIFI_H
#define HTOOL_WIFI_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <esp_system.h>
#include <string.h>
#include <stdio.h>
#include "esp_event.h"
#include <lwip/netif.h>
#include "esp_wifi.h"
#include "nvs.h"
#include "esp_log.h"
#include "nvs_flash.h"

extern wchar_t username1[22];
extern wchar_t username2[22];
extern wchar_t username3[22];
extern wchar_t username4[22];

extern wchar_t password[64];

typedef struct htool_wifi_client {
    bool connection_in_progress;            ///< Provides information if a connection is currently in progress (true) or not (false).
    bool scan_in_progress;                  ///< Provides information if a scan is currently in progress (true) or not (false).
    bool wifi_station_active;               ///< Provides information whether an WIFI Station is connected (true) or not connected (false).
    bool wifi_connected;
    bool wifi_handler_active;               ///< Is the wifi handler currently running (true) or not running (false).
    esp_netif_t *esp_netif;					///< Initialized networking interface.
    EventGroupHandle_t status_bits;         ///< Event group for BIT management.
    char hostname[30];
} htool_wifi_client_t;

void htool_wifi_captive_portal_stop();

void htool_wifi_captive_portal_start();

void htool_wifi_send_deauth_frame(uint8_t ssid_index, bool is_station);

void htool_wifi_send_disassociate_frame(uint8_t ssid_index, bool is_station);

void htool_wifi_start_active_scan();

void htool_wifi_start_passive_scan();

void init_wifi(wifi_mode_t mode);

int htool_wifi_init();

void htool_wifi_start();

void htool_wifi_start_beacon_spammer();

void htool_wifi_start_deauth();

void htool_set_wifi_sta_config();

#endif