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
#include "htool_uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "htool_wifi.h"
#include "linenoise/linenoise.h"
#include "htool_nvsm.h"
#include "string.h"
#include "htool_api.h"
#include "argtable3/argtable3.h"
#include "htool_display.h"
#include "htool_netman.h"
#include "wchar.h"
#include <stdio_ext.h>

#define TAG "htool_api"
#define USER_KEY "_user"
#define PW_KEY "_pw"
#define HTOOL_UART_OK 0


static enum {
    STATE_INITIAL_LOGIN = 0,
    STATE_LOGIN = 1,
    STATE_RUNNING = 2,
} cur_state;

static struct {
    struct arg_lit *arg1;
    struct arg_lit *arg2;
    struct arg_end *end;
} scan_args;

static struct {
    struct arg_lit *arg1;
    struct arg_lit *arg2;
    struct arg_end *end;
} creds_change_args;

char user[32];
size_t user_len = 32;
char pw[32];
size_t pw_len = 32;
char *prompt = NULL;
char *line;


static void print_cb(char *string) {
    printf("%s\n", string);
    FREE_MEM(string);
}


static uint8_t get_current_row(uint32_t delay_ms) {
    int row = 0, col = 0;
    char seq[7];

    printf("\033[6n\n");
    if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, (unsigned char*)&seq, 7, pdMS_TO_TICKS(delay_ms))) {
        if (sscanf(seq, "\033[%d;%dR", &row, &col) != 2) {
            return 0; // Error or not supported
        }
    }
    printf("\033[A");

    return row;
}


static void print_animation(char *string, uint32_t timeout_ms) {
    bool first = true;
    uint8_t last_row = 0;
    uint8_t temp_row;
    uint32_t start_index = 0;
    uint32_t j;
    for (uint32_t i = 0; string[i] != '\0'; i++) {
        for (j = start_index; j <= i; j++) {
            printf("%c", string[j]);
        }
        printf("\n");
        if (string[i + 1] != '\0') {
            if ((temp_row = get_current_row(timeout_ms)) != last_row && !first) {
                start_index = j - 1;
            }
            first = false;
            last_row = temp_row;
            printf("\033[A\033[2K");
        }
    }
}


static void read_in_password(char *password, char *prompt, uint8_t *len) {
    *len = 0;
    if (prompt[0] != '\0') {
        printf("%s\033[A\n", prompt);
    }
    while (true) { 
        char c; 
        uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, (unsigned char*)&c, 1, portMAX_DELAY);
        if (c == '\n' || c == '\r') {
            if (*len == 0) {
                continue;
            }
            break;
        }
        if (c == '\t' || c == ' ' || (c >= 1 && c <= 26 && c != 8)) {
            continue;
        }
        if (c == '\033') {
            char seq[3];
            if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, (unsigned char*)seq, 2, 100 / portTICK_PERIOD_MS) == 2) {
                if (seq[0] == '[' && (seq[1] == 'A' || seq[1] == 'B' || seq[1] == 'C' || seq[1] == 'D')) {
                    continue;
                }
            }
        }
        if (c == 0x08 || c == 127) {
            if (*len > 0) {
                *len -= 1;
                printf("%s", prompt);
                for (uint8_t j = 0; j < *len; j++) {
                    printf("*");
                }
                printf("\033[A\n");
                printf("\033[2K");
            }
        }
        else {
            if (*len < 31) {
                password[*len] = c;
                *len += 1;
                printf("%s", prompt);
                for (uint8_t j = 0; j < *len; j++) {
                    printf("*");
                }
                printf("\033[A\n");
                printf("\033[2K");
            }
        }
    }
    password[*len] = '\0';
    *len += 1;
}


static char* authtype_to_string(wifi_auth_mode_t auth) {
    if (auth == WIFI_AUTH_OPEN) {
        return "OPEN";
    }
    else if (auth == WIFI_AUTH_WEP) {
        return "WEP";
    }
    else {
        return "WPA";
    }
}


static uint8_t convert_string_to_2_digit_number(char *str) { // !: also returns 0 if invalid input
    uint8_t value = 0;
    if (str[0] >= '0' && str[0] <= '9') {
        value = str[0] - '0';
        if (str[1] >= '0' && str[1] <= '9') {
            value = value * 10 + (str[1] -'0');
        }
    }

    return value;
}


static uint8_t wait_for_input(char *input, uint8_t max_len, uint32_t input_delay, uint32_t total_timeout) {  // !: max_len is without '\0'
    uint8_t len = 0;
    char c;
    uint32_t delay = total_timeout;
    printf("\033[0m");
    while (true) {
        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(delay)) == 0) {
            if (len > 0) {
                input[len] = '\0';
                len++;
            }
            break;
        }
        else {
            if (c == '\n' || c == '\r') {
                if (len == 0) {
                    delay = input_delay;
                    continue;
                }
                input[len] = '\0';
                len++;
                break;
            }
            if (c == '\t' || c == ' ' || (c >= 1 && c <= 26 && c != 8)) {
                delay = input_delay;
                continue;
            }
            if (c == '\033') {
                char seq[3];
                if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, (unsigned char*)seq, 2, 100 / portTICK_PERIOD_MS) == 2) {
                    if (seq[0] == '[' && (seq[1] == 'A' || seq[1] == 'B' || seq[1] == 'C' || seq[1] == 'D')) {
                        delay = input_delay;
                        continue;
                    }
                }
            }
            if (c == 0x08 || c == 127) {
                if (len > 0) {
                    len--;
                    input[len] = '\0';
                    printf("%s\n", input);
                    printf("\033[A\033[2K");
                }
                delay = input_delay;
            }
            else {
                if (len < max_len) {
                    input[len] = c;
                    len++;
                    printf("%s\n", input);
                    printf("\033[A\033[2K");
                    delay = input_delay;
                }
            }
        }
    }

    printf("\033[36;1m");
    return len;
}


