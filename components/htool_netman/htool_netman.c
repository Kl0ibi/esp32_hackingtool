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
#include <htool_api.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include "esp_log.h"
#include "htool_system.h"
#include "htool_wifi.h"
#include "htool_netman.h"
#include "htool_modbus.h"
#include "driver/uart.h"
#include "lwip/ip.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"

static const char TAG[] = "networkmanager";

#define MODBUS_HEADER_LEN 0x09
#define HTTP_MAX_RESP_BUFFER 4096
#define UDP_MAX_RESP_BUFFER 4096
#define MULTICAST_TTL 2
#define UDP_TIMEOUT_S 3
#define CONFIG_HTTP_CLIENT_USER_AGENT "HackingTool by kl0ibi"


typedef enum {
    NETMAN_STATUS_OK = 0,
    NETMAN_STATUS_ERR_PARSING_REQ = 1,
    NETMAN_STATUS_REQUIRED_INFO_MISSING = 2,
    NETMAN_STATUS_ERR_UNKNOWN_PROTO = 3,
    NETMAN_STATUS_ERR_NOT_REACHABLE = 4,
    NETMAN_STATUS_ERR_DATA_READ = 5,
    NETMAN_STATUS_STOPPED_BY_USER = 6,
    NETMAN_STATUS_NOT_FOUND_IP = 7,
    NETMAN_STATUS_NOT_FOUND_MAC = 8,
} netman_status;

typedef union {
    uint16_t uint16_t;
    int16_t int16_t;
    uint32_t uint32_t;
    int32_t int32_t;
    float float_t;
} value_union;

enum {
    NETMAN_PROTO_HTTP = 0,
    NETMAN_PROTO_MODBUS = 1,
    NETMAN_PROTO_UDP = 2,
    NETMAN_PROTO_ARP = 3,
};


static int32_t socket_send_msg(int socket, char *ip, uint16_t *port, char *msg) {
    int32_t rc;
    char addr_buf[32] = {0};
    struct addrinfo hints = {0};
    struct addrinfo *res;

    // Set fields and generate IP format for Multicast address and port
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET; // For an IPv4 socket
    if ((rc = getaddrinfo(ip, NULL, &hints, &res)) < 0) {
        ESP_LOGE(TAG, "getaddrinfo() failed for IPV4 destination address! Error: %d", errno);
        return -1;
    }
    if (res == 0) {
        ESP_LOGE(TAG, "getaddrinfo() did not return any addresses!");
        return -1;
    }
    ((struct sockaddr_in *) res->ai_addr)->sin_port = htons(*port);
    inet_ntoa_r(((struct sockaddr_in *) res->ai_addr)->sin_addr, addr_buf, sizeof(addr_buf) - 1);

    // Send discovery message to multicast address
    ESP_LOGI(TAG, "Sending to IPV4 multicast address %s:%d ...", addr_buf, *port);
    uint8_t *req_bytes;
    uint32_t req_byte_size;
    htool_system_hex_string_to_byte_array(msg, &req_bytes, &req_byte_size);
    if (req_byte_size > 0) {
        rc = sendto(socket, req_bytes, req_byte_size, 0, res->ai_addr, res->ai_addrlen);
        FREE_MEM(req_bytes);
    }
    else {
        rc = sendto(socket, msg, strlen(msg), 0, res->ai_addr, res->ai_addrlen);
    }
    freeaddrinfo(res); // Free previously filled address information
    if (rc < 0) {
        ESP_LOGE(TAG, "IPV4 sendto failed! Error: %d", errno);
        return -1;
    }

    return 0;
}


static int32_t socket_add_ipv4_multicast_group(int32_t sock, char *ip) {
    int32_t rc;
    struct ip_mreq m_request = {0};
    struct in_addr inet_addr = {0};
    esp_netif_ip_info_t ip_info = {0};

    // Configure source interface
    if ((rc = esp_netif_get_ip_info(htool_wifi_get_current_netif(), &ip_info)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", rc);
        goto err;
    }
    inet_addr_from_ip4addr(&inet_addr, &ip_info.ip);

    // Configure multicast address to listen to
    if ((rc = inet_aton(ip, &m_request.imr_multiaddr.s_addr)) != 1) {
        ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", ip);
        // Errors in the return value have to be negative
        rc = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(m_request.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(m_request.imr_multiaddr.s_addr))) {
        ESP_LOGW(TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. "
                      "This will probably not work.", ip);
    }

    // Assign the IPv4 multicast source interface, via its IP (only necessary if this socket is IPV4 only)
    if ((rc = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &inet_addr,
                         sizeof(struct in_addr))) < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
        goto err;
    }

    if ((rc = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m_request,
                         sizeof(struct ip_mreq))) < 0) {
        ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
        goto err;
    }

    err:
    return rc;
}


