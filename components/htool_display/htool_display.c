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
#include "htool_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "esp_event.h"
#include "htool_api.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <font6x9.h>
#include <font5x7.h>
#include <aps.h>
#include <fps.h>
#include <hagl_hal.h>
#include <hagl.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "htool_api.h"
#include "htool_wifi.h"

static const char *TAG = "main";
static EventGroupHandle_t event;
static hagl_backend_t *display;

display_states cur_handling_state = ST_STARTUP;

//TODO: make bit alignment and bitsplitting more generic
volatile uint64_t last_timestamp = 0;
volatile uint64_t last_long_press_left_timestamp = 0;
volatile uint64_t last_long_press_right_timestamp = 0;
volatile bool long_press_right = false;
volatile bool long_press_left = false;

uint64_t pause_timestamp = 0;
bool beacon_spammer_running = false;
bool deauther_running = false;
bool cp_is_running = false;
bool evil_twin_is_running = false;
bool first_scan = true;

uint8_t target_ch = 0;
uint8_t target_bssid[6] = {0};
uint8_t animation = 0;

wchar_t evil_twin_ssid[26] = {0};

color_t color_all_scans[11];


const wchar_t header[23] = u"HackingTool by kl0ibi";
const wchar_t menu[60] = u"Menu:\nLeft: ↑ / Right: ↓\nRight Long Press: OK";
const wchar_t menu_scan[40] = u"Scan:\nLeft Long Press: BACK";
const wchar_t scan[40] = u"-) Scan Networks";
const wchar_t deauth[40] = u"-) Deauth WiFi";
const wchar_t beacon[40] = u"-) Beacon Spammer";
const wchar_t c_portal[40] = u"-) Captive Portal";
const wchar_t evil_twin[40] = u"-) Evil Twin";
const wchar_t ble_spammer[40] = u"Deauth:\nLeft Long Press: BACK";
wchar_t scan_list[200];
wchar_t scans_cut[20] = {0};
uint8_t printy = 0;
uint8_t length = 0;
wchar_t scans[10][50] = {0};
char scan_auth[5] = {};

bool htool_display_is_beacon_spammer_running() {
    return beacon_spammer_running;
}

