/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "keyboard.h"

#include "pubsub_ids.h"
#include "usb_devices.h"

#include "lv_drivers/indev/evdev.h"

#include <glob.h>


lv_group_t *keyboard_group;

static lv_indev_drv_t       indev_drv_2;
static bool                 ready = false;

static char* search_kbd_device() {
    glob_t globbuf;
    globbuf.gl_offs = 1;
    char *path = NULL;

    int res = glob("/dev/input/by-path/*-kbd", 0, NULL, &globbuf);
    if (res == 0) {
        path = globbuf.gl_pathv[0];
    }
    return path;
}

static void setup_kbd(char *path) {
    if (!evdev_set_file(path)) {
        return;
    }

    if (indev_drv_2.type == LV_INDEV_TYPE_NONE) {
        lv_indev_drv_init(&indev_drv_2);
        indev_drv_2.type = LV_INDEV_TYPE_KEYPAD;
        indev_drv_2.read_cb = evdev_read;

        lv_indev_t *keyboard_indev = lv_indev_drv_register(&indev_drv_2);

        lv_indev_set_group(keyboard_indev, keyboard_group);
    }

    ready = true;
}

static void on_usb_device_change(void * s, lv_msg_t * msg) {
    enum usb_devices_event_t event = (enum usb_devices_event_t)msg->payload;
    char *dev_path = search_kbd_device();
    if (dev_path && !ready) {
        setup_kbd(dev_path);
    } else if (!dev_path && ready) {
        ready = false;
    }
}

void keyboard_init() {
    keyboard_group = lv_group_create();

    lv_msg_subscribe(MSG_USB_DEVICE_CHANGED, on_usb_device_change, NULL);

    char *path = search_kbd_device();
    if (!path)
        return;

    setup_kbd(path);
}

bool keyboard_ready() {
    return ready;
}
