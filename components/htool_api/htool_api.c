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
#include "htool_api.h"
#include "htool_wifi.h"
#include "htool_display.h"
#include <esp_event.h>
#include <esp_log.h>
#include "htool_pn532_spi.h"
#include "htool_uart.h"
#include "htool_ble.h"


#define MAX_COMMAND_LENGTH 50

#define TAG "htool_api"

static bool beacon_spammer_running = false;
static bool deauther_running = false;
static bool cp_is_running = false;
static bool evil_twin_is_running = false;


bool htool_api_ble_adv_running() {
    return htool_ble_adv_running();
}


void htool_api_ble_stop_adv() {
    htool_ble_stop_adv();
}


void htool_api_ble_start_adv() {
    htool_ble_start_adv();
}


void htool_api_set_ble_adv(uint8_t i) {
    htool_ble_set_adv_data(i);
}


uint8_t htool_api_ble_deinit() {
    return htool_ble_deinit();
}


uint8_t htool_api_ble_init() {
    return htool_ble_init();
}


bool htool_api_is_beacon_spammer_running() {
    return beacon_spammer_running;
}

bool htool_api_is_deauther_running() {
    return deauther_running;
}

bool htool_api_is_captive_portal_running() {
    return cp_is_running;
}

bool htool_api_is_evil_twin_running() {
    return evil_twin_is_running;
}

void htool_api_send_deauth_frame(uint8_t index, bool is_station) {
    htool_wifi_send_deauth_frame(index, is_station);
}

void htool_api_send_disassociate_frame(uint8_t index, bool is_station) {
    htool_wifi_send_disassociate_frame(index, is_station);
}

void htool_api_start_beacon_spammer(uint8_t beacon_index) {
    beacon_spammer_running = true;
    beacon_task_args.beacon_index = beacon_index;
    htool_wifi_start_beacon_spammer();
}

void htool_api_stop_beacon_spammer() {
    beacon_spammer_running = false;
}

void htool_api_start_deauther() {
    deauther_running = true;
    htool_wifi_start_deauth();
}

void htool_api_stop_deauther() {
    deauther_running = false;
}

void htool_api_start_captive_portal(uint8_t cp_index) {
    cp_is_running = true;
    captive_portal_task_args.is_evil_twin = false;
    captive_portal_task_args.cp_index = cp_index;
    htool_wifi_captive_portal_start();
}

void htool_api_stop_captive_portal() {
    if (cp_is_running) {
        cp_is_running = false; htool_wifi_captive_portal_stop();
    }
}

void htool_api_start_evil_twin(uint8_t ssid_index, uint8_t cp_index) {
    evil_twin_is_running = true;
    esp_wifi_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    //start captive portal with specific ssid and router webpage to get credentials
    captive_portal_task_args.is_evil_twin = true;
    captive_portal_task_args.ssid_index = ssid_index;
    captive_portal_task_args.cp_index = cp_index;
    htool_wifi_captive_portal_start();
}

void htool_api_stop_evil_twin() {
    if (evil_twin_is_running) {
        evil_twin_is_running = false; //TODO: add handling
        htool_wifi_captive_portal_stop();
    }
}

void htool_api_start_active_scan() {
    htool_wifi_start_active_scan();
}

void htool_api_start_passive_scan() {
    htool_wifi_start_passive_scan();
}

bool htool_api_is_wifi_connected() {
    return htool_wifi_is_wifi_connected();
}

uint8_t htool_api_connect_to_wifi() {
    return htool_wifi_connect();
}

void htool_api_setup_station(uint8_t ssid_index, char *password) {
    htool_wifi_setup_station(ssid_index, password);
}

void htool_api_init () {
    esp_log_level_set("wifi", 4);
    if (htool_wifi_init() != HTOOL_OK) {
        ESP_LOGE(TAG, "Error at init HTool_Wifi!");
        abort();
    }
    htool_display_init();
    htool_uart_cli_init();
}

void htool_api_start() {
    htool_wifi_start();
    htool_display_start();
    htool_uart_cli_start();
    //Debug
    //htool_pn532_spi_init(27, 26, 25, 33);
    //htool_pn532_spi_start();
}
