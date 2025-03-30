/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void qth_str_to_pos(const char * qth, double *lat_deg, double *lon_deg);
void qth_pos_to_str(double lat_deg, double lon_deg, char * qth);

bool qth_grid_check(const char *grid);
double qth_pos_dist(const double lat1_deg, const double lon1_deg, const double lat2_deg, const double lon2_deg);