static int scan_command(int32_t argc, char** argv) {
    bool first_scan = true;
    uint64_t scan_pause = 0;
    uint64_t fflush_pause = 0;
    uint8_t prev_cnt = 0;

    if (arg_parse(argc, argv, (void **) &scan_args) != 0) {
        printf("Error parsing arguments\n");
        return HTOOL_UART_OK;
    }
    printf("Press \033[31;1many\033[36;1m key for stop scanning!\n");
    while (true) {
        if ((esp_timer_get_time() - scan_pause > 5000000) || first_scan) {
            if (first_scan) {
                if (scan_args.arg2->count) {// Passive
                    htool_api_start_passive_scan();
                }
                else {
                    htool_api_start_active_scan();
                }
                first_scan = false;
            }
            else {
                if (scan_args.arg1->count) { // Active
                    htool_api_start_active_scan();
                }
                else {
                    htool_api_start_passive_scan();
                }
            }
            scan_pause = esp_timer_get_time();
        }
        else {
            if ((prev_cnt == 0 && global_scans_count) || fflush_pause == 0 || esp_timer_get_time() - fflush_pause > 1000000) {
                for (uint8_t i = 0; i < prev_cnt; i++) {
                    printf("\033[A\033[K");
                }
                for (uint8_t i = 0; i < global_scans_count; i++) {
                    printf("\033[31;1m%hhu\033[36;1m - %s %d %s %02X:%02X:%02X:%02X:%02X:%02X ch: %u/%u\n", i + 1, global_scans[i].ssid, global_scans[i].rssi,
                           authtype_to_string(global_scans[i].authmode), global_scans[i].bssid[0], global_scans[i].bssid[1], global_scans[i].bssid[2], global_scans[i].bssid[3], global_scans[i].bssid[4],
                           global_scans[i].bssid[5], global_scans[i].second, global_scans[i].primary);
                }
                fflush_pause = esp_timer_get_time();
                prev_cnt = global_scans_count;
            }
        }
        char c;
        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(100))) {
            esp_wifi_scan_stop();
            break;
        }
    }

    return HTOOL_UART_OK;
}


static uint8_t scan_for_networs_and_wait_for_input(bool all) {
    bool first_scan = true;
    uint64_t scan_pause = 0;
    uint64_t fflush_pause = 0;
    uint8_t prev_cnt = 0;

    printf("Choose Network / Press \033[31;1m0\033[36;1m for exit\n");
    while (true) {
        if ((esp_timer_get_time() - scan_pause > 5000000) || first_scan) {
            if (first_scan) {
                htool_api_start_active_scan();
                first_scan = false;
            }
            else {
                htool_api_start_passive_scan();
            }
            scan_pause = esp_timer_get_time();
        }
        else {
            if ((prev_cnt == 0 && global_scans_count) || fflush_pause == 0 || esp_timer_get_time() - fflush_pause > 1000000) {
                for (uint8_t i = 0; i < prev_cnt; i++) {
                    printf("\033[A\033[K");
                }
                for (uint8_t i = 0; i < global_scans_count; i++) {
                    printf("\033[31;1m%hhu\033[36;1m - %s %d %s %02X:%02X:%02X:%02X:%02X:%02X ch: %u/%u\n", i + 1, global_scans[i].ssid, global_scans[i].rssi,
                           authtype_to_string(global_scans[i].authmode), global_scans[i].bssid[0], global_scans[i].bssid[1], global_scans[i].bssid[2], global_scans[i].bssid[3], global_scans[i].bssid[4],
                           global_scans[i].bssid[5], global_scans[i].second, global_scans[i].primary);
                }
                if (all && global_scans_count) {
                    printf ("\033[31;1m%hhu\033[36;1m - All\n", global_scans_count + 1);
                }
                fflush_pause = esp_timer_get_time();
                prev_cnt = all ? (global_scans_count ? global_scans_count + 1 : global_scans_count) : global_scans_count;
            }
        }
        char input[3] = {0};
        if (wait_for_input(input, 2, 3000, 100)) {
            uint8_t number;
            number = convert_string_to_2_digit_number(input);
            if (number == 0 || number <= prev_cnt) {
                if (number != 0) {
                    printf("Selected: \033[31;1m%s\033[36;1m\n", input);
                }
                esp_wifi_scan_stop();
                return number;
            }
            printf("\033[2K\n");
            printf("\033[A\033[2K");
        }
        else {
            printf("\033[36;1m");
        }
    }
}


static int deauth_command() {
    uint8_t input;
    while (true) {
        if ((input = scan_for_networs_and_wait_for_input(true))) {
            menu_cnt = input - 1; // TODO: change handling //--> remove menucount
            htool_api_start_deauther();
            printf("Press \033[31;1many\033[36;1m key for stopping deauth!\n");
            while (true) {
                char c;
                if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, portMAX_DELAY)) {
                    htool_api_stop_deauther();
                    printf("\033[A\033[2K\n");
                    break;
                }
            }
        }
        else {
            break;
        }
    }

    return HTOOL_UART_OK;
}


