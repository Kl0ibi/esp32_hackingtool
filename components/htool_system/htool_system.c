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
#include <math.h>
#include <string.h>
#include "htool_system.h"

void htool_system_memcpy_reverse(void *d, void *s, uint8_t size) {
    unsigned char *pd = (unsigned char *) d;
    unsigned char *ps = (unsigned char *) s;

    ps += size;
    while (size--) {
        --ps;
        *pd++ = *ps;
    }
}


char *htool_system_escape_quotes(char *string, uint32_t size) {
    char *escaped_string;
    uint32_t num = 0;

    for (uint32_t i = 0; i < size; i++) {
        if (string[i] == '\n' || string[i] == '\t') {
            continue;
        }
        if (string[i] == '"') {
            num += 2;
            continue;
        }
        num ++;
    }
    escaped_string = malloc(num + 1);
    uint32_t j = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (string[i] == '\n' || string[i] == '\t') {
            continue;
        }
        if (string[i] == '"') {
            escaped_string[j++] = '\\';
        }
        escaped_string[j++] = string[i];
    }
    if (j > 0) {
        j--;
    }
    escaped_string[j] = '\0';

    return escaped_string;
}


/*
 !: array_size is 0 if string is NOT A HEX STRING (in this case nothing gets allocated)
 */
void htool_system_hex_string_to_byte_array(char *hex_string, uint8_t **byte_array, uint32_t *array_size) {
    uint32_t len;

    if ((len = strlen(hex_string)) > 2) {
        if (hex_string[0] == '0' && (hex_string[1] == 'x' || hex_string[1] == 'X')) {
            len -= 2; // - first 2 chars
            if (len % 2 != 0) {
                len++;
            }
            *array_size = len / 2;
            *byte_array = malloc(*array_size * sizeof(uint8_t));

            uint32_t j = 0;
            for (uint32_t i = 2; i <= len; i += 2) {
                sscanf(hex_string + i, "%2hhX", &(*byte_array)[j++]);
            }
            return;
        }
    }
    *array_size = 0;
}


uint32_t htool_system_extract_number_from_string(const char *str, uint16_t factor) {
    uint32_t temp_len;
    uint8_t len;
    uint8_t dot_position = 0;
    uint8_t before_pos;
    uint8_t after_pos;
    uint32_t number = 0;
    uint32_t fact = 1;
    uint16_t temp_factor;
    uint16_t factor_digit_cnt;

    if (str == NULL) {
        return 0;
    }
    temp_len = strlen(str);
    if (temp_len == 0) {
        return 0;
    }
    else if (temp_len > 64) {
        len = 64;
    }
    else {
        len = temp_len;
    }
    for (uint8_t i = 0; i < len; i++) {
        if (str[i] == '.') {
            if (i > 0 && i < len && str[i - 1] >= '0' && str[i - 1] <= '9' && str[i + 1] >= '0' && str[i + 1] <= '9') {
                dot_position = i;
                break;
            }
        }
    }
    if (dot_position != 0) {
        temp_factor = factor;
        factor_digit_cnt = 0;

        while (temp_factor != 0) {
            temp_factor /= 10;
            factor_digit_cnt++;
        }
        if (factor_digit_cnt != 0) {
            factor_digit_cnt--;
        }
        before_pos = dot_position - 1;
        while (before_pos > 0 && str[before_pos] >= '0' && str[before_pos] <= '9') {
            before_pos--;
        }
        after_pos = dot_position + 1;
        while (after_pos < len && str[after_pos] >= '0' && str[after_pos] <= '9') {
            after_pos++;
        }
        for (uint8_t i = after_pos - 1; i > before_pos; i--) {
            if (i > dot_position && i - dot_position >= 1) {
                if (i - dot_position > factor_digit_cnt || factor_digit_cnt == 0) {
                    continue;
                }
                else {
                    fact = (uint32_t)pow(10, (factor_digit_cnt - (i - dot_position)));
                }
            }
            else if (i == dot_position) {
                fact = factor;
                continue;
            }

            number += (str[i] - '0') * fact;
            fact *= 10;
        }
    }
    else {
        uint8_t found_cnt = 0;
        char temp_str[10];

        for (uint8_t i = 0; i < len; i++) {
            if (str[i] >= '0' && str[i] <= '9') {
                temp_str[found_cnt] = str[i];
                found_cnt++;
                if (found_cnt == 10) {
                    break;
                }
            }
            else if (found_cnt && str[i] < '0' && str[i] > '9') {
                break;
            }
        }
        if (found_cnt != 0) {
            if (found_cnt < 10) {
                temp_str[found_cnt] = '\0';
            }
            return strtol(temp_str, NULL, 10) * factor;
        }
    }

    return number;
}
