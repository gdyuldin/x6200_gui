/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "textarea_window.h"
#include "params/params.h"
#include "main_screen.h"
#include "qth/qth.h"
#include "msg.h"
#include "dialog.h"
#include "events.h"

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);

static dialog_t             dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = NULL,
    .key_cb = key_cb
};

dialog_t                    *dialog_qth = &dialog;

static bool edit_ok() {
    const char *qth = textarea_window_get();

    if (qth_grid_check(qth)) {
        params_str_set(&params.qth, qth);
    } else {
        msg_update_text_fmt("Incorrect QTH Grid");
    }

    dialog_destruct(&dialog);
    return true;
}

static bool edit_cancel() {
    dialog_destruct(&dialog);
    return true;
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = textarea_window_open(edit_ok, edit_cancel);

    lv_obj_t *text = textarea_window_text();

    lv_textarea_set_accepted_chars(text,
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    );

    lv_textarea_set_max_length(text, 6);
    lv_textarea_set_placeholder_text(text, "QTH Grid");
    lv_obj_add_event_cb(text, key_cb, LV_EVENT_KEY, NULL);

    textarea_window_set(params.qth.x);
}

static void destruct_cb() {
    textarea_window_close();
    dialog.obj = NULL;
}

static void key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct(&dialog);
            break;

        case LV_KEY_ENTER:
            edit_ok();
            dialog_destruct(&dialog);
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}