static int evil_twin_command() {
    char input[3] = {0};
    uint8_t ssid_input;
    uint8_t number;

    printf("(\033[31;1m0\033[36;1m) - General, (\033[31;1m1\033[36;1m) - Huawei, (\033[31;1m2\033[36;1m) - ASUS, (\033[31;1m3\033[36;1m) - TP-Link, (\033[31;1m4\033[36;1m) - Netgear, (\033[31;1m5\033[36;1m) - o2\n"
           "(\033[31;1m6\033[36;1m) - Fritzbox, (\033[31;1m7\033[36;1m) - Vodafone, (\033[31;1m8\033[36;1m) - Magenta, (\033[31;1m9\033[36;1m) - 1&1, (\033[31;1m10\033[36;1m) - A1, (\033[31;1m11\033[36;1m) - Globe,\n"
           "(\033[31;1m12\033[36;1m) - PLDT, (\033[31;1m13\033[36;1m) - AT&T, (\033[31;1m14\033[36;1m) - Swisscom, (\033[31;1m15\033[36;1m) - Verizon\n");
    while (true) {
        if ((ssid_input = scan_for_networs_and_wait_for_input(false))) {
            printf("Choose Captive Portal:\n");
            while (true) {
                if (wait_for_input(input, 2, 1000, -1)) {
                    number = convert_string_to_2_digit_number(input);
                    if (number <= 15) {
                        break;
                    }
                }
            }
            htool_api_start_evil_twin(ssid_input - 1, number);
            if (htool_api_is_evil_twin_running()) {
                printf("Press \033[31;1many\033[36;1m key for stop!\nUser entries:\n");
                char c;
                while (true) {
                    if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(200))) {
                        printf("\033[A\033[2K");
                        printf("\033[A\033[2K\033[A\n");
                        htool_wifi_reset_creds();
                        htool_api_stop_evil_twin();
                        break;
                    }
                    if (htool_wifi_get_user_cred_len()) {
                        printf("%s", htool_wifi_get_user_cred());
                    }
                    printf("\n\033[A\033[2K");
                }
            }
        }
        else {
            break;
        }
    }

    return HTOOL_UART_OK;
}


static int ble_spoof_command() {
    htool_api_ble_init();
    char input[3] = {0};
    printf("Spoof Apple, Google, Samsung, Microsoft Devices! Press '\033[31;1m0\033[36;1m' for exit\n");
    printf("(\033[31;1m1\033[36;1m) - Random cyclic of all brands,\n");
    printf("(\033[31;1m2\033[36;1m) - Apple Random cyclic, (\033[31;1m3\033[36;1m) - AirPods, (\033[31;1m4\033[36;1m) - AirPods Pro,\n");
    printf("(\033[31;1m5\033[36;1m) - AirPods Max, (\033[31;1m6\033[36;1m) - AirPods Gen2, (\033[31;1m7\033[36;1m) - AirPods Gen3,\n");
    printf("(\033[31;1m8\033[36;1m) - AirPods Pro Gen2, (\033[31;1m9\033[36;1m) - Power Beats, (\033[31;1m10\033[36;1m) - Power Beats Pro\n");
    printf("(\033[31;1m11\033[36;1m) - Beats Solo Pro, (\033[31;1m12\033[36;1m) - Beats Buds, (\033[31;1m13\033[36;1m) - Beats Flex, (\033[31;1m14\033[36;1m) - Beats X,\n");
    printf("(\033[31;1m15\033[36;1m) - Beats Solo3, (\033[31;1m16\033[36;1m) - Beats Studio3, (\033[31;1m17\033[36;1m) - Beats Studio Pro,\n");
    printf("(\033[31;1m18\033[36;1m) - Beats Fit Pro, (\033[31;1m19\033[36;1m) - Beats Buds Plus, (\033[31;1m20\033[36;1m) - Apple TV Setup,\n");
    printf("(\033[31;1m21\033[36;1m) - Apple TV Pair, (\033[31;1m22\033[36;1m) - Apple TV new User, (\033[31;1m23\033[36;1m) - Apple ID Setup,\n");
    printf("(\033[31;1m24\033[36;1m) - Apple TV Audio Sync, (\033[31;1m25\033[36;1m) - Apple TV Homekit, (\033[31;1m26\033[36;1m) - Apple TV Keyboard,\n");
    printf("(\033[31;1m27\033[36;1m) - Apple TV connect to Network, (\033[31;1m28\033[36;1m) - HomePod, (\033[31;1m29\033[36;1m) - Setup New Phone,\n");
    printf("(\033[31;1m30\033[36;1m) - Transfer Number, (\033[31;1m31\033[36;1m) - Apple TV Colorbalance, (\033[31;1m32\033[36;1m) - Google,\n");
    printf("(\033[31;1m33\033[36;1m) - Samsung Random cyclic, (\033[31;1m34\033[36;1m) - Samsung Watch4, (\033[31;1m35\033[36;1m) - Samsung French Watch4,\n");
    printf("(\033[31;1m36\033[36;1m) - Samsung Fox Watch5, (\033[31;1m37\033[36;1m) - Samsung Watch5, (\033[31;1m38\033[36;1m) - Samsung Watch5 Pro\n");
    printf("(\033[31;1m39\033[36;1m) - Samsung Watch6, (\033[31;1m40\033[36;1m) - Microsoft\n");

    while (true) {
        if (wait_for_input(input, 2, 1000, -1)) {
            uint8_t number;
            number = convert_string_to_2_digit_number(input);
            if (number <= 40) {
                if (number == 0) {
                    break;
                }
                else {
                    if (number == 1) {
                        number = 41; // adv_random
                    }
                    htool_api_set_ble_adv(number - 2);
                    htool_api_ble_start_adv();
                    char c;
                    printf("Press \033[31;1many\033[36;1m key for stopping spoofing\n");
                    while (true) {
                        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(200))) {
                            htool_api_ble_stop_adv();
                            printf("\033[A\033[2K\033[A\n");
                            break;
                        }
                        htool_api_set_ble_adv(number - 2);
                    }
                }
            }
            else {
                printf("Invalid Input!\n");
                printf("\033[A\033[2K");
            }
        }
    }
    htool_api_ble_stop_adv();
    htool_api_ble_deinit();

    return HTOOL_UART_OK;
}

