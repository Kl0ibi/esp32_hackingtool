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
#ifndef HTOOL_DISPLAY_H
#define HTOOL_DISPLAY_H

#include <stdbool.h>

uint8_t menu_cnt = 0;

bool scan_started = false;

bool deauth_all = false;

typedef enum {
    ST_MENU = 0,
    ST_SCAN,
    ST_DEAUTH,
    ST_BEACON,
    ST_C_PORTAL,
    ST_EVIL_TWIN,
    ST_STARTUP,
    ST_BEACON_SUBMENU,
    ST_EVIL_TWIN_SUBMENU,
}display_states;

bool htool_display_is_deauter_running();

bool htool_display_is_beacon_spammer_running();

void htool_display_init();

void htool_display_start();
#endif
