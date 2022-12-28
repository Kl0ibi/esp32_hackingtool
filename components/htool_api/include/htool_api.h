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
#ifndef HTOOL_API_H
#define HTOOL_API_H

#include <lwip/netif.h>
#include "esp_wifi.h"

#define HTOOL_OK 0
#define HTOOL_BASE 100
#define HTOOL_ERR_GENERAL (HTOOL_BASE + 1)
#define HTOOL_ERR_TIMEOUT (HTOOL_BASE + 2)
#define HTOOL_ERR_BUSY (HTOOL_BASE + 3)
#define HTOOL_ERR_CONNECTION_FAILED (HTOOL_BASE + 4)
#define HTOOL_ERR_WIFI_NOT_CONNECT (HTOOL_BASE + 5) ///< No WiFi network connected
#define HTOOL_ERR_TOO_HIGH_NETWORK_ID (HTOOL_BASE + 6) ///< The network id is higher than the HTOOL_MAX_WIFI_NETWORK_SLOTS
#define HTOOL_ERR_WIFI_HANDLING_TASK_ALREADY_RUNNING (HTOOL_BASE + 7)
#define HTOOL_ERR_WIFI_HANDLING_TASK_START_FAILED (HTOOL_BASE + 8)
#define HTOOL_ERR_POINTER_NULL (HTOOL_BASE + 9)
#define HTOOL_ERR_MEMORY (HTOOL_BASE + 10)
#define HTOOL_ERR_ID_NOT_FOUND (HTOOL_BASE + 11)
#define HTOOL_ERR_MAX_SLOTS_CONFIGURED (HTOOL_BASE + 12)
#define HTOOL_ERR_CLIENT (HTOOL_BASE + 13)
#define HTOOL_ERR_NO_CONFIGURED_NETWORKS (HTOOL_BASE + 14)
#define HTOOL_ERR_INVALID_CREDENTIALS (HTOOL_BASE + 15)
#define HTOOL_ERR_ALREADY_INITIALIZED (HTOOL_BASE + 16)

#define FREE_MEM(x) x ? free(x) : 0

typedef struct {
    bool is_evil_twin;
    uint8_t ssid_index;
    uint8_t  cp_index;
} captive_portal_task_args_t;

extern captive_portal_task_args_t captive_portal_task_args;

extern wifi_ap_record_t *global_scans;

extern uint8_t global_scans_count;

void htool_api_send_disassociate_frame(uint8_t index, bool is_station);

void htool_api_send_deauth_frame(uint8_t index, bool is_station);

void htool_api_start_evil_twin(uint8_t ssid_index);

void htool_api_stop_captive_portal();

void htool_api_start_captive_portal(uint8_t cp_index);

bool htool_api_is_deauther_running();

bool htool_api_is_beacon_spammer_running();

void htool_api_start_beacon_spammer();

void htool_api_start_deauther();

void htool_api_start_passive_scan();

void htool_api_start_active_scan();

void htool_api_init();

void htool_api_start();

#endif