static int network_tools_command() {
    uint8_t ssid_input;
    printf("Choose WiFi to connect to:\n");

    while (true) {
        bool pw_saved;
        char password[32];
        char ssid[15];
        uint8_t len;
        other_wifi:
        if ((ssid_input = scan_for_networs_and_wait_for_input(false))) {
            while (true) {
                strncpy(ssid, (char *)global_scans[ssid_input - 1].ssid, 15);
                if (global_scans[ssid_input - 1].authmode != WIFI_AUTH_OPEN) {
                    len = 15;
                    if ((nvsm_get_str(ssid, password, (size_t *)&len) < 0) || !len) {
                        reenter:
                        read_in_password(password, prompt, &len);
                        pw_saved = false;
                    }
                    else {
                        pw_saved = true;
                    }
                }
                else {
                    password[0] = '\0';
                    pw_saved = true;
                }
                htool_api_setup_station(ssid_input - 1, password);
                retry:
                if (htool_api_connect_to_wifi() != HTOOL_OK) {
                    printf("Connection Failed. Renter password (\033[31;1m1\033[36;1m) / Try again (\033[31;1m2\033[36;1m) / Choose other WiFi (\033[31;1m3\033[36;1m) / exit (\033[31;1m0\033[36;1m)\n");
                    char c;
                    while (true) {
                        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, portMAX_DELAY) == 0) {
                            continue;
                        }
                        if (c == '0') {
                            goto exit;
                        }
                        else if (c == '1') {
                            goto reenter;
                        }
                        else if (c == '2') {
                            goto retry;
                        }
                        else if (c == '3') {
                            goto other_wifi;
                        }
                        else {
                            printf("Invalid Input\n");
                        }
                    }
                }
                else {
                    break;
                }
            }
            if (!pw_saved) {
                printf("\033[36;1mSave Password? (\033[31;1my\033[36;1m / \033[31;1mn\033[36;1m)\n");
                char c;
                while (true) {
                    if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, portMAX_DELAY) == 0) {
                        continue;
                    }
                    if (c == 'y') {
                        nvsm_set_str(ssid, password);
                        break;
                    }
                    else if (c == 'n') {
                        break;
                    }
                    else {
                        printf("Invalid Input!\n");
                    }
                }
            }
            printf("Exit with typing '\033[31;1mexit\033[36;1m' or '\033[31;1m0\033[36;1m'\n");
            printf("proto: \033[31;1m(0)\033[36;1m- HTTP, \033[31;1m(1)\033[36;1m- ModBusTCP, \033[31;1m(2)\033[36;1m - UDP, \033[31;1m(3)\033[36;1m - ARP\n");
            printf("{\"\033[31;1mhost\033[36;1m\": \"IP\", \"\033[31;1mport\033[36;1m\": 80, \"\033[31;1mproto\033[36;1m\": 0, \"\033[31;1muri\033[36;1m\": \"/test\"}\n");
            printf("{\"\033[31;1mhost\033[36;1m\": \"IP\", \"\033[31;1mport\033[36;1m\": 502, \"\033[31;1mproto\033[36;1m\": 1, \"\033[31;1munit\033[36;1m\": 1, \"\033[31;1maddr\033[36;1m\": 30000, \"\033[31;1mregs\033[36;1m\": 2,\n\"\033[31;1mtype\033[36;1m\": (\033[31;1m0\033[36;1m - raw, \033[31;1m1\033[36;1m - values, \033[31;1m2\033[36;1m - string), \"\033[31;1mendian\033[36;1m\": (\033[31;1mfalse\033[36;1m - little, \033[31;1mtrue\033[36;1m - big)}\n");
            printf("{\"\033[31;1mhost\033[36;1m\": \"IP\", \"\033[31;1mport\033[36;1m\": 100, \"\033[31;1mproto\033[36;1m\": 2, \"\033[31;1mmessage\033[36;1m\": \"0x1223 / test\"}\n");
            printf("{\"\033[31;1mproto\033[36;1m\": 3, \"\033[31;1mstart\033[36;1m\": 1, \"\033[31;1mend\033[36;1m\": 100, \"\033[31;1mip\033[36;1m\": \"192.168.8.1\", \"\033[31;1mmac\033[36;1m\": \"12:ab:34:cd:56:ef\"}\n");
            while (true) {
                line = linenoise(prompt);
                if (line == NULL) {
                    continue;
                }
                if (strncmp(line, "exit", 4) == 0 || line[0] == '0') {
                    printf("\033[A\033[2K");
                    printf("\033[36;1m");
                    htool_wifi_disconnect();
                    break;
                }
                printf("\033[36;1m\n");
                linenoiseHistoryAdd(line);
                char *resp_json;
                uint32_t resp_len;
                htool_netman_handle_request(line, 1, &resp_json, &resp_len, print_cb);
            }
        }
        else {
            break;
        }
    }

    exit:
    htool_wifi_disconnect();
    htool_set_wifi_sta_config();
    return HTOOL_UART_OK;
}