static int32_t create_multicast_ipv4_socket(char *ip, uint16_t *port) {
    int32_t rc;
    struct sockaddr_in sock_addr = {0};
    int32_t sock;

    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
        return -1;
    }

    // Bind the socket to any address
    sock_addr.sin_family = PF_INET;
    sock_addr.sin_port = htons(*port);
    sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((rc = bind(sock, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in))) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket! RC=%i", rc);
        goto err;
    }

    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
    if ((rc = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t))) < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL! RC=%i", rc);
        goto err;
    }

    // This is also a listening socket, so add it to the multicast group for listening...
    if ((rc = socket_add_ipv4_multicast_group(sock, ip)) < 0) {
        ESP_LOGE(TAG, "Failed to add socket to multicast group! RC=%i", rc);
        goto err;
    }

    // All set, socket is configured for sending and receiving
    return sock;

    err:
    close(sock);
    return -1;
}


static char *netman_status_enum_to_string (netman_status status) {
    switch (status) {
        case NETMAN_STATUS_OK:
            return "OK";
        case NETMAN_STATUS_ERR_PARSING_REQ:
            return "Parsing error";
        case NETMAN_STATUS_REQUIRED_INFO_MISSING:
            return "Required info missing";
        case NETMAN_STATUS_ERR_UNKNOWN_PROTO:
            return "Unknown protocol";
        case NETMAN_STATUS_ERR_NOT_REACHABLE:
            return "Host not reachable";
        case NETMAN_STATUS_ERR_DATA_READ:
            return "Data read error / Timeout";
        case NETMAN_STATUS_STOPPED_BY_USER:
            return "Stopped by User";
        case NETMAN_STATUS_NOT_FOUND_IP:
            return "Not found IP";
        case NETMAN_STATUS_NOT_FOUND_MAC:
            return "Not found MAC";
    }

    return "Unknown";
}


