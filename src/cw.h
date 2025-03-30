/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <liquid/liquid.h>

void cw_init();

void cw_put_audio_samples(unsigned int n, cfloat *samples);
void cw_put_audio_int_samples(unsigned int n, int16_t *samples);

// bool cw_change_decoder(int16_t df);
// float cw_change_snr(int16_t df);
// float cw_change_peak_beta(int16_t df);
// float cw_change_noise_beta(int16_t df);

#ifdef __cplusplus
}
#endif