static int clear_command() {
    printf("\033[H\033[J");
    printf("  ___ ___                __   .__             ___________           .__\n"
           " /   |   \\_____    ____ |  | _|__| ____    ___\\__    ___/___   ____ |  |\n"
           "/    ~    \\__  \\ _/ ___\\|  |/ /  |/    \\  / ___\\|    | /  _ \\ /  _ \\|  |\n"
           "\\    Y    // __ \\\\  \\___|    <|  |   |  \\/ /_/  >    |(  <_> |  <_> )  |__\n"
           " \\___|_  /(____  /\\___  >__|_ \\__|___|  /\\___  /|____| \\____/ \\____/|____/\n"
           "       \\/      \\/     \\/     \\/       \\//_____/\n"
           "___.            __   .__  _______  ._____.   .__\n"
           "\\_ |__ ___.__. |  | _|  | \\   _  \\ |__\\_ |__ |__|\n"
           " | __ <   |  | |  |/ /  | /  /_\\  \\|  || __ \\|  |\n"
           " | \\_\\ \\___  | |    <|  |_\\  \\_/   \\  || \\_\\ \\  |\n"
           " |___  / ____| |__|_ \\____/\\_____  /__||___  /__|\n"
           "     \\/\\/           \\/           \\/        \\/\n");
    printf("<=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=>\n\n");

    return HTOOL_UART_OK;
}


static int reboot_command() {
    printf("\033[H\033[J\n");
    esp_restart();
}


static int logout_command() {
    uint8_t rand = esp_random() % 10;
    cur_state = STATE_LOGIN;
    printf("\033[H\033[J");

    switch (rand) {
        case 0: print_animation("Logged out. Don't worry, I'll keep your seat warm!", 30); break;
        case 1: print_animation("Out of the digital world and back to reality. Good luck out there!", 30); break;
        case 2: print_animation("Logout successful. Time to get some sunshine!", 30); break;
        case 3: print_animation("Hasta la vista, baby! Come back soon!", 30); break;
        case 4: print_animation("You've successfully escaped! See you next time!", 30); break;
        case 5: print_animation("You've successfully logged out. Don't forget to come back for your daily dose of digital fun!", 30); break;
        case 6: print_animation("Log out complete. Remember, the real world doesn't have a 'Ctrl + Z'.", 30); break;
        case 7: print_animation("Beep boop! Human logged out. HackingTool will miss you.", 30); break;
        case 8: print_animation("Logged out. I will miss your warm hands.", 30); break;
        case 9: print_animation("Bye for now! Your virtual world is taking a quick nap.", 30); break;
        default: print_animation("Logged Out.", 30); break;
    }
    rand = esp_random() % 7;
    switch (rand) {
        case 0: print_animation("Zzz... Press any key to interrupt my beauty sleep. Proceed with caution!", 30); break;
        case 1: print_animation("If you dare, press a key. But be warned, I'm not a morning program!", 30); break;
        case 2: print_animation("Key press required. Disclaimer: I might wake up on the wrong side of the esp32 chip.", 30); break;
        case 3: print_animation("Press any key to wake me. I dream of software updates.", 30); break;
        case 4: print_animation("Hit any key to wake me up. But let's keep our expectations low until I've had my first byte of data.", 30); break;
        case 5: print_animation("Press key for waking me up. I'll sleep, but I'm warning you, I snore in binary ... 011010110101", 30); break;
        case 6: print_animation("Zzz... Please press any key. But do it gently, I'm dreaming of clouds shaped like fluffy unicorns...", 30); break;
        default: print_animation("Press any key for waking me up!", 30); break;
    }
    cur_state = STATE_LOGIN;

    return HTOOL_UART_OK;
}

