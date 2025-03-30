/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <unistd.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

typedef bool (*textarea_window_cb_t)(void);

lv_obj_t * textarea_window_open(textarea_window_cb_t ok_cb, textarea_window_cb_t cancel_cb);
lv_obj_t * textarea_window_open_w_label(textarea_window_cb_t ok_cb, textarea_window_cb_t cancel_cb, const char * text);
void textarea_window_close();

const char* textarea_window_get();
void textarea_window_set(const char *text);

lv_obj_t * textarea_window_text();
