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
#include <stdio.h>
#include "htool_api.h"
#include "htool_wifi.h"
#include "htool_display.h"
#include <esp_event.h>
#include <esp_log.h>

#define TAG "htool_api"

bool htool_api_is_beacon_spammer_running() {
    return htool_display_is_beacon_spammer_running();
}

bool htool_api_is_deauther_running() {
    return htool_display_is_deauter_running();
}

void htool_api_send_deauth_frame(uint8_t index, bool is_station) {
    htool_wifi_send_deauth_frame(index, is_station);
}

void htool_api_send_disassociate_frame(uint8_t index, bool is_station) {
    htool_wifi_send_disassociate_frame(index, is_station);
}

void htool_api_start_beacon_spammer(uint8_t beacon_index) {
    beacon_task_args.beacon_index = beacon_index;
    htool_wifi_start_beacon_spammer();
}

void htool_api_start_deauther() {
    htool_wifi_start_deauth();
}

void htool_api_stop_captive_portal() {
    htool_wifi_captive_portal_stop();
}

void htool_api_start_captive_portal(uint8_t cp_index) {
    captive_portal_task_args.is_evil_twin = false;
    captive_portal_task_args.cp_index = cp_index;
    htool_wifi_captive_portal_start();
}

void htool_api_start_evil_twin(uint8_t ssid_index, uint8_t cp_index) {
    esp_wifi_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    //start captive portal with specific ssid and router webpage to get credentials
    captive_portal_task_args.is_evil_twin = true;
    captive_portal_task_args.ssid_index = ssid_index;
    captive_portal_task_args.cp_index = cp_index;
    htool_wifi_captive_portal_start();
}

void htool_api_start_active_scan() {
    htool_wifi_start_active_scan();
}

void htool_api_start_passive_scan() {
    htool_wifi_start_passive_scan();
}

void htool_api_init () {
    esp_log_level_set("wifi", 4);
    if (htool_wifi_init() != HTOOL_OK) {
        ESP_LOGE(TAG, "Error at init HTool_Wifi!");
        abort();
    }
    htool_display_init();
}

void htool_api_start() {
   htool_wifi_start();
   htool_display_start();
}