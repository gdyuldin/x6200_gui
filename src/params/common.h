/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <stdbool.h>
#include <pthread.h>

extern pthread_mutex_t  params_mux;

void params_lock();
void params_unlock(bool *dirty);
bool params_ready_to_save();
