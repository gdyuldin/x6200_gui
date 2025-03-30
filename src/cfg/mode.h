#pragma once

#include "common.h"
#include <aether_radio/x6200_control/control.h>

#define MAX_FILTER_FREQ 10000

int32_t cfg_mode_change_freq_step(bool up);
int32_t cfg_mode_set_low_filter(int32_t val);
int32_t cfg_mode_set_high_filter(int32_t val);

const char * cfg_mode_agc_label(x6200_agc_t val);
