/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */


#include "common.h"
#include "../util.h"

#define PARAMS_SAVE_TIMEOUT  (3 * 1000)

pthread_mutex_t params_mux = PTHREAD_MUTEX_INITIALIZER;
static uint64_t params_mod_time = 0;

void params_lock() {
    pthread_mutex_lock(&params_mux);
}

void params_unlock(bool *dirty) {
    if (dirty != NULL) {
        *dirty = true;
    }
    if (!params_mod_time)
    {
        params_mod_time = get_time();
    }
    pthread_mutex_unlock(&params_mux);
}

bool params_ready_to_save() {
    if ((params_mod_time) && (get_time() - params_mod_time > PARAMS_SAVE_TIMEOUT)) {
        params_mod_time = 0;
        return true;
    }
    return false;
}
