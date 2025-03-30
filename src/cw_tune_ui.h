/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once
#include <stdbool.h>
#include <lvgl/lvgl.h>

void cw_tune_init(lv_obj_t * parent);

// bool cw_tune_toggle(int16_t diff);

void cw_tune_set_freq(float hz);
