/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "msg.h"

#include "styles.h"
#include "util.h"
#include "events.h"

#include <stdio.h>
#include <stdlib.h>

#define FADE_TIME 250
#define DURATION 2000

static lv_obj_t     *obj;
static lv_timer_t   *fade_out_timer=NULL;
static lv_anim_t    fade;
static bool         fade_run = false;
static uint32_t     timer_end=0;

enum msg_type_t {
    MSG_UPDATE,
    MSG_SCHEDULE,
};

typedef struct {
    char            text[128];
    enum msg_type_t type;
} delayed_message_t;

static void fade_out_timer_cb(lv_timer_t *t) {
    lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_TRANSP);
    lv_anim_start(&fade);
    fade_run = true;
    fade_out_timer = NULL;
}

static void fade_anim(void * obj, int32_t v) {
    lv_obj_set_style_opa(obj, v, 0);
}

static void fade_ready(lv_anim_t * a) {
    fade_run = false;
}

static void msg_show_timer(lv_timer_t *t) {
    delayed_message_t * msg = t->user_data;
    if (fade_out_timer != NULL) {
        lv_timer_del(fade_out_timer);
    }
    lv_label_set_text(obj, msg->text);
    lv_obj_move_foreground(obj);
    lv_anim_set_values(&fade, lv_obj_get_style_opa(obj, 0), LV_OPA_COVER);
    fade_run = true;
    lv_anim_start(&fade);
    fade_out_timer = lv_timer_create(fade_out_timer_cb, DURATION - FADE_TIME, NULL);
    lv_timer_set_repeat_count(fade_out_timer, 1);
}

static void msg_update_cb(lv_event_t * e) {
    delayed_message_t *msg = (delayed_message_t *) lv_event_get_param(e);
    uint32_t delay;
    uint32_t cur_tick = lv_tick_get();
    if (timer_end < cur_tick) {
        timer_end = cur_tick;
    }
    if (msg->type == MSG_UPDATE) {
        delay = 0;
        timer_end = cur_tick;
    } else {
        delay = timer_end - cur_tick;
    }
    delayed_message_t *msg_copy = malloc(sizeof(delayed_message_t));
    *msg_copy = *msg;
    lv_timer_t *timer = lv_timer_create(msg_show_timer, delay, (void *)(msg_copy));
    lv_timer_set_repeat_count(timer, 1);
    timer_end += DURATION;
}

lv_obj_t * msg_init(lv_obj_t *parent) {
    obj = lv_label_create(parent);

    lv_obj_add_style(obj, &msg_style, 0);

    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(obj, msg_update_cb, EVENT_MSG_UPDATE, NULL);
    lv_label_set_recolor(obj, true);

    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_time(&fade, FADE_TIME);
    lv_anim_set_exec_cb(&fade, fade_anim);
    lv_anim_set_ready_cb(&fade, fade_ready);

    return obj;
}

void msg_update_text_fmt(const char * fmt, ...) {
    va_list args;

    delayed_message_t * msg = (delayed_message_t *) malloc(sizeof(delayed_message_t));
    msg->type = MSG_UPDATE;
    va_start(args, fmt);
    vsnprintf(msg->text, sizeof(msg->text), fmt, args);
    va_end(args);

    event_send(obj, EVENT_MSG_UPDATE, (void*)msg);
}

void msg_schedule_text_fmt(const char * fmt, ...) {
    va_list args;

    delayed_message_t * msg = (delayed_message_t *) malloc(sizeof(delayed_message_t));
    msg->type = MSG_SCHEDULE;
    va_start(args, fmt);
    vsnprintf(msg->text, sizeof(msg->text), fmt, args);
    va_end(args);

    event_send(obj, EVENT_MSG_UPDATE, (void*)msg);
}
