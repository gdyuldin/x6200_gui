#pragma once

#include "cfg.h"

#define TRANSVERTER_NUM 3

typedef struct {
    cfg_item_t from;
    cfg_item_t to;
    cfg_item_t shift;
} cfg_transverter_t;

extern cfg_transverter_t cfg_transverters[TRANSVERTER_NUM];

int32_t cfg_transverter_get_shift(int32_t freq);
