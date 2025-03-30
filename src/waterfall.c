/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "waterfall.h"

#include "styles.h"
#include "radio.h"
#include "events.h"
#include "params/params.h"
#include "band_info.h"
#include "meter.h"
#include "backlight.h"
#include "dsp.h"
#include "util.h"
#include "pubsub_ids.h"
#include "scheduler.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define PX_BYTES    sizeof(lv_color_t)
#define DEFAULT_MIN S7
#define DEFAULT_MAX S9_40
#define WIDTH 800

typedef struct {
    uint8_t data[512];
    uint32_t width_hz;
    int32_t center_freq;
} waterfall_cache_row_t;

static lv_obj_t         *obj;
static lv_obj_t         *img;

static lv_style_t       middle_line_style;
static lv_obj_t         *middle_line;
static lv_point_t       middle_line_points[] = { {0, 0}, {0, 0} };

static lv_coord_t       height;
static int32_t         width_hz = 100000;

static float            grid_min = DEFAULT_MIN;
static float            grid_max = DEFAULT_MAX;

static lv_img_dsc_t     *frame;

// static int32_t          *freq_offsets;
static uint16_t               last_row_id;
static waterfall_cache_row_t *waterfall_cache;

static int32_t          radio_center_freq = 0;
static int32_t          wf_center_freq = 0;

static uint8_t          refresh_period = 1;
static uint8_t          refresh_counter = 0;


static void refresh_waterfall( void * arg);
static void draw_middle_line();
static void redraw_cb(lv_event_t * e);
static void on_fg_freq_change(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);
static void on_fft_width_changed(Subject *subj, void *user_data);


lv_obj_t * waterfall_init(lv_obj_t * parent) {
    subject_add_observer_and_call(cfg_cur.fg_freq, on_fg_freq_change, NULL);
    wf_center_freq = radio_center_freq;

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &waterfall_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    // Middle line style
    lv_style_init(&middle_line_style);
    lv_style_set_line_width(&middle_line_style, 1);
    lv_style_set_line_color(&middle_line_style, lv_color_hex(0xAAAAAA));
    lv_style_set_line_opa(&middle_line_style, LV_OPA_60);
    lv_style_set_blend_mode(&middle_line_style, LV_BLEND_MODE_ADDITIVE);

    subject_add_observer_and_call(cfg_cur.band->grid.min.val, on_grid_min_change, NULL);
    subject_add_observer_and_call(cfg_cur.band->grid.max.val, on_grid_max_change, NULL);
    subject_add_observer_and_call(cfg_cur.fft_width, on_fft_width_changed, NULL);
    return obj;
}

static void scroll_down() {
    last_row_id = (last_row_id + 1) % height;
}

void waterfall_data(float *data_buf, uint16_t size, bool tx) {
    scroll_down();

    float min, max;
    if (tx) {
        min = DEFAULT_MIN;
        max = DEFAULT_MAX;
    } else {
        min = grid_min;
        max = grid_max;
    }

    waterfall_cache[last_row_id].center_freq = radio_center_freq;
    waterfall_cache[last_row_id].width_hz = tx ? 48000 : width_hz;

    for (uint16_t x = 0; x < size; x++) {
        float       v = (data_buf[x] - min) / (max - min);

        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }

        uint8_t id = v * 255;
        waterfall_cache[last_row_id].data[x] = id;
    }
    scheduler_put_noargs(refresh_waterfall);
}

static void do_scroll_cb(lv_event_t * event) {
    if (wf_center_freq == radio_center_freq) {
        return;
    }
    if (params.waterfall_smooth_scroll.x) {
        wf_center_freq += (radio_center_freq - wf_center_freq) / 10 + 1;
    } else {
        wf_center_freq = radio_center_freq;
    }
    scheduler_put_noargs(refresh_waterfall);
}

void waterfall_set_height(lv_coord_t h) {
    lv_obj_set_height(obj, h);
    lv_obj_update_layout(obj);

    /* For more accurate horizontal scroll, it should be a "multiple of 500Hz" */
    /* 800 * 500Hz / 100000Hz = 4.0px */

    height = lv_obj_get_height(obj);

    frame = lv_img_buf_alloc(WIDTH, height, LV_IMG_CF_TRUE_COLOR);

    img = lv_img_create(obj);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(img, frame);

    last_row_id = 0;

    waterfall_cache = calloc(sizeof(waterfall_cache[0]), height);
    memset(waterfall_cache, 0, sizeof(waterfall_cache[0]) * height);
    for (size_t i = 0; i < height; i++) {
        waterfall_cache[i].center_freq = radio_center_freq;
        waterfall_cache[i].width_hz = width_hz;
    }

    lv_obj_add_event_cb(img, do_scroll_cb, LV_EVENT_DRAW_POST_END, NULL);

    waterfall_min_max_reset();
    band_info_init(obj);
    draw_middle_line();
}

