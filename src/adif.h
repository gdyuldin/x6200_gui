/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */
#pragma once

#include "qso_log.h"

#include <time.h>

typedef struct adif_log_s *adif_log;

adif_log  adif_log_init(const char * path);

void adif_log_close(adif_log l);

void adif_add_qso(adif_log l, qso_log_record_t qso);

int adif_read(const char * path, qso_log_record_t ** records);
