/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "qth.h"

#include <math.h>
#include <string.h>
#include <ctype.h>


bool qth_grid_check(const char *grid) {
    uint8_t len = strlen(grid);

    switch (len) {
        case 8:
            if (grid[7] < '0' || grid[7] > '9') return false;
            if (grid[6] < '0' || grid[6] > '9') return false;
        case 6:
            if (toupper(grid[5]) < 'A' || toupper(grid[5]) > 'X') return false;
            if (toupper(grid[4]) < 'A' || toupper(grid[4]) > 'X') return false;
        case 4:
            if (grid[3] < '0' || grid[3] > '9') return false;
            if (grid[2] < '0' || grid[2] > '9') return false;
        case 2:
            if (toupper(grid[1]) < 'A' || toupper(grid[1]) > 'R') return false;
            if (toupper(grid[0]) < 'A' || toupper(grid[0]) > 'R') return false;
            break;

        default:
            return false;
    }

    return true;
}

double qth_pos_dist(const double lat1_deg, const double lon1_deg, const double lat2_deg, const double lon2_deg) {
    double lat1 = 0, lat2 = 0;
    double lon1 = 0, lon2 = 0;

    lat1 = lat1_deg * M_PI / 180.0;
    lat2 = lat2_deg * M_PI / 180.0;
    lon1 = lon1_deg * M_PI / 180.0;
    lon2 = lon2_deg * M_PI / 180.0;

    double dlat = lat1 - lat2;
    double dlon = lon1 - lon2;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) + cos(lat1) * cos(lat2) * sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    return c * 6371;
}

void qth_pos_to_str(double lat, double lon, char* buf) {

    int t1;

    if (180.001 < fabs(lon) ||
        90.001 < fabs(lat)) {
            strcpy(buf, "    n/a ");
    }

    if (179.99999 < lon) {
        lon = 179.99999;
    }

    lon += 180.0;
    t1 = (int)(lon / 20);
    buf[0] = (char)t1 + 'A';

    if ('R' < buf[0]) {
        buf[0] = 'R';
    }
    lon -= (float)t1 * 20.0;

    t1 = (int)lon / 2;
    buf[2] = (char)t1 + '0';
    lon -= (float)t1 * 2;

    lon *= 60.0;

    t1 = (int)(lon / 5);
    buf[4] = (char) ((char)t1 + 'a');
    lon -= (float)(t1 * 5);

    lon *= 60.0;
    t1 = (int)(lon / 30);

    if (9 < t1) {
        t1 = 9;
    }
    buf[6] = (char) ((char)t1 + '0');

    if (89.99999 < lat) {
        lat = 89.99999;
    }

    lat += 90.0;
    t1 = (int)(lat / 10.0);
    buf[1] = (char)t1 + 'A';

    if ('R' < buf[1]) {
        buf[1] = 'R';
    }
    lat -= (float)t1 * 10.0;

    buf[3] = (char)lat + '0';
    lat -= (int)lat;
    lat *= 60.0;

    t1 = (int)(lat / 2.5);
    buf[5] = (char)((char)t1 + 'a');
    lat -= (float)(t1 * 2.5);
    lat *= 60.0;

    t1 = (int)(lat / 15);

    if (9 < t1) {
        t1 = 9;
    }

    buf[7] = (char) ((char)t1 + '0');
    buf[8] = '\0';
}

void qth_str_to_pos(const char * grid, double *lat_deg, double *lon_deg) {
    uint8_t n = strlen(grid);

    *lon_deg = -180.0;
    *lat_deg = -90.0;

    *lon_deg += (toupper(grid[0]) - 'A') * 20.0;
    *lat_deg += (toupper(grid[1]) - 'A') * 10.0;

    if (n >= 4) {
        *lon_deg += (grid[2] - '0') * 2.0;
        *lat_deg += (grid[3] - '0') * 1.0;
    }

    if (n >= 6) {
        *lon_deg += (toupper(grid[4]) - 'A') * 5.0 / 60.0;
        *lat_deg += (toupper(grid[5]) - 'A') * 2.5 / 60.0;
    }

    if (n >= 8) {
        *lon_deg += (grid[6] - '0') * 5.0 / 600.0;
        *lat_deg += (grid[7] - '0') * 2.5 / 600.0;
    }

    switch (n) {
        case 2:
            *lon_deg += 20.0 / 2;
            *lat_deg += 10.0 / 2;
            break;

        case 4:
            *lon_deg += 2.0 / 2;
            *lat_deg += 1.0 / 2;
            break;

        case 6:
            *lon_deg += 5.0 / 60.0 / 2;
            *lat_deg += 2.5 / 60.0 / 2;
            break;

        case 8:
            *lon_deg += 5.0 / 600.0 / 2;
            *lat_deg += 2.5 / 600.0 / 2;
    }
}
