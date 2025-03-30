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

lv_obj_t * msg_init(lv_obj_t *parent);

/// @brief Show or update message text
/// @param fmt
/// @param
void msg_update_text_fmt(const char * fmt, ...);


/// @brief Schedule message with text
/// @param fmt
/// @param
void msg_schedule_text_fmt(const char * fmt, ...);
