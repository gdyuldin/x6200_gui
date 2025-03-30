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
#include <sqlite3.h>
#include <stdint.h>

extern sqlite3          *db;

bool database_init();

bool sql_query_exec(const char *sql);

void params_write_int(const char *name, int data, bool *dirty);
void params_write_int64(const char *name, uint64_t data, bool *dirty);
void params_write_float(const char *name, float data, bool *dirty);
void params_write_text(const char *name, const char *data, bool *dirty);
