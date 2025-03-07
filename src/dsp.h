/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"

#ifdef __cplusplus

#include "cfg/subjects.h"
#include <array>

extern "C" {
#endif

  #include "radio.h"
  #include <stdbool.h>
  #include <stdint.h>
  #include <stdlib.h>
  #include <liquid/liquid.h>

#ifdef __cplusplus
}
#endif

#define WATERFALL_NFFT  512
#define SPECTRUM_NFFT   800

#ifdef __cplusplus

extern "C" {
#endif

void dsp_init();
void dsp_samples(float *buf_samples, uint16_t size, bool tx, int16_t dbm);
void dsp_reset();

float dsp_get_spectrum_beta();
void dsp_set_spectrum_beta(float x);

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);
#ifdef __cplusplus
}
#endif