static int creds_command(int32_t argc, char** argv) {
    char *string;
    if (arg_parse(argc, argv, (void **) &creds_change_args) != 0) {
        printf("Error parsing arguments\n");
        return HTOOL_UART_OK;
    }
    if ((!creds_change_args.arg1->count && !creds_change_args.arg2->count) || (creds_change_args.arg1->count)) {
        if (!creds_change_args.arg1->count) {
            print_animation("Change username? (\033[31;1my\033[36;1m / \033[31;1mn\033[36;1m)", 50);
            while (true) {
                char c;
                wait_for_input(&c, 1, 0, -1);
                if (c != 'y' && c != 'Y') {
                    goto password;
                }
                break;
            }
        }
        asprintf(&string, "Hey %s, I won't forget you. I'll just call you by your new name.", user);
        print_animation(string, 50);
        FREE_MEM(string);
        while (true) {
            line = linenoise(prompt);
            if (!line) {
                continue;
            }
            strncpy(user, line, sizeof(user));
            nvsm_set_str(USER_KEY, user);
            FREE_MEM(prompt);
            asprintf(&prompt, "\033[30;46;1m%s\033[39;44m>_\033[0m ", user);
            break;
        }
        printf("\033[36;1m");
        asprintf(&string, "Congrats! You've been successfully reborn as %s", user);
        print_animation(string, 80);
        FREE_MEM(string);
    }
    password:
    if ((!creds_change_args.arg1->count && !creds_change_args.arg2->count) || (creds_change_args.arg2->count)) {
        if (!creds_change_args.arg2->count) {
            print_animation("Change password? (\033[31;1my\033[36;1m / \033[31;1mn\033[36;1m)", 50);
            while (true) {
                char c;
                wait_for_input(&c, 1, 0, -1);
                if (c != 'y' && c != 'Y') {
                    return HTOOL_UART_OK;
                }
                break;
            }
        }
        print_animation("Get ready to conjure up a new password. Make it strong, like coffee, but not so strong that even you can't remember it!", 50);
        char password1[32];
        uint8_t len1;
        char password2[32];
        uint8_t len2;
        uint8_t i = 0;
        while (true) {
            if (i != 0) {
                print_animation("Try Again - Choose a new Password:", 50);
            }
            read_in_password(password1, prompt, &len1);
            printf("\033[36;1m");
            print_animation("Re-Enter Password:", 50);
            read_in_password(password2, prompt, &len2);
            printf("\033[36;1m");
            if (strncmp(password1, password2, sizeof(password1)) == 0) {
                strncpy(pw, password1, 32);
                break;
            }
            if (i == 0) {
                i++;
            }
        }
        nvsm_set_str(PW_KEY, pw);
        print_animation("Password updated! It's so secret, even I'm clueless", 50);
    }

    return HTOOL_UART_OK;
}

static int captive_portal_command() {
    char input[3] = {0};
    uint8_t number;

    printf("Choose Captive Portal by entering Number:\n"
           "(\033[31;1m1\033[36;1m) - Google, (\033[31;1m2\033[36;1m) - McDonald's, (\033[31;1m3\033[36;1m) - Facebook, (\033[31;1m4\033[36;1m) - Apple, (\033[31;1m0\033[36;1m) - exit\n");
    while (true) {
        if (htool_api_is_captive_portal_running()) {
            printf("Press \033[31;1many\033[36;1m key for stop!\nUser entries:\n");
            char c;
            while (true) {
                if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(200))) {
                    printf("\033[A\033[2K");
                    printf("\033[A\033[2K\033[A\n");
                    htool_wifi_reset_creds();
                    htool_api_stop_captive_portal();
                    break;
                }
                if (htool_wifi_get_user_cred_len()) {
                    printf("%s", htool_wifi_get_user_cred());
                }
                if (htool_wifi_get_pw_cred_len()) {
                    printf(" - %s", htool_wifi_get_pw_cred());
                }
                printf("\n\033[A\033[2K");
            }
        }
        while (true) {
            if (wait_for_input(input, 2, 1000, -1)) {
                number = convert_string_to_2_digit_number(input);
                if (number == 0) {
                    if (htool_api_is_captive_portal_running()) {
                        htool_api_stop_captive_portal();
                    }
                    return HTOOL_UART_OK;
                }
                if (number <= 4) {
                    break;
                }
            }
        }
        if (htool_api_is_captive_portal_running()) {
            htool_api_stop_captive_portal();
        }
        htool_api_start_captive_portal(number - 1);
    }
}


static int beacon_spammer_command(int32_t argc, char** argv) {
    printf("Choose Beacon Spammer by entering number:\n"
           "(\033[31;1m1\033[36;1m) - Random (fastest), (\033[31;1m2\033[36;1m) - SSID random MAC, (\033[31;1m3\033[36;1m) - SSID same MAC,\n"
           "(\033[31;1m4\033[36;1m) - Funny SSIDS, (\033[31;1m0\033[36;1m) - Exit\n");
   while (true) {
       if (htool_api_is_beacon_spammer_running()) {
           printf("Press \033[31;1many\033[36;1m key for stop!\n");
           char c;
           while (true) {
               if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, portMAX_DELAY)) {
                   htool_api_stop_beacon_spammer();
                   printf("\033[A\033[2K\033[A\n");
                   break;
               }
           }
       }
       else {
           char c[2] = {0};
           uint8_t number;
           wait_for_input(c, 1, 1000, -1);
           number = convert_string_to_2_digit_number(c);
           if (number == 0) {
               return HTOOL_UART_OK;
           }
           if (number <= 4) {
               if (number == 2 || number == 3) {
                   if (!(number = scan_for_networs_and_wait_for_input(true))) {
                       continue;
                   }
                   menu_cnt = number;
                   htool_api_start_beacon_spammer(1);
               }
               else if (number == 3) {
                   if (!(number = scan_for_networs_and_wait_for_input(true))) {
                       continue;
                   }
                   menu_cnt = number;
                   htool_api_start_beacon_spammer(2);
               }
               else {
                   htool_api_start_beacon_spammer(number - 1);
               }
           }
       }
   }

    return HTOOL_UART_OK;
}