static void menu_task() {
    color_t color_header = hagl_color(display, 20, 50, 250);
    color_t color_passive = hagl_color(display, 0, 0, 0);
    color_t color_green = hagl_color(display, 0, 255, 0);
    color_t color_red = hagl_color(display, 255, 0, 0);

    menu_cnt = 0;

    uint8_t min_y = 0;
    uint8_t max_y = 10;
    uint8_t line_y = 0;
    color_t color_dark_green = hagl_color(display, 0, 100, 0);
    color_t color_darker_green = hagl_color(display, 0, 50, 0);

    hagl_flush(display);
    hagl_clear(display);

    while (true) {
        hagl_put_text(display, header, 0, 0, color_header, font6x9);
        switch (cur_handling_state) {
            case ST_STARTUP:
                hagl_clear(display);
                hagl_flush(display);
                while (true) {
                    line_y++;
                    if (line_y > 10) {
                        line_y = 0;
                        min_y = min_y + 10;
                        max_y = max_y + 10;
                        if (max_y > 240) {
                            break;
                        }
                    }
                    hagl_put_text(display, u"hackingtool", 35, 110, color_green, font6x9);
                    hagl_put_text(display, u"by kl0ibi", 40, 120, color_green, font6x9);
                    int16_t x0 = esp_random() % 135;
                    int16_t y0 = (esp_random() % (max_y - min_y + 1)) + min_y;
                    char random_bit = (char) (esp_random() & 1) + '0';
                    hagl_put_char(display, random_bit, x0, y0, color_green, font6x9);
                    hagl_put_char(display, random_bit, x0-5, y0+15, color_dark_green, font6x9);
                    hagl_put_char(display, random_bit, x0, min_y - 10, color_dark_green, font6x9);
                    hagl_put_char(display, random_bit, x0, min_y - 20, color_darker_green, font6x9);
                    hagl_flush(display);
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                hagl_flush(display);
                hagl_clear(display);
                cur_handling_state = ST_MENU;
                break;
            case ST_MENU:
                hagl_put_text(display, menu, 0, 10, color_header, font6x9);
                if (menu_cnt == 0) {
                    hagl_put_text(display, scan, 0, 40, color_red, font6x9);
                    hagl_put_text(display, deauth, 0, 50, color_green, font6x9);
                    hagl_put_text(display, beacon, 0, 60, color_green, font6x9);
                    hagl_put_text(display, c_portal, 0, 70, color_green, font6x9);
                    hagl_put_text(display, evil_twin, 0, 80, color_green, font6x9);
                }
                else if (menu_cnt == 1) {
                    hagl_put_text(display, scan, 0, 40, color_green, font6x9);
                    hagl_put_text(display, deauth, 0, 50, color_red, font6x9);
                    hagl_put_text(display, beacon, 0, 60, color_green, font6x9);
                    hagl_put_text(display, c_portal, 0, 70, color_green, font6x9);
                    hagl_put_text(display, evil_twin, 0, 80, color_green, font6x9);
                }
                else if (menu_cnt == 2) {
                    hagl_put_text(display, scan, 0, 40, color_green, font6x9);
                    hagl_put_text(display, deauth, 0, 50, color_green, font6x9);
                    hagl_put_text(display, beacon, 0, 60, color_red, font6x9);
                    hagl_put_text(display, c_portal, 0, 70, color_green, font6x9);
                    hagl_put_text(display, evil_twin, 0, 80, color_green, font6x9);
                }
                else if (menu_cnt == 3) {
                    hagl_put_text(display, scan, 0, 40, color_green, font6x9);
                    hagl_put_text(display, deauth, 0, 50, color_green, font6x9);
                    hagl_put_text(display, beacon, 0, 60, color_green, font6x9);
                    hagl_put_text(display, c_portal, 0, 70, color_red, font6x9);
                    hagl_put_text(display, evil_twin, 0, 80, color_green, font6x9);
                }
                else if (menu_cnt == 4) {
                    hagl_put_text(display, scan, 0, 40, color_green, font6x9);
                    hagl_put_text(display, deauth, 0, 50, color_green, font6x9);
                    hagl_put_text(display, beacon, 0, 60, color_green, font6x9);
                    hagl_put_text(display, c_portal, 0, 70, color_green, font6x9);
                    hagl_put_text(display, evil_twin, 0, 80, color_red, font6x9);
                }
                if (long_press_right) {
                    ESP_LOGW(TAG, "long pressed right");
                    long_press_right = false;
                    cur_handling_state = menu_cnt + 1;
                    menu_cnt = 0;
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                if (long_press_left) {
                    long_press_left = false;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                break;
            case ST_SCAN:
                printy = 40;
                hagl_put_text(display, u"Scan:\nLeft Long Press: BACK", 0, 10, color_header, font6x9);
                    if ((esp_timer_get_time() - pause_timestamp > 15000000) || first_scan) {
                        if (first_scan) {
                            htool_api_start_active_scan();
                            first_scan = false;
                        }
                        else {
                            htool_api_start_passive_scan();
                            pause_timestamp = esp_timer_get_time();
                        }
                        scan_started = true;
                        pause_timestamp = esp_timer_get_time();
                    }
                    else {
                        if (!scan_started) {
                            hagl_put_text(display, u"Scan List:", 0, 28, color_header, font6x9);
                            for (uint8_t i = 0; i < (global_scans_count > 8 ? 8: global_scans_count); i++) {
                                length = strlen((const char *) global_scans[i].ssid);
                                if (length > 15) {
                                    length = 15;
                                }
                                if (global_scans[i].authmode == 0) {
                                    //OPEN
                                    sprintf(scan_auth, "OPEN");
                                }
                                else if (global_scans[i].authmode == 1) {
                                    //WEP
                                    sprintf(scan_auth, "WEP");
                                }
                                else {
                                    //WPA or versions of it
                                    sprintf(scan_auth, "WPA");
                                }
                                swprintf(scans[i], sizeof(scans[i]), u"%.*s %d %s\n%02x:%02x:%02x:%02x:%02x:%02x ch: %d/%d",length, global_scans[i].ssid, global_scans[i].rssi, scan_auth, global_scans[i].bssid[0],
                                         global_scans[i].bssid[1], global_scans[i].bssid[2], global_scans[i].bssid[3], global_scans[i].bssid[4], global_scans[i].bssid[5], global_scans[i].second, global_scans[i].primary);
                                hagl_put_text(display, scans[i], 0, printy, hagl_color(display, 0, 255, 0), font5x7);
                                printy += 20;

                            }
                        }
                        else {
                            if (animation == 0) {
                                hagl_put_text(display, u"Scanning .  ", 0, 28, color_header, font6x9);
                            }
                            else if (animation == 1) {
                                hagl_put_text(display, u"Scanning .. ", 0, 28, color_header, font6x9);
                            }
                            else if (animation == 2) {
                                hagl_put_text(display, u"Scanning ... ", 0, 28, color_header, font6x9);
                            }
                            else if (animation == 3) {
                                hagl_put_text(display, u"Scanning  .. ", 0, 28, color_header, font6x9);
                            }
                            else if (animation == 4) {
                                hagl_put_text(display, u"Scanning   . ", 0, 28, color_header, font6x9);
                            }
                            else if (animation == 5) {
                                hagl_put_text(display, u"Scanning     ", 0, 28, color_header, font6x9);
                            }
                            animation++;
                            if (animation == 6) {
                                animation = 0;
                            }
                            for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                                hagl_put_text(display, scans[i], 0, printy, hagl_color(display, 0, 255, 0), font5x7);
                                printy += 20;
                            }
                        }
                    }
                if (long_press_left) {
                    long_press_left = false;
                    menu_cnt = 0;
                    if (scan_started) {
                        esp_wifi_scan_stop();
                        scan_started = false;
                    }
                    first_scan = true;
                    for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                        memset(scans[i], 0, sizeof(scans[i]));
                    }
                    ESP_LOGW(TAG, "long pressed left");
                    cur_handling_state = ST_MENU;
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                hagl_clear(display);
                break;
            case ST_DEAUTH:
                printy = 55;
                hagl_put_text(display, u"Deauth:\nLeft Long Press: BACK", 0, 10, color_header, font5x7);
                hagl_put_text(display, u"Right Long Press:", 0, 25, color_header, font5x7);
                hagl_put_text(display, u"START / STOP", 0, 35, color_header, font5x7);
                if (!deauther_running) {
                    if ((esp_timer_get_time() - pause_timestamp > 15000000) || first_scan) {
                        if (first_scan) {
                            htool_api_start_active_scan();
                            first_scan = false;
                        }
                        else {
                            htool_api_start_passive_scan();
                            pause_timestamp = esp_timer_get_time();
                        }
                        scan_started = true;
                        pause_timestamp = esp_timer_get_time();
                    }
                    else {
                        if (!scan_started) {
                            hagl_put_text(display, u"Choose WiFi:", 0, 43, color_header, font6x9);
                            for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                                length = strlen((const char *) global_scans[i].ssid);
                                if (length > 26) {
                                    length = 26;
                                }
                                swprintf(scans[i], sizeof(scans[i]), u"%.*s", length, global_scans[i].ssid);
                            }
                        }
                        else {
                            if (animation == 0) {
                                hagl_put_text(display, u"Scanning .  ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 1) {
                                hagl_put_text(display, u"Scanning .. ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 2) {
                                hagl_put_text(display, u"Scanning ... ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 3) {
                                hagl_put_text(display, u"Scanning  .. ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 4) {
                                hagl_put_text(display, u"Scanning   . ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 5) {
                                hagl_put_text(display, u"Scanning     ", 0, 43, color_header, font6x9);
                            }
                            animation++;
                            if (animation == 6) {
                                animation = 0;
                            }
                        }
                    }
                    hagl_put_text(display, u"[STOPPED]", 78, 43, color_red, font6x9);
                }
                else {
                    hagl_put_text(display, u"Choose WiFi:", 0, 43, color_header, font6x9);
                    hagl_put_text(display, u"[RUNNING]", 78, 43, color_green, font6x9);
                }
                for (uint8_t i = 0; i < 11; i++) {
                    color_all_scans[i] = hagl_color(display, 0, 255, 0);
                }
                color_all_scans[menu_cnt] = hagl_color(display, 255, 0, 0);

                for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                    if (i == menu_cnt) {
                        hagl_put_text(display, scans[i], 0, printy, color_all_scans[menu_cnt], font5x7);
                        printy = printy + 10;
                    }
                    else {
                        hagl_put_text(display, scans[i], 0, printy, color_all_scans[i], font5x7);
                        printy = printy + 10;
                    }
                }
                if (scans[0][0] != 0) {
                    if (global_scans_count == menu_cnt) {
                    hagl_put_text(display, u"Deauth all WiFis", 0, printy, color_all_scans[menu_cnt], font5x7);
                    }
                    else {
                        hagl_put_text(display, u"Deauth all WiFis", 0, printy, color_green, font5x7);
                    }
                }
                if (long_press_right) {
                    long_press_right = false;
                    pause_timestamp = 0;
                    if (deauther_running) {
                        deauther_running = false;
                        first_scan = true;
                    }
                    else {
                        htool_api_start_deauther();
                        deauther_running = true;
                    }
                }
                if (long_press_left) {
                    menu_cnt = 0;
                    pause_timestamp = 0;
                    long_press_left = false;
                    ESP_LOGW(TAG, "long pressed left");
                    deauther_running = false;
                    first_scan = true;
                    if (scan_started) {
                        esp_wifi_scan_stop();
                        scan_started = false;
                    }
                    for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                        memset(scans[i], 0, sizeof(scans[i]));
                    }
                    cur_handling_state = ST_MENU;
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                hagl_clear(display);
                break;
            case ST_BEACON:
                hagl_put_text(display, u"Beacon Spammer\nLong Left Press: BACK", 0, 10, color_header, font5x7);
                hagl_put_text(display, u"Long Right Press to", 0, 26, color_header, font5x7);
                if (long_press_right) {
                    long_press_right = false;
                    if (beacon_spammer_running) {
                        beacon_spammer_running = false;
                    }
                    else {
                        htool_api_start_beacon_spammer();
                        beacon_spammer_running = true;
                    }
                }
                if (long_press_left) {
                    long_press_left = false;
                    ESP_LOGW(TAG, "long pressed left");
                    cur_handling_state = ST_MENU;
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                if (beacon_spammer_running) {
                    hagl_put_text(display, u"STOP!", 100, 26, color_red, font6x9);
                    if (animation == 0) {
                        hagl_put_text(display, u"Spamming .  ", 0, 34, color_header, font6x9);
                    }
                    else if (animation == 1) {
                        hagl_put_text(display, u"Spamming .. ", 0, 34, color_header, font6x9);
                    }
                    else if (animation == 2) {
                        hagl_put_text(display, u"Spamming ... ", 0, 34, color_header, font6x9);
                    }
                    else if (animation == 3) {
                        hagl_put_text(display, u"Spamming  .. ", 0, 34, color_header, font6x9);
                    }
                    else if (animation == 4) {
                        hagl_put_text(display, u"Spamming   . ", 0, 34, color_header, font6x9);
                    }
                    else if (animation == 5) {
                        hagl_put_text(display, u"Spamming     ", 0, 34, color_header, font6x9);
                    }
                    animation++;
                    if (animation == 6) {
                        animation = 0;
                    }
                }
                else {
                    hagl_put_text(display, u"SPAM!", 100, 26, color_green, font6x9);
                    hagl_put_text(display, u"Spamming     ", 0, 34, color_passive, font6x9);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                break;
            case ST_C_PORTAL:
                printy = 55;
                hagl_put_text(display, u"Captive Portal:\nLeft Long Press: BACK", 0, 10, color_header, font5x7);
                hagl_put_text(display, u"Right Long Press:", 0, 25, color_header, font5x7);
                hagl_put_text(display, u"START / STOP", 0, 35, color_header, font5x7);

                if (cp_is_running) {
                    //wait for credentialss
                    //print credentials
                    if (animation == 0) {
                        hagl_put_text(display, u"Wait for creds .  ", 0, 45, color_green, font6x9);
                    }
                    else if (animation == 1) {
                        hagl_put_text(display, u"Wait for creds .. ", 0, 45, color_green, font6x9);
                    }
                    else if (animation == 2) {
                        hagl_put_text(display, u"Wait for creds ... ", 0, 45, color_green, font6x9);
                    }
                    else if (animation == 3) {
                        hagl_put_text(display, u"Wait for creds  .. ", 0, 45, color_green, font6x9);
                    }
                    else if (animation == 4) {
                        hagl_put_text(display, u"Wait for creds   . ", 0, 45, color_green, font6x9);
                    }
                    else if (animation == 5) {
                        hagl_put_text(display, u"Wait for creds     ", 0, 45, color_green, font6x9);
                    }
                    animation++;
                    if (animation == 6) {
                        animation = 0;
                    }

                   if (username1[0] != 0) {
                       hagl_put_text(display, u"Username:", 0, printy, color_red, font6x9);
                       printy = printy +10;
                       hagl_put_text(display, username1, 0, printy, color_green, font6x9);
                       printy = printy +10;
                   }
                    if (username2[0] != 0) {
                        hagl_put_text(display, username2, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    if (username3[0] != 0) {
                        hagl_put_text(display, username3, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    if (username4[0] != 0) {
                        hagl_put_text(display, username4, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                   if (password[0] != 0) {
                        hagl_put_text(display, u"Password:", 0, printy, color_red, font6x9);
                        hagl_put_text(display, password, 0, printy + 10, color_green, font5x7);
                   }
                    hagl_put_text(display, u"[RUNNING]", 78, 35, color_green, font6x9);


                }
                else {
                    hagl_put_text(display, u"[STOPPED]", 78, 35, color_red, font6x9);
                    if (menu_cnt == 0) {
                        hagl_put_text(display, u"Google Captive Portal", 0, 55, color_red, font5x7);
                        hagl_put_text(display, u"McDonald's Captive Portal", 0, 65, color_green, font5x7);  //TODO: Which CaptivePortal could we add?
                        //hagl_put_text(display, u"Apple Captive Portal", 0, 135, color_green, font5x7); TOOD: add Apple shop captive portal
                    }
                    else {
                        hagl_put_text(display, u"Google Captive Portal", 0, 55, color_green, font5x7);
                        hagl_put_text(display, u"McDonald's Captive Portal", 0, 65, color_red, font5x7);
                    }
                }
                if (long_press_right) {
                    long_press_right = false;
                    if (cp_is_running) {
                        cp_is_running = false;
                        strcpy((char*)username1, "");
                        strcpy((char*)username2, "");
                        strcpy((char*)username3, "");
                        strcpy((char*)username4, "");
                        strcpy((char*)password, "");
                        htool_api_stop_captive_portal();
                    }
                    else {
                        vTaskDelay(pdMS_TO_TICKS(200));
                        htool_api_start_captive_portal(menu_cnt);
                        cp_is_running = true;
                    }
                }
                if (long_press_left) {
                    long_press_left = false;
                    ESP_LOGW(TAG, "long pressed left");
                    cur_handling_state = ST_MENU;
                    menu_cnt = 0;
                    if (cp_is_running) {
                        strcpy((char*)username1, "");
                        strcpy((char*)username2, "");
                        strcpy((char*)username3, "");
                        strcpy((char*)username4, "");
                        strcpy((char*)password, "");
                        htool_api_stop_captive_portal();
                        cp_is_running = false;
                    }
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                hagl_clear(display);
                break;
            case ST_EVIL_TWIN:
                printy = 75;
                hagl_put_text(display, u"Evil Twin:\nLeft Long Press: BACK", 0, 10, color_header, font5x7);
                hagl_put_text(display, u"Right Long Press:", 0, 25, color_header, font5x7);
                hagl_put_text(display, u"START / STOP", 0, 35, color_header, font5x7);

                if (evil_twin_is_running) {
                    //wait for credentialss
                    //print credentials
                    if (animation == 0) {
                        hagl_put_text(display, u"Wait for creds .  ", 0, printy-10, color_green, font6x9);
                    }
                    else if (animation == 1) {
                        hagl_put_text(display, u"Wait for creds .. ", 0, printy-10, color_green, font6x9);
                    }
                    else if (animation == 2) {
                        hagl_put_text(display, u"Wait for creds ... ", 0, printy-10, color_green, font6x9);
                    }
                    else if (animation == 3) {
                        hagl_put_text(display, u"Wait for creds  .. ", 0, printy-10, color_green, font6x9);
                    }
                    else if (animation == 4) {
                        hagl_put_text(display, u"Wait for creds   . ", 0, printy-10, color_green, font6x9);
                    }
                    else if (animation == 5) {
                        hagl_put_text(display, u"Wait for creds     ", 0, printy-10, color_green, font6x9);
                    }
                    animation++;
                    if (animation == 6) {
                        animation = 0;
                    }

                    hagl_put_text(display, u"Target SSID:", 0, 45, color_green, font6x9);
                    hagl_put_text(display, evil_twin_ssid, 0, 55, color_green, font6x9);

                    if (username1[0] != 0) {
                        hagl_put_text(display, u"Password:", 0, printy, color_red, font6x9);
                        printy = printy +10;
                        hagl_put_text(display, username1, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    if (username2[0] != 0) {
                        hagl_put_text(display, username2, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    if (username3[0] != 0) {
                        hagl_put_text(display, username3, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    if (username4[0] != 0) {
                        hagl_put_text(display, username4, 0, printy, color_green, font6x9);
                        printy = printy +10;
                    }
                    hagl_put_text(display, u"[RUNNING]", 78, 35, color_green, font6x9);
                }
                else {
                    printy = 55;
                    for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                        color_all_scans[i] = hagl_color(display, 0, 255, 0);
                    }
                    color_all_scans[menu_cnt] = hagl_color(display, 255, 0, 0);

                    for (uint8_t i = 0; i < global_scans_count; i++) {
                        if (i == menu_cnt) {
                            hagl_put_text(display, scans[i], 0, printy, color_all_scans[menu_cnt], font5x7);
                            printy = printy + 10;
                        }
                        else {
                            hagl_put_text(display, scans[i], 0, printy, color_all_scans[i], font5x7);
                            printy = printy + 10;
                        }
                    }
                    if ((esp_timer_get_time() - pause_timestamp > 15000000) || first_scan) {
                        if (first_scan) {
                            htool_api_start_active_scan();
                            first_scan = false;
                        }
                        else {
                            htool_api_start_passive_scan();
                            pause_timestamp = esp_timer_get_time();
                        }
                        scan_started = true;
                        pause_timestamp = esp_timer_get_time();
                    }
                    else {
                        if (!scan_started) {
                            hagl_put_text(display, u"Choose WiFi:", 0, 43, color_header, font6x9);
                            for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                                length = strlen((const char *) global_scans[i].ssid);
                                if (length > 26) {
                                    length = 26;
                                }
                                swprintf(scans[i], sizeof(scans[i]), u"%.*s", length, global_scans[i].ssid);
                            }
                        }
                        else {
                            if (animation == 0) {
                                hagl_put_text(display, u"Scanning .  ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 1) {
                                hagl_put_text(display, u"Scanning .. ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 2) {
                                hagl_put_text(display, u"Scanning ... ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 3) {
                                hagl_put_text(display, u"Scanning  .. ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 4) {
                                hagl_put_text(display, u"Scanning   . ", 0, 43, color_header, font6x9);
                            }
                            else if (animation == 5) {
                                hagl_put_text(display, u"Scanning     ", 0, 43, color_header, font6x9);
                            }
                            animation++;
                            if (animation == 6) {
                                animation = 0;
                            }
                        }
                    }
                    hagl_put_text(display, u"[STOPPED]", 78, 35, color_red, font6x9);
                }
                if (long_press_right) {
                    long_press_right = false;
                    if (evil_twin_is_running) {
                        evil_twin_is_running = false;
                        strcpy((char*)username1, "");
                        strcpy((char*)username2, "");
                        strcpy((char*)username3, "");
                        strcpy((char*)username4, "");
                        strcpy((char*)password, "");
                        htool_api_stop_captive_portal();
                    }
                    else {
                        swprintf(evil_twin_ssid, sizeof(evil_twin_ssid), u"%.*s", strlen((const char*)global_scans[menu_cnt].ssid) > 26 ? 26 : strlen((const char*)global_scans[menu_cnt].ssid), global_scans[menu_cnt].ssid);
                        //strncpy((char*)evil_twin_ssid, (const char*)global_scans[menu_cnt], strlen((const char*)global_scans[menu_cnt].ssid) > 26 ? 26 : strlen((const char*)global_scans[menu_cnt].ssid));
                        htool_api_start_evil_twin(menu_cnt);
                        evil_twin_is_running = true;
                    }
                }
                if (long_press_left) {
                    long_press_left = false;
                    ESP_LOGW(TAG, "long pressed left");
                    cur_handling_state = ST_MENU;
                    first_scan = true;
                    menu_cnt = 0;
                    if (evil_twin_is_running) {
                        strcpy((char*)username1, "");
                        strcpy((char*)username2, "");
                        strcpy((char*)username3, "");
                        strcpy((char*)username4, "");
                        strcpy((char*)password, "");
                        htool_api_stop_captive_portal();
                        evil_twin_is_running = false;
                    }
                    for (uint8_t i = 0; i < (global_scans_count > 8 ? 8 : global_scans_count); i++) {
                        memset(scans[i], 0, sizeof(scans[i]));
                    }
                    hagl_flush(display);
                    hagl_clear(display);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                hagl_flush(display);
                hagl_clear(display);
                break;
            default:
                break;
        }
    }
}

bool htool_display_is_deauter_running() {
    return deauther_running;
}

static void IRAM_ATTR gpio_interrupt_handler(void *args) {
    if (esp_timer_get_time() >= last_timestamp+350000) { //debounce
        //long_press_right = false;
        //long_press_left = false;
        last_timestamp = esp_timer_get_time();
        ESP_EARLY_LOGI(TAG, "debounce");
        if (args == 0) {
            if (gpio_get_level(0)) {
                ESP_EARLY_LOGI(TAG, "long press left");
                long_press_left = true;
            }
        }
        else {
            if (gpio_get_level(35)) {
                ESP_EARLY_LOGI(TAG, "long press right");
                long_press_right = true;
            }
        }
    }
    else if ((int)args == 35) {
        if (long_press_right && esp_timer_get_time() <= last_timestamp+350000) {
            ESP_EARLY_LOGI(TAG, "debounce right");
            return;
        }
        ESP_EARLY_LOGI(TAG, "press right debounce");
        if (esp_timer_get_time() >= last_long_press_right_timestamp+200000) { //debounce
            long_press_left = false;
            long_press_right = false;
            last_long_press_right_timestamp = esp_timer_get_time();
            ESP_EARLY_LOGI(TAG, "press right");
            if (cur_handling_state == ST_DEAUTH) {
                menu_cnt++;
                if (menu_cnt > global_scans_count) {
                    menu_cnt = 0;
                }
            }
            else if (cur_handling_state == ST_EVIL_TWIN) {
                menu_cnt++;
                if (menu_cnt > global_scans_count - 1) {
                    menu_cnt = 0;
                }
            }
            else if (cur_handling_state == ST_C_PORTAL) {
                menu_cnt++;
                if (menu_cnt > 1) {
                    menu_cnt = 0;
                }
            }
            else if (cur_handling_state == ST_MENU) {
                menu_cnt++;
                if (menu_cnt > 4) {
                    menu_cnt = 0;
                }
            }
        }
    }
    else if ((int)args == 0) {
        if (long_press_left && esp_timer_get_time() <= last_timestamp+350000) {
            ESP_EARLY_LOGI(TAG, "debounce left");
            return;
        }
        ESP_EARLY_LOGI(TAG, "press left debounce");
        if (esp_timer_get_time() >= last_long_press_left_timestamp + 200000) { //debounce
            long_press_left = false;
            long_press_right = false;
            ESP_EARLY_LOGI(TAG, "press left");
            last_long_press_left_timestamp = esp_timer_get_time();
            if (cur_handling_state == ST_DEAUTH) {
                if (menu_cnt != 0) {
                    menu_cnt--;
                } else {
                    menu_cnt = global_scans_count;
                }
            }
            else if (cur_handling_state == ST_EVIL_TWIN) {
                if (menu_cnt != 0) {
                    menu_cnt--;
                } else {
                    menu_cnt = global_scans_count - 1;
                }
            }
            else if (cur_handling_state == ST_C_PORTAL) {
                if (menu_cnt != 0) {
                    menu_cnt--;
                } else {
                    menu_cnt = 1;
                }
            }
            else if (cur_handling_state == ST_MENU) {
                if (menu_cnt != 0) {
                    menu_cnt--;
                } else {
                    menu_cnt = 4;
                }
            }
        }
    }
}

void htool_display_init() {
    event = xEventGroupCreate();
    gpio_set_direction(0, GPIO_MODE_INPUT);
    gpio_set_direction(35, GPIO_MODE_INPUT);
    gpio_pullup_en(35);
    gpio_pulldown_en(35);
    display = hagl_init();
    gpio_install_isr_service(0);
    gpio_set_intr_type(0,GPIO_INTR_ANYEDGE);
    gpio_intr_enable(0);
    gpio_set_intr_type(35,GPIO_INTR_ANYEDGE);
    gpio_intr_enable(35);
    gpio_isr_handler_add(35, gpio_interrupt_handler, (void *)35);
    gpio_isr_handler_add(0, gpio_interrupt_handler, (void *)0);
}

void htool_display_start() {
    xTaskCreatePinnedToCore(menu_task, "test", (1024*6), NULL, 1, NULL, 1);
}