static void middle_line_cb(lv_event_t * event) {
    if (params.waterfall_center_line.x && lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!params.waterfall_center_line.x && !lv_obj_has_flag(middle_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(middle_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
}

static void draw_middle_line() {
    middle_line_points[1].y = height;
    middle_line = lv_line_create(obj);
    lv_line_set_points(middle_line, middle_line_points, 2);
    lv_obj_add_style(middle_line, &middle_line_style, 0);
    lv_obj_center(middle_line);
    lv_obj_add_event_cb(obj, middle_line_cb, LV_EVENT_DRAW_POST_END, NULL);
}

void waterfall_min_max_reset() {
    if (params.waterfall_auto_min.x) {
        grid_min = DEFAULT_MIN;
    } else {
        grid_min = subject_get_int(cfg_cur.band->grid.min.val);
    }
    if (params.waterfall_auto_max.x) {
        grid_max = DEFAULT_MAX;
    } else {
        grid_max = subject_get_int(cfg_cur.band->grid.max.val);
    }
}

void waterfall_update_max(float db) {
    if (params.waterfall_auto_max.x) {
        lpf(&grid_max, db + 3.0f, 0.85f, DEFAULT_MAX);
    } else {
        // TODO: set min/max at param change
        grid_max = subject_get_int(cfg_cur.band->grid.max.val);
    }
}

void waterfall_update_min(float db) {
    if (params.waterfall_auto_min.x) {
        lpf(&grid_min, db + 3.0f, 0.95f, DEFAULT_MIN);
    } else {
        grid_min = subject_get_int(cfg_cur.band->grid.min.val);
    }
}

void waterfall_refresh_reset() {
    refresh_period = 1;
}

void waterfall_refresh_period_set(uint8_t k) {
    if (k == 0) {
        return;
    }
    refresh_period = k;
}

static void redraw_cb(lv_event_t * e) {
    int32_t src_x_offset;
    int16_t src_y, src_x0, dst_y, dst_x;

    lv_color_t black = lv_color_black();
    lv_color_t px_color;
    for (src_y = 0; src_y < height; src_y++) {
        dst_y = ((height - src_y + last_row_id) % height);

        int32_t left_freq = waterfall_cache[src_y].center_freq - waterfall_cache[src_y].width_hz / 2;
        int32_t right_freq = left_freq + waterfall_cache[src_y].width_hz;

        left_freq -= wf_center_freq;
        right_freq -= wf_center_freq;

        // hz to px
        left_freq = left_freq * WIDTH / width_hz + WIDTH / 2;
        right_freq = right_freq * WIDTH / width_hz + WIDTH / 2;
        uint16_t src_w = right_freq - left_freq;

        if ((right_freq < 0) || (left_freq > WIDTH)) {
            memset((lv_color_t *)frame->data + dst_y * WIDTH, 0, WIDTH * PX_BYTES);
        } else {
            for (dst_x = 0; dst_x < WIDTH; dst_x++) {
                uint16_t src_x = (dst_x - left_freq) * WATERFALL_NFFT / src_w;
                if ((src_x < 0) || (src_x >= WATERFALL_NFFT - 1)) {
                    px_color = black;
                } else {
                    uint8_t y_p = waterfall_cache[src_y].data[src_x];
                    px_color = (lv_color_t)wf_palette[y_p];
                }
                *((lv_color_t*)frame->data + (dst_y * WIDTH + dst_x)) = px_color;
            }
        }
    }
}

static void refresh_waterfall( void * arg) {
    refresh_counter++;
    if (refresh_counter >= refresh_period) {
        refresh_counter = 0;
        redraw_cb(NULL);
        lv_obj_invalidate(img);
    }
}

// static void on_fft_width_changed(Subject *subj, void *user_data) {
//     zoom = subject_get_int(subj);
//     lv_style_set_line_width(&middle_line_style, zoom / 2 + 2);
// }

static void on_fg_freq_change(Subject *subj, void *user_data) {
    radio_center_freq = subject_get_int(subj);
}

static void on_grid_min_change(Subject *subj, void *user_data) {
    if (!params.waterfall_auto_min.x) {
        grid_min = subject_get_int(subj);
    }
}
static void on_grid_max_change(Subject *subj, void *user_data) {
    if (!params.waterfall_auto_max.x) {
        grid_max = subject_get_int(subj);
    }
}

static void on_fft_width_changed(Subject *subj, void *user_data) {
    width_hz = subject_get_int(subj);
}
