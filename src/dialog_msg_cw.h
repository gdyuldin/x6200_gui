/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "dialog.h"

#include "buttons.h"

#include "lvgl/lvgl.h"

extern dialog_t *dialog_msg_cw;

void dialog_msg_cw_append(uint32_t id, const char *val);