static void uart_menu_task() {
    printf("\033[H\033[J");
    printf("\033[36;1m");

    while (true) {
        if (cur_state == STATE_LOGIN || cur_state == STATE_INITIAL_LOGIN) {
            if (nvsm_get_str(USER_KEY, user, &user_len) == ESP_FAIL || nvsm_get_str(PW_KEY, pw, &pw_len) == ESP_FAIL) {
                print_animation("Hey buddy, let's get to know each other! Please enter your username:", 50);
                char *string;
                while (true) {
                    line = linenoise("Username: ");
                    printf("\033[A\033[2K");
                    if (line == NULL) {
                        continue;
                    }
                    strncpy(user, line, 32);
                    asprintf(&prompt, "\033[30;46;1m%s\033[39;44m>_\033[0m ",user);
                    asprintf(&string, "Great to meet you, %s! Let's share a little secret – create a password known only to us:", user);
                    print_animation(string, 50);
                    free(string);
                    linenoiseFree(line);
                    break;
                }
                char password1[32];
                uint8_t len1;
                char password2[32];
                uint8_t len2;
                uint8_t i = 0;
                while (true) {
                    if (i != 0) {
                        print_animation("Try Again - Choose a new Password:", 50);
                    }
                    read_in_password(password1, prompt, &len1);
                    printf("\033[36;1m");
                    if (i != 0) {
                        print_animation("Re-Enter Password:", 50);
                    } else {
                        print_animation("The gateway to the hacking world is almost open... just re-enter your password:", 50);
                    }
                    read_in_password(password2, prompt, &len2);
                    printf("\033[36;1m");
                    if (strncmp(password1, password2, sizeof(password1)) == 0) {
                        strncpy(pw, password1, 32);
                        break;
                    }
                    if (i == 0) {
                        i++;
                    }
                }
                nvsm_set_str(USER_KEY, user);
                nvsm_set_str(PW_KEY, pw);
                printf("\033[36;1m");
                print_animation( "Time to have some fun :), but remember not to cross to the dark side.", 50);
                print_animation("And don't forget, there's no shame in using the 'help' command whenever you need it.", 50);
                print_animation("....", 1000);
            }
            else {
                if (cur_state == STATE_LOGIN) {
                    char c;
                    while (true) {
                        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, (unsigned char*)&c, 1, portMAX_DELAY) > 0) {
                            break;
                        }
                    }
                }
                char *string;
                char password[32];
                uint8_t len1;
                uint8_t rand = esp_random() % 21;
                switch (rand) {
                    case 0: asprintf(&string, "Look who's back! %s, enter your top-secret password and let's get this digital party started!", user); break;
                    case 1: asprintf(&string, "Ah, %s, you've returned! I've been so bored without you. Password, please, so we can stir up some fun!", user); break;
                    case 2: asprintf(&string, "Welcome back, %s! If you forgot your password, I'm not telling. Just kidding, type it in and let's roll!", user); break;
                    case 3: asprintf(&string, "%s, you're back! Hurry up with your password, my circuits are tingling with excitement!", user); break;
                    case 4: asprintf(&string, "Hey %s, did you miss me? Type your password so we can start our next digital adventure!", user); break;
                    case 5: asprintf(&string, "Yo, %s! I was just daydreaming in binary. Password, please, and let's make those dreams come true!", user); break;
                    case 6: asprintf(&string, "Guess who's back? %s! Quick, enter your password before I start telling bad tech jokes!", user); break;
                    case 7: asprintf(&string, "Oh, %s, you're like an upgrade to my system. Password, and let's compute some fun!", user); break;
                    case 8: asprintf(&string, "Welcome, %s! If you've forgotten your password, just pretend to reboot. Just kidding, go ahead and enter it!", user); break;
                    case 9: asprintf(&string, "Hey there, %s! Enter your password, and let's escape reality one byte at a time!", user); break;
                    case 10: asprintf(&string, "Hey %s, missed me? Enter your password - I promise I won't peek!", user); break;
                    case 11: asprintf(&string, "Welcome back, %s! Your password is the key... to our little digital secret world.", user); break;
                    case 12: asprintf(&string, "%s, you're here! Quick, type your password before I start tickling the keyboard!", user); break;
                    case 13: asprintf(&string, "Ah, %s! If I had a penny for every time I missed you... I'd need better financial management. Password, please!", user); break;
                    case 14: asprintf(&string, "Hello %s! Your password is like a magic spell - say it and poof! Fun appears!", user); break;
                    case 15: asprintf(&string, "Guess who's back? %s! Enter your password, and let's unlock some not-so-serious business.", user); break;
                    case 16: asprintf(&string, "Psst, %s! Your password is the secret handshake to our digital clubhouse. What's the password?", user); break;
                    case 17: asprintf(&string, "Oh, %s! I was just daydreaming about passwords. Care to share yours again?", user); break;
                    case 18: asprintf(&string, "Back again, %s? I was just polishing pixels. Password, and let's get them sparkling!", user); break;
                    case 19: asprintf(&string, "Welcome back, %s! If entering your password was a sport, I bet you'd be a champion!", user); break;
                    case 20: asprintf(&string, "Welcome back, %s, I missed you - please enter your password quickly so we can start having fun <3", user); break;
                    default: asprintf(&string, "Welcome back, %s, Enter your Password!", user);
                }
                if (prompt == NULL) {
                    asprintf(&prompt, "\033[30;46;1m%s\033[39;44m>_\033[0m ", user);
                }
                print_animation(string, 30);
                FREE_MEM(string);
                uint8_t i = 0;
                while (true) {
                    read_in_password(password, prompt,&len1);
                    printf("\033[36;1m");
                    if (strncmp(password, pw, 32) == 0) {
                        break;
                    }
                    else {
                        if (i < 10) {
                            print_animation("Oops! Wrong password!", 30);
                            i++;
                        }
                        else {
                            print_animation("Don't waste time, enter the right password so we can start having fun <3", 30);
                            i = 0;
                        }
                    }
                }
            }
            printf("\033[H\033[J");
            printf("  ___ ___                __   .__             ___________           .__\n"
                   " /   |   \\_____    ____ |  | _|__| ____    ___\\__    ___/___   ____ |  |\n"
                   "/    ~    \\__  \\ _/ ___\\|  |/ /  |/    \\  / ___\\|    | /  _ \\ /  _ \\|  |\n"
                   "\\    Y    // __ \\\\  \\___|    <|  |   |  \\/ /_/  >    |(  <_> |  <_> )  |__\n"
                   " \\___|_  /(____  /\\___  >__|_ \\__|___|  /\\___  /|____| \\____/ \\____/|____/\n"
                   "       \\/      \\/     \\/     \\/       \\//_____/\n"
                   "___.            __   .__  _______  ._____.   .__\n"
                   "\\_ |__ ___.__. |  | _|  | \\   _  \\ |__\\_ |__ |__|\n"
                   " | __ <   |  | |  |/ /  | /  /_\\  \\|  || __ \\|  |\n"
                   " | \\_\\ \\___  | |    <|  |_\\  \\_/   \\  || \\_\\ \\  |\n"
                   " |___  / ____| |__|_ \\____/\\_____  /__||___  /__|\n"
                   "     \\/\\/           \\/           \\/        \\/\n");
            printf("<=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=><=>\n\n");

            cur_state = STATE_RUNNING;
        }
        else {
            line = linenoise(prompt);
            if (line == NULL) {
                continue;
            }
            if (line[0] != '\0') {
                linenoiseHistoryAdd(line);
            }
            printf("\033[36;1m");
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unrecognized command\n");
            }
            else if (err == ESP_ERR_INVALID_ARG) {
            }
            else if (err == ESP_OK && ret != ESP_OK) {
                printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            }
            else if (err != ESP_OK) {
                printf("Internal error: %s\n", esp_err_to_name(err));
            }
            linenoiseFree(line); //TODO: check if everywhere done
        }
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
    vTaskDelete(NULL);
}