static bool is_broadcast_ip(const char *ip_address) {
    int a, b, c, d;
    if (sscanf(ip_address, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }

    return(d == 255);
}


void htool_netman_do_nothing() { // *: fix linker error

}


void htool_netman_handle_request(char *data, uint16_t len, char **response_json, uint32_t *resp_len, htool_netman_print_cb print) {
    cJSON *json = NULL;
    netman_status status;

    if (len > 0) {
        if ((json = cJSON_Parse(data)) != NULL) {
            cJSON *host;
            cJSON *port;
            cJSON *proto;

            if ((proto = cJSON_GetObjectItemCaseSensitive(json, "proto")) != NULL && !cJSON_IsNull(proto) && cJSON_IsNumber(proto)) {
            if (((host = cJSON_GetObjectItemCaseSensitive(json, "host")) != NULL && !cJSON_IsNull(host) && cJSON_IsString(host) && host->valuestring[0] != 0) || (proto->valueint == NETMAN_PROTO_ARP)) {
                if (((port = cJSON_GetObjectItemCaseSensitive(json, "port")) != NULL && !cJSON_IsNull(port) && cJSON_IsNumber(port) && port->valueint > 0 && port->valueint <= 0xFFFF) || (proto->valueint == NETMAN_PROTO_ARP)) {
                        if (proto->valueint == NETMAN_PROTO_HTTP) {
                            // *: {"host": "0.0.0.0", "port": 80, "user": "test", "pass": "123456", "proto": 0, "type": 1, "uri": "/api/v1/values.json?all&test=1"}
                            cJSON *user;
                            cJSON *pass;
                            cJSON *uri;
                            if ((uri = cJSON_GetObjectItemCaseSensitive(json, "uri")) != NULL && uri->valuestring[0] != '\0') {
                                esp_http_client_config_t config = {0};
                                config.host = host->valuestring;
                                config.port = port->valueint;
                                config.url = uri->valuestring;
                                config.timeout_ms = 3000;
                                config.user_agent = CONFIG_HTTP_CLIENT_USER_AGENT;
                                if ((user = cJSON_GetObjectItemCaseSensitive(json, "user")) != NULL && !cJSON_IsNull(user) && cJSON_IsString(user) && user->valuestring[0] != '\0') {
                                    config.auth_type = HTTP_AUTH_TYPE_BASIC;
                                    config.username = user->valuestring;
                                }
                                if ((pass = cJSON_GetObjectItemCaseSensitive(json, "pass")) != NULL && !cJSON_IsNull(pass) && cJSON_IsString(pass) && pass->valuestring[0] != '\0') {
                                    config.auth_type = HTTP_AUTH_TYPE_BASIC;
                                    config.password = pass->valuestring;
                                }
                                esp_http_client_handle_t client = esp_http_client_init(&config);
                                int32_t content_length;
                                if (esp_http_client_open(client, 0) == ESP_OK) {
                                    content_length = esp_http_client_fetch_headers(client);
                                    if (content_length > HTTP_MAX_RESP_BUFFER) {
                                        content_length = HTTP_MAX_RESP_BUFFER;
                                    }
                                    ESP_LOGD(TAG, "content len %u", content_length);
                                    if (content_length >= 0) {
                                        // *: {"status": 0, "uri": "/api/v1/values.json?all&test=1", "body": "{"key":"power_ac"}", "type": "1", "header": "HTTP1.1 Content-Length: 321", "data":"{"power_ac: 600"}", "http_status": 200, "ts": 1691654105}
                                        if (content_length != 0) {
                                            int32_t data_read;
                                            char *resp_str = malloc(content_length * sizeof(char)); //TODO: allow extern alloc?
                                            data_read = esp_http_client_read_response(client, resp_str, content_length);
                                            ESP_LOGD(TAG, "data read: %u", data_read);
                                            if (data_read > 0) {
                                                char *resp_escaped = htool_system_escape_quotes(resp_str, data_read);
                                                status = NETMAN_STATUS_OK;
                                                *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"uri:\":\"%s\",\"http_status\":%u,\"content_length\":%u,\"data\":\"%s\"}", netman_status_enum_to_string(status), uri->valuestring, esp_http_client_get_status_code(client), content_length, resp_escaped);
                                                print(*response_json);
                                                FREE_MEM(resp_escaped);
                                            }
                                            else {
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            esp_http_client_close(client);
                                            esp_http_client_cleanup(client);
                                            FREE_MEM(resp_str);
                                            goto exit;
                                        }
                                        else {
                                            int32_t data_read;
                                            char *resp_str = malloc(HTTP_MAX_RESP_BUFFER * sizeof(char));
                                            data_read = esp_http_client_read_response(client, resp_str, HTTP_MAX_RESP_BUFFER);
                                            if (data_read > 0) {
                                                status = NETMAN_STATUS_OK;
                                                char *resp_escaped = htool_system_escape_quotes(resp_str, data_read);
                                                *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"uri:\":\"%s\",\"http_status\":%u,\"content_length\":%u,\"data\":\"%s\"}", netman_status_enum_to_string(status), uri->valuestring, esp_http_client_get_status_code(client), content_length, resp_escaped);
                                                print(*response_json);
                                                FREE_MEM(resp_escaped);
                                            }
                                            else {
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            esp_http_client_close(client);
                                            esp_http_client_cleanup(client);
                                            FREE_MEM(resp_str);
                                            goto exit;
                                        }
                                    }
                                    esp_http_client_close(client);
                                }
                                esp_http_client_cleanup(client);
                                ESP_LOGE(TAG, "Http Connect failed");
                                status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                            }
                            else {
                                ESP_LOGE(TAG, "Required Info is missing");
                                status = NETMAN_STATUS_REQUIRED_INFO_MISSING;
                            }
                        }
                        else if (proto->valueint == NETMAN_PROTO_MODBUS) {
                            // *: {"host": "0.0.0.0", "port": 80, "proto": 1, "unit": 0, "addr": 0, "regs": 0}
                            cJSON *addr;
                            cJSON *regs;
                            cJSON *unit;
                            cJSON *type;
                            cJSON *con_delay;
                            cJSON *endian;
                            bool big_endian;
                            uint32_t delay;
                            if ((addr = cJSON_GetObjectItemCaseSensitive(json, "addr")) != NULL && !cJSON_IsNull(addr) && cJSON_IsNumber(addr)) {
                                if ((regs = cJSON_GetObjectItemCaseSensitive(json, "regs")) != NULL && !cJSON_IsNull(regs) && cJSON_IsNumber(regs) && regs->valueint != 0 && regs->valueint > 0 && regs->valueint <= 0x7B) {
                                    if ((unit = cJSON_GetObjectItemCaseSensitive(json, "unit")) != NULL && !cJSON_IsNull(unit) && cJSON_IsNumber(unit) && unit->valueint <= 0xF7) {
                                        if ((con_delay = cJSON_GetObjectItemCaseSensitive(json, "delay")) != NULL && !cJSON_IsNull(con_delay) && cJSON_IsNumber(con_delay)) {
                                            delay = con_delay->valueint;
                                        }
                                        else {
                                            delay = 0;
                                        }
                                        if ((endian = cJSON_GetObjectItemCaseSensitive(json, "endian")) != NULL && !cJSON_IsNull(endian) && (cJSON_IsBool(endian) || cJSON_IsNumber(endian))) {
                                            big_endian = endian->valueint;
                                        }
                                        else {
                                            big_endian = false;
                                        }
                                        int32_t sock;
                                        if ((sock = modbus_tcp_client_connect(host->valuestring, (uint16_t *)&port->valueint)) != MODBUS_ERR) {
                                            vTaskDelay(pdMS_TO_TICKS(delay));
                                            if ((type = cJSON_GetObjectItemCaseSensitive(json, "type")) == NULL || cJSON_IsNull(type) || !cJSON_IsNumber(type) || type->valueint == 0) {
                                                uint8_t *buf = calloc((regs->valueint * 2 + MODBUS_HEADER_LEN), sizeof(uint8_t)); // * 2 because 1 register has size of 16bits
                                                if (modbus_tcp_client_read_raw(sock, unit->valueint, addr->valueint, regs->valueint, buf) == MODBUS_OK) {
                                                    char *buf_hex_data = malloc( regs->valueint * 2 * 2 + 3); // + 3 is "0x" + \0
                                                    char *buf_hex_header = malloc( MODBUS_HEADER_LEN * 2 + 3); // + 3 is "0x" + \0
                                                    snprintf(buf_hex_data, 3, "0x");
                                                    snprintf(buf_hex_header, 3, "0x");
                                                    for (size_t i = 0; i < regs->valueint * 2; i++) {
                                                        snprintf(buf_hex_data + 2 + 2 * i, 3, "%02X", buf[(MODBUS_HEADER_LEN - 1) + (regs->valueint * 2) - i]);
                                                    }
                                                    for (size_t i = 0; i < MODBUS_HEADER_LEN; i++) {
                                                        snprintf(buf_hex_header + 2 + 2 * i, 3, "%02X", buf[(MODBUS_HEADER_LEN - 1) - i]);
                                                    }
                                                    status = NETMAN_STATUS_OK;
                                                    *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"unit:\":%hhu,\"addr\":%hu,\"regs\":%hhu,\"header\":\"%s\",\"data\":\"%s\"}", netman_status_enum_to_string(status), unit->valueint, addr->valueint, regs->valueint, buf_hex_header, buf_hex_data);
                                                    print(*response_json);
                                                    ESP_LOGI(TAG, "Successfully read data");

                                                    FREE_MEM(buf_hex_data);
                                                    FREE_MEM(buf_hex_header);
                                                    FREE_MEM(buf);
                                                    modbus_tcp_client_disconnect(sock);
                                                    goto exit;
                                                }
                                                else {
                                                    ESP_LOGE(TAG, "Error during reading data");
                                                    status = NETMAN_STATUS_ERR_DATA_READ;
                                                }
                                                FREE_MEM(buf);
                                            }
                                            else if (type->valueint == 1) {
                                                value_union values;
                                                if (modbus_tcp_client_read(sock, unit->valueint, addr->valueint, regs->valueint, big_endian, (uint8_t *)&values)) {
                                                    status = NETMAN_STATUS_OK;
                                                    *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"unit:\":%hhu,\"addr\":%hu,\"regs\":%hhu,\"values\":\"u16: %hu, i16: %hi, u32: %u, i32: %i, f: %f\"}", netman_status_enum_to_string(status), unit->valueint, addr->valueint, regs->valueint, values.uint16_t, values.int16_t, values.uint32_t, values.int32_t, values.float_t);
                                                    print(*response_json);
                                                    ESP_LOGI(TAG, "Successfully read data");
                                                    goto exit;
                                                }
                                                else {
                                                    ESP_LOGE(TAG, "Error during reading data");
                                                    status = NETMAN_STATUS_ERR_DATA_READ;
                                                }
                                            }
                                            else if (type->valueint == 2) {
                                                char *string = NULL;
                                                uint8_t length;
                                                if (modbus_tcp_client_read_str(sock, unit->valueint, addr->valueint, regs->valueint, big_endian, &string, &length) && length > 0 && string[0] != '\0') {
                                                    status = NETMAN_STATUS_OK;
                                                    *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"unit:\":%hhu,\"addr\":%hu,\"regs\":%hhu,\"string\":\"%s\"}", netman_status_enum_to_string(status), unit->valueint, addr->valueint, regs->valueint, string);
                                                    print(*response_json);
                                                    ESP_LOGI(TAG, "Successfully read data");
                                                    FREE_MEM(string);
                                                    goto exit;
                                                }
                                                else {
                                                    ESP_LOGE(TAG, "Error during reading data");
                                                    status = NETMAN_STATUS_ERR_DATA_READ;
                                                }
                                                FREE_MEM(string);
                                            }
                                            else {
                                                ESP_LOGE(TAG, "Unknown modbus type");
                                                status = NETMAN_STATUS_ERR_PARSING_REQ;
                                            }
                                        }
                                        else {
                                            ESP_LOGE(TAG, "Error during connect");
                                            status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        }
                                        modbus_tcp_client_disconnect(sock);
                                        goto exit;
                                    }
                                }
                            }
                            ESP_LOGE(TAG, "Required Info is missing");
                            status = NETMAN_STATUS_REQUIRED_INFO_MISSING;
                        }
                        else if (proto->valueint == NETMAN_PROTO_UDP) {
                            // *: {"host": "0.0.0.0", "port": 80, "proto": 2, "message": %s}
                            cJSON *message;
                            if ((message = cJSON_GetObjectItemCaseSensitive(json, "message")) != NULL && !cJSON_IsNull(message) && cJSON_IsString(message)) {
                                struct in_addr addr;
                                if (inet_pton(AF_INET, host->valuestring, &addr) <= 0) {
                                    ESP_LOGE(TAG, "Invalid IP");
                                    status = NETMAN_STATUS_ERR_PARSING_REQ;
                                    goto exit;
                                }
                                uint32_t ip = ntohl(addr.s_addr);
                                if (ip >= 0xE0000000U && ip <= 0xEFFFFFFFU) { // Multicast Range: 224.0.0.0 - 239.255.255.255
                                    ESP_LOGI(TAG, "Is Multicast IP");
                                    int32_t mc_sock = -1;
                                    struct sockaddr_in dest_addr = {0};
                                    struct timeval tv = {0};
                                    int32_t slc;
                                    uint64_t start_time;
                                    uint64_t end_time;

                                    start_time = esp_timer_get_time();
                                    if ((mc_sock = create_multicast_ipv4_socket(host->valuestring, (uint16_t *)&port->valueint)) < 0) {
                                        ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
                                        status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        goto exit;
                                    }
                                    // Set destination multicast addresses for sending from these sockets
                                    dest_addr.sin_family = PF_INET;
                                    dest_addr.sin_port = htons(port->valueint);
                                    inet_aton(host->valuestring, &dest_addr.sin_addr.s_addr);
                                    if (socket_send_msg(mc_sock, host->valuestring, (uint16_t *)&port->valueint, message->valuestring) != 0) {
                                        ESP_LOGE(TAG, "Unable to send message!");
                                        status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        shutdown(mc_sock, 0);
                                        close(mc_sock);
                                        goto exit;
                                    }
                                    uint32_t num_resp = 0;
                                    while (true) {
                                        end_time = start_time + (int64_t) (UDP_TIMEOUT_S * 1e+6);
                                        if (end_time < esp_timer_get_time()) {
                                            ESP_LOGE(TAG, "timeout");
                                            if (!num_resp)  {
                                                ESP_LOGI(TAG, "No response received");
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            shutdown(mc_sock, 0);
                                            close(mc_sock);
                                            goto exit;
                                        }
                                        tv.tv_sec = UDP_TIMEOUT_S;
                                        tv.tv_usec = 0;
                                        fd_set read_fds;
                                        FD_ZERO(&read_fds);
                                        FD_SET(mc_sock, &read_fds);
                                        // Wait until we receive data from socket connection or timeout
                                        slc = select(mc_sock + 1, &read_fds, NULL, NULL, &tv);
                                        if (slc < 0) {
                                            ESP_LOGE(TAG, "Select failed! Error: %d", errno);
                                            break;
                                        }
                                        else if (slc > 0) {
                                            // Check of socket already provided data to read based on the 'read_fds' variable
                                            if (FD_ISSET(mc_sock, &read_fds)) {
                                                char *response = malloc(UDP_MAX_RESP_BUFFER * sizeof(char));
                                                int32_t recv_len;
                                                struct sockaddr_in recv_addr = {0};
                                                char sender_addr[16] = {0};
                                                socklen_t sock_len = sizeof(recv_addr);

                                                if ((recv_len = recvfrom(mc_sock, response, UDP_MAX_RESP_BUFFER, 0, (struct sockaddr *)&recv_addr, &sock_len)) < 0 && errno != 11) {
                                                    ESP_LOGE(TAG, "Multicast recvfrom failed! Error: %d", errno);
                                                    status = NETMAN_STATUS_ERR_DATA_READ;
                                                    shutdown(mc_sock, 0);
                                                    close(mc_sock);
                                                    FREE_MEM(response);
                                                    goto exit;
                                                }
                                                if (recv_len > 0) {
                                                    if (recv_len < UDP_MAX_RESP_BUFFER) {
                                                        response[recv_len] = '\0';
                                                    }
                                                    if (recv_addr.sin_family == PF_INET) {
                                                        inet_ntoa_r(recv_addr.sin_addr, sender_addr, sizeof(sender_addr) - 1);
                                                    }
                                                    char *buf_hex_string = malloc( recv_len * 2 + 3); // + 3 is "0x" + \0
                                                    snprintf(buf_hex_string, 3, "0x");
                                                    for (size_t i = 0; i < recv_len; i++) {
                                                        snprintf(buf_hex_string + 2 + 2 * i, 3, "%02X", response[i]);
                                                    }
                                                    char *escaped_response = htool_system_escape_quotes(response, recv_len + 1);
                                                    ESP_LOGD(TAG, "Received response: %s\n", response);
                                                    status = NETMAN_STATUS_OK;
                                                    *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"data_string\":\"%s\",\"data_hex\":\"%s\",\"data_len\":%u,\"sender_addr\":\"%s\",\"port\":%u}", netman_status_enum_to_string(status), escaped_response, buf_hex_string, recv_len, sender_addr, ntohs(recv_addr.sin_port));
                                                    print(*response_json);
                                                    FREE_MEM(buf_hex_string);
                                                    FREE_MEM(escaped_response);
                                                    FREE_MEM(response);
                                                    num_resp++;
                                                    continue;
                                                }
                                                else {
                                                    ESP_LOGE(TAG, "No incoming data");
                                                    status = NETMAN_STATUS_ERR_DATA_READ;
                                                    shutdown(mc_sock, 0);
                                                    close(mc_sock);
                                                    FREE_MEM(response);
                                                    goto exit;
                                                }
                                            }
                                        }
                                        else {
                                            ESP_LOGI(TAG, "Timeout passed");
                                            if (!num_resp) {
                                                ESP_LOGW(TAG, "No incoming data");
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            shutdown(mc_sock, 0);
                                            close(mc_sock);
                                            goto exit;
                                        }
                                    }
                                }
                                else {
                                    ESP_LOGI(TAG, "Is Non Multicast IP");
                                    uint64_t start_time = esp_timer_get_time();
                                    uint64_t end_time;
                                    bool is_broadcast = is_broadcast_ip(host->valuestring);
                                    struct sockaddr_in dest_addr;
                                    dest_addr.sin_addr.s_addr = inet_addr(host->valuestring);
                                    dest_addr.sin_family = AF_INET;
                                    dest_addr.sin_port = htons(port->valueint);

                                    int32_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
                                    if (sock < 0) {
                                        ESP_LOGE(TAG, "Error creating socket");
                                        status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        goto exit;
                                    }
                                    if (is_broadcast) {
                                        int32_t v = 1;
                                        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &v, sizeof(v));
                                    }
                                    struct sockaddr_in dest_addr_ip4;
                                    dest_addr_ip4.sin_addr.s_addr = htonl(INADDR_ANY);
                                    dest_addr_ip4.sin_family = AF_INET;
                                    dest_addr_ip4.sin_port = htons(port->valueint);
                                    if (bind(sock, (const struct sockaddr *) &dest_addr_ip4, sizeof(dest_addr_ip4)) < 0) {
                                        ESP_LOGE(TAG, "Error binding socket");
                                        close(sock);
                                        status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        goto exit;
                                    }
                                    struct timeval timeout;
                                    timeout.tv_sec = UDP_TIMEOUT_S;
                                    timeout.tv_usec = 0;
                                    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                                    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                                    int32_t err;
                                    uint8_t *req_bytes;
                                    uint32_t req_byte_size;
                                    htool_system_hex_string_to_byte_array(message->valuestring, &req_bytes, &req_byte_size);
                                    if (req_byte_size > 0) {
                                        err = sendto(sock, req_bytes, req_byte_size, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                                        FREE_MEM(req_bytes);
                                    }
                                    else {
                                        err = sendto(sock, message->valuestring, strlen(message->valuestring), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                                    }
                                    if (err < 0) {
                                        ESP_LOGE(TAG, "Error sending UDP message");
                                        close(sock);
                                        status = NETMAN_STATUS_ERR_NOT_REACHABLE;
                                        goto exit;
                                    }
                                    uint32_t num_resp = 0;
                                    while (true) {
                                        end_time = start_time + (int64_t) (UDP_TIMEOUT_S * 1e+6);
                                        if (end_time < esp_timer_get_time()) {
                                            ESP_LOGE(TAG, "timeout");
                                            if (!num_resp) {
                                                ESP_LOGI(TAG, "No response received");
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            close(sock);
                                            goto exit;
                                        }
                                        struct sockaddr_in recv_addr = {0};
                                        char sender_addr[16] = {0};
                                        socklen_t socklen = sizeof(recv_addr);
                                        char *response = malloc(UDP_MAX_RESP_BUFFER * sizeof(char));
                                        int32_t recv_len;
                                        recv_len = recvfrom(sock, response, UDP_MAX_RESP_BUFFER - 1, 0, (struct sockaddr *)&recv_addr, &socklen);
                                        if (recv_len > 0) {
                                            if (recv_len < UDP_MAX_RESP_BUFFER) {
                                                response[recv_len] = '\0';
                                            }
                                            if (recv_addr.sin_family == PF_INET) {
                                                inet_ntoa_r(recv_addr.sin_addr, sender_addr, sizeof(sender_addr) - 1);
                                            }
                                            char *buf_hex_string = malloc( recv_len * 2 + 3); // + 3 is "0x" + \0
                                            snprintf(buf_hex_string, 3, "0x");
                                            for (size_t i = 0; i < recv_len; i++) {
                                                snprintf(buf_hex_string + 2 + 2 * i, 3, "%02X", response[i]);
                                            }
                                            char *escaped_string = htool_system_escape_quotes(response, recv_len + 1);
                                            ESP_LOGD(TAG, "Received response: %s\n", escaped_string);
                                            status = NETMAN_STATUS_OK;
                                            *resp_len = asprintf(response_json, "{\"status\":\"%s\",\"data_string\":\"%s\",\"data_hex\":\"%s\",\"data_len\":%u,\"sender_addr\":\"%s\",\"port\":%u}", netman_status_enum_to_string(status), escaped_string, buf_hex_string, recv_len, sender_addr, ntohs(recv_addr.sin_port));
                                            print(*response_json);
                                            FREE_MEM(escaped_string);
                                            FREE_MEM(buf_hex_string);
                                            FREE_MEM(response);
                                            num_resp++;
                                        }
                                        else {
                                            ESP_LOGI(TAG, "Timeout!");
                                            if (!num_resp) {
                                                ESP_LOGI(TAG, "No response received");
                                                status = NETMAN_STATUS_ERR_DATA_READ;
                                            }
                                            FREE_MEM(response);
                                            close(sock);
                                            goto exit;
                                        }
                                    }
                                }
                            }
                            status = NETMAN_STATUS_REQUIRED_INFO_MISSING;
                        }
                        else if (proto->valueint == NETMAN_PROTO_ARP) {
                            // *: {"proto": 5, start": 0, "end": 100} //TODO: add find and stop by finding mac or ip
                            uint8_t start_ip;
                            uint8_t end_ip;
                            cJSON *start;
                            cJSON *end;
                            cJSON *ip_search;
                            cJSON *mac_search;
                            bool search_ip = false;
                            bool search_mac = false;
                            uint8_t match_mac[6];
                            uint32_t match_ip;

                            //if ((message = cJSON_GetObjectItemCaseSensitive(json, "message")) != NULL && !cJSON_IsNull(message) && cJSON_IsString(message)) {
                            if ((start = cJSON_GetObjectItemCaseSensitive(json, "start")) == NULL || cJSON_IsNull(start) || !cJSON_IsNumber(start) || start->valueint < 1 || start->valueint > 255) {
                                start_ip = 1;
                            }
                            else {
                                start_ip = start->valueint;
                            }
                            if ((end = cJSON_GetObjectItemCaseSensitive(json, "end")) == NULL || cJSON_IsNull(end) || !cJSON_IsNumber(end) || end->valueint < 0 || end->valueint > 255) {
                                end_ip = 255;
                            }
                            else {
                                end_ip = end->valueint;
                            }
                            if ((ip_search = cJSON_GetObjectItemCaseSensitive(json, "ip")) == NULL || cJSON_IsNull(ip_search) || !cJSON_IsString(ip_search)) {
                                search_ip = false;
                            }
                            else {
                                search_ip = true;
                                if (inet_pton(AF_INET, ip_search->valuestring, &match_ip) != 1) {
                                    status = NETMAN_STATUS_ERR_PARSING_REQ;
                                    goto exit;
                                }
                            }
                            if ((mac_search = cJSON_GetObjectItemCaseSensitive(json, "mac")) == NULL || cJSON_IsNull(mac_search) || !cJSON_IsString(mac_search)) {
                                search_mac = false;

                            }
                            else {
                                search_mac = true;
                                uint8_t items_read = sscanf(mac_search->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                                    &match_mac[0], &match_mac[1], &match_mac[2], 
                                    &match_mac[3], &match_mac[4], &match_mac[5]);
                                if (items_read != 6) {
                                    status = NETMAN_STATUS_ERR_PARSING_REQ;
                                    goto exit;
                                }
                            }
                            printf("Stop by pressing any key!\n");
                            esp_netif_t *netif = htool_wifi_get_current_netif();
                            struct netif *netif_if = esp_netif_get_netif_impl(netif);
                            ip4_addr_t current_ip;
                            uint8_t cnt = 0;
                            char c;
                            for (uint16_t i = start_ip; i <= end_ip; i++) {
                                if (!search_ip) {
                                    IP4_ADDR(&current_ip, netif_if->ip_addr.u_addr.ip4.addr & 0xFF, (netif_if->ip_addr.u_addr.ip4.addr >> 8) & 0xFF, (netif_if->ip_addr.u_addr.ip4.addr >> 16) & 0xFF, i);
                                }
                                else {
                                    current_ip.addr = match_ip;
                                }
                                ESP_LOGD(TAG, "send mac request for ip %s", ip4addr_ntoa(&current_ip));
                                etharp_request(netif_if, &current_ip);
                                cnt++;
                                if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(500)) == 1) {
                                    status = NETMAN_STATUS_STOPPED_BY_USER;
                                    goto exit;
                                }
                                if (search_ip || cnt >= 10 || i == end_ip) { // arp table max size is 0b1010
                                    for (uint8_t x = 0; x <= cnt; x++) {
                                        struct eth_addr *eth_ret = NULL;
                                        const ip_addr_t *ip_addr_ret = NULL;
                                        if (!search_ip) {
                                            IP4_ADDR(&current_ip, netif_if->ip_addr.u_addr.ip4.addr & 0xFF, (netif_if->ip_addr.u_addr.ip4.addr >> 8) & 0xFF, (netif_if->ip_addr.u_addr.ip4.addr >> 16) & 0xFF, i - x);
                                        }
                                        else {
                                            current_ip.addr = match_ip;
                                        }
                                        ESP_LOGD(TAG, "read for ip: %s", ip4addr_ntoa(&current_ip));
                                        if (etharp_find_addr(netif_if, &current_ip, &eth_ret, (const ip4_addr_t **)&ip_addr_ret) >= 0) {
                                            if (search_mac || search_ip) {
                                                if ((search_ip && current_ip.addr == match_ip) || (search_mac && memcmp(eth_ret->addr, match_mac, 6) == 0)) {
                                                    status = NETMAN_STATUS_OK;
                                                    *resp_len = asprintf(response_json, "{\"status\":\"%s\", \"ip\": \"%s\", \"mac\": \"%.2X:%.2X:%.2X:%2.X:%.2X:%.2X\"}", netman_status_enum_to_string(status), ip4addr_ntoa(&current_ip), eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2], eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
                                                    print(*response_json);
                                                    goto exit;
                                                }
                                            }
                                            else {
                                                status = NETMAN_STATUS_OK;
                                                *resp_len = asprintf(response_json, "{\"status\":\"%s\", \"ip\": \"%s\", \"mac\": \"%.2X:%.2X:%.2X:%.2X:%.2X:%.2X\"}", netman_status_enum_to_string(status), ip4addr_ntoa(&current_ip), eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2], eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
                                                print(*response_json);
                                            }
                                        }
                                        else {
                                            if (search_ip && current_ip.addr == match_ip) {
                                                status = NETMAN_STATUS_NOT_FOUND_IP;
                                                goto exit;
                                            }
                                        }
                                    }
                                    cnt = 0;
                                }
                            }
                            if (search_ip) {
                                status = NETMAN_STATUS_NOT_FOUND_IP;
                            }
                            else if (search_mac) {
                                status = NETMAN_STATUS_NOT_FOUND_MAC;
                            }
                            else {
                                status = NETMAN_STATUS_OK;
                            }
                        }
                        else {
                            ESP_LOGE(TAG, "Unknown Protocol in Request");
                            status = NETMAN_STATUS_ERR_UNKNOWN_PROTO;
                        }
                        goto exit;
                    }
                }
            }
            ESP_LOGE(TAG, "Required Info is missing");
            status = NETMAN_STATUS_REQUIRED_INFO_MISSING;
        }
        else {
            ESP_LOGE(TAG, "Error during parsing json");
            status = NETMAN_STATUS_ERR_PARSING_REQ;
        }
    }
    else {
        ESP_LOGE(TAG, "No payload len");
        status = NETMAN_STATUS_ERR_PARSING_REQ;
    }

    exit:
    if (status != NETMAN_STATUS_OK) {
        *resp_len = asprintf(response_json, "{\"status\":\"%s\"}", netman_status_enum_to_string(status));
        print(*response_json);
    }
    if (json) {
        cJSON_Delete(json);
    }
}