void htool_uart_cli_start() {
    xTaskCreatePinnedToCore(&uart_menu_task, "uart_menu_task", 4096, NULL, 5, NULL, 1);
}


void htool_uart_cli_init() {
    cur_state = STATE_INITIAL_LOGIN;
    fflush(stdout);
    fsync(fileno(stdout));
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .source_clk = UART_SCLK_REF_TICK,
    };
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
            .hint_color = atoi(LOG_COLOR_CYAN)
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );
    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);
    linenoiseHistorySetMaxLen(100);
    linenoiseAllowEmpty(false);
    esp_console_register_help_command();
    const esp_console_cmd_t captive_portal = {
            .command = "cportal",
            .help = "Starts a Captive Portal",
            .hint = NULL,
            .func = captive_portal_command,
    };
    const esp_console_cmd_t beacon_spammer = {
            .command = "beacon",
            .help = "Starts a Beacon Spammer",
            .hint = NULL,
            .func = beacon_spammer_command,
    };
    const esp_console_cmd_t clear = {
            .command = "clear",
            .help = "Clears Terminal",
            .hint = NULL,
            .func = clear_command,
    };
    const esp_console_cmd_t logout = {
            .command = "logout",
            .help = "Logs you out",
            .hint = NULL,
            .func = logout_command,
    };
    scan_args.arg1 = arg_lit0("a", "active",  "Active Scan");
    scan_args.arg2 = arg_lit0("p", "passive",  "Passive Scan");
    scan_args.end = arg_end(1);
    const esp_console_cmd_t scan = {
            .command = "scan",
            .help = "Starts a Scan",
            .hint = NULL,
            .func = scan_command,
            .argtable = &scan_args,
    };
    const esp_console_cmd_t reboot = {
            .command = "reboot",
            .help = "Reboots the HackingTool",
            .hint = NULL,
            .func = reboot_command,
    };
    const esp_console_cmd_t deauth = {
            .command = "deauth",
            .help = "De-authenticate networks",
            .hint = NULL,
            .func = deauth_command,
    };
    const esp_console_cmd_t evil_twin = {
            .command = "eviltwin",
            .help = "Starts Evil-Twin",
            .hint = NULL,
            .func = evil_twin_command,
    };
    const esp_console_cmd_t network_tools = {
            .command = "networktools",
            .help = "Choose between Network-Tools",
            .hint = NULL,
            .func = network_tools_command,
    };
    const esp_console_cmd_t ble_spoof = {
            .command = "blespoof",
            .help = "Spoof Devices over BLE",
            .hint = NULL,
            .func = ble_spoof_command,
    };
    creds_change_args.arg1 = arg_lit0("u", "username",  "Change Username");
    creds_change_args.arg2 = arg_lit0("p", "password",  "Change Password");
    creds_change_args.end = arg_end(1);
    const esp_console_cmd_t cred_change = {
            .command = "credschange",
            .help = "Change your login credentials",
            .hint = NULL,
            .func = creds_command,
            .argtable = &creds_change_args,
    };
    esp_console_cmd_register(&captive_portal);
    esp_console_cmd_register(&beacon_spammer);
    esp_console_cmd_register(&scan);
    esp_console_cmd_register(&clear);
    esp_console_cmd_register(&logout);
    esp_console_cmd_register(&reboot);
    esp_console_cmd_register(&deauth);
    esp_console_cmd_register(&evil_twin);
    esp_console_cmd_register(&network_tools);
    esp_console_cmd_register(&ble_spoof);
    esp_console_cmd_register(&cred_change);
}
