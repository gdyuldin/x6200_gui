/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2025 Georgy Dyuldin aka R2RFE
 */

#include "dialog_eq.h"

#include <array>

extern "C" {
    #include "events.h"
    #include "keyboard.h"
    #include "styles.h"
}

typedef enum {
    EQ_TYPE_RX,
    EQ_TYPE_RX_WFM,
    EQ_TYPE_MIC,
} eq_type_t;

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);

static char *rx_eq_en_label_getter();
static char *mic_eq_en_label_getter();

static void rx_eq_press_cb(button_item_t *item);
static void rx_eq_en_press_cb(button_item_t *item);
static void mic_eq_press_cb(button_item_t *item);
static void mic_eq_en_press_cb(button_item_t *item);
static void reset_press_cb(button_item_t *item);

static void slider_update_cb(lv_event_t * e);

static void on_mode_change(Subject * subj, void *user_data);
static void on_rx_en_change(Subject * subj, void *user_data);

static eq_type_t get_rx_eq_type();

class EQControl {
    eq_type_t                      eq_type = EQ_TYPE_RX;
    std::array<Subject *, 5>  values;
    std::array<Observer *, 5> value_observers;
    Subject                  *eq_en;

    static std::array<lv_obj_t *, 5> sliders;
    static std::array<lv_obj_t *, 5> labels;

  public:
    void set_type(eq_type_t t) {
        if ((t != eq_type) || !values[0]){
            cfg_eq_t *eq_cfg;

            eq_type = t;
            switch (eq_type) {
                case EQ_TYPE_MIC:
                    eq_cfg = &cfg.eq.mic;
                    break;
                case EQ_TYPE_RX_WFM:
                    eq_cfg = &cfg.eq.rx_wfm;
                    break;
                default:
                    eq_cfg = &cfg.eq.rx;
                    break;
            }
            clear_observers();

            eq_en = eq_cfg->en.val;
            values[0] = eq_cfg->p1.val;
            values[1] = eq_cfg->p2.val;
            values[2] = eq_cfg->p3.val;
            values[3] = eq_cfg->p4.val;
            values[4] = eq_cfg->p5.val;
            for (size_t i = 0; i < values.size(); i++) {
                value_observers[i] = values[i]->subscribe(on_control_change, (void *)i);
                value_observers[i]->notify();
            }
        }
    }

    void clear_observers() {
        for (auto observer : value_observers) {
            if (observer)
                delete observer;
        }
    }

    void set_slider_label(lv_obj_t *slider, lv_obj_t *label, uint8_t p) {
        sliders[p] = slider;
        labels[p]  = label;
    }
    void clear_sliders_labels() {
        sliders.fill(nullptr);
        labels.fill(nullptr);
    }
    int32_t get(uint8_t p) {
        return subject_get_int(values[p]);
    }
    void set(uint8_t p, int8_t val) {
        if (val != 0) {
            subject_set_int(eq_en, true);
        }
        subject_set_int(values[p], val);
    }
    void reset() {
        for (auto val : values) {
            subject_set_int(val, 0);
        }
    }

    eq_type_t get_eq_type() {
        return eq_type;
    }

    static void on_control_change(Subject *s, void *user_data) {
        uint8_t   i      = (uint32_t)user_data;
        lv_obj_t *slider = sliders[i];
        if (!slider) {
            return;
        }
        int32_t slider_val = lv_slider_get_value(slider);
        int32_t new_val    = subject_get_int(s);
        if (new_val != slider_val) {
            lv_slider_set_value(slider, new_val, LV_ANIM_OFF);
        }
        lv_label_set_text_fmt(labels[i], "%d", new_val);
    }
};

static EQControl controls;
std::array<lv_obj_t *, 5> EQControl::sliders;
std::array<lv_obj_t *, 5> EQControl::labels;

static button_item_t btn_rx_eq = {
    .type  = BTN_TEXT,
    .label = "RX EQ",
    .press = rx_eq_press_cb,
};
static button_item_t btn_rx_eq_en = {
    .type  = BTN_TEXT_FN,
    .label_fn = rx_eq_en_label_getter,
    .press = rx_eq_en_press_cb,
    .subj = &cfg_cur.mode
};
static button_item_t btn_mic_eq = {
    .type  = BTN_TEXT,
    .label = "MIC EQ",
    .press = mic_eq_press_cb,
};
static button_item_t btn_mic_eq_en = {
    .type  = BTN_TEXT_FN,
    .label_fn = mic_eq_en_label_getter,
    .press = mic_eq_en_press_cb,
    .subj = &cfg.eq.mic.en.val,
};
static button_item_t btn_reset = {
    .type  = BTN_TEXT,
    .label = "Reset",
    .press = reset_press_cb,
};

static buttons_page_t btn_page = {
    {
     &btn_rx_eq,
     &btn_rx_eq_en,
     &btn_mic_eq,
     &btn_mic_eq_en,
     &btn_reset,
     }
};

static dialog_t dialog = {
    .obj          = NULL,
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .audio_cb     = NULL,
    .rotary_cb    = NULL,
    .btn_page     = &btn_page,
    .prev_page    = NULL,
    .key_cb       = key_cb,
    .run          = false,
};


dialog_t *dialog_eq = &dialog;

static void construct_cb(lv_obj_t *parent) {
    eq_type_t rx_eq_type = get_rx_eq_type();
    controls.set_type(rx_eq_type);
    buttons_mark(&btn_rx_eq, true);

    lv_obj_t *obj = lv_obj_create(parent);
    dialog.obj = obj;

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), 200, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_style_grid_column_dsc_array(obj, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(obj, row_dsc, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
    lv_obj_set_size(obj, 600, 300);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_center(obj);

    lv_obj_set_layout(obj, LV_LAYOUT_GRID);

    lv_obj_add_event_cb(obj, key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(keyboard_group, obj);

    lv_obj_t *slider;
    lv_obj_t *label;
    const char *labels[] = {"300", "700", "1200", "1800", "2300"};
    for (size_t i = 0; i < 5; i++)
    {
        int32_t val = controls.get(i);
        label = lv_label_create(obj);
        lv_label_set_text_fmt(label, "%d", val);
        lv_obj_set_grid_cell(label, LV_GRID_ALIGN_CENTER, i, 1, LV_GRID_ALIGN_CENTER, 0, 1);

        slider = lv_slider_create(obj);
        lv_slider_set_range(slider, -10, 10);
        lv_slider_set_value(slider, val, LV_ANIM_OFF);
        lv_obj_set_size(slider, 10, 150);
        lv_obj_set_style_pad_all(slider, 0, 0);
        lv_obj_set_grid_cell(slider, LV_GRID_ALIGN_CENTER, i, 1, LV_GRID_ALIGN_CENTER, 1, 1);
        lv_obj_set_user_data(slider, (void*)i);
        lv_obj_add_event_cb(slider, slider_update_cb, LV_EVENT_VALUE_CHANGED, (void*)label);
        lv_group_add_obj(keyboard_group, slider);
        controls.set_slider_label(slider, label, i);

        label = lv_label_create(obj);
        lv_obj_set_style_pad_all(label, 0, 0);
        lv_label_set_text(label, labels[i]);
        lv_obj_set_grid_cell(label, LV_GRID_ALIGN_CENTER, i, 1, LV_GRID_ALIGN_CENTER, 2, 1);
    }

    subject_add_delayed_observer(cfg_cur.mode, on_mode_change, nullptr);

    subject_add_delayed_observer(cfg.eq.rx.en.val, on_rx_en_change, nullptr);
    subject_add_delayed_observer(cfg.eq.rx_wfm.en.val, on_rx_en_change, nullptr);

    // TODO: switch rx en button depending on mode

    // w = 780;
    // h = 330;

    // lv_obj_add_event_cb(chart, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    // lv_obj_set_size(chart, w, h);
    // lv_obj_center(chart);

    // lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    // lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
}

static void destruct_cb() {
    controls.clear_observers();
    controls.clear_sliders_labels();
}


static void key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct();
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

static char* rx_eq_en_label_getter() {
    static char buf[22];
    if (subject_get_int(cfg_cur.mode) == x6100_mode_wfm) {
        sprintf(buf, "RX EQ:\n%s", subject_get_int(cfg.eq.rx_wfm.en.val) ? "On": "Off");

    } else {
        sprintf(buf, "RX EQ:\n%s", subject_get_int(cfg.eq.rx.en.val) ? "On": "Off");
    }
    return buf;
}

static char* mic_eq_en_label_getter() {
    static char buf[22];
    sprintf(buf, "MIC EQ:\n%s", subject_get_int(cfg.eq.mic.en.val) ? "On": "Off");
    return buf;
}


static void rx_eq_press_cb(button_item_t *item) {
    controls.set_type(get_rx_eq_type());
    buttons_mark(&btn_mic_eq, false);
    buttons_mark(item, true);
}

static void rx_eq_en_press_cb(button_item_t *item) {
    Subject *subj;
    if (subject_get_int(cfg_cur.mode) == x6100_mode_wfm) {
        subj = cfg.eq.rx_wfm.en.val;
    } else {
        subj = cfg.eq.rx.en.val;
    }
    subject_set_int(subj, !subject_get_int(subj));
    buttons_refresh(item);
}

static void mic_eq_press_cb(button_item_t *item) {
    controls.set_type(EQ_TYPE_MIC);
    buttons_mark(&btn_rx_eq, false);
    buttons_mark(item, true);
}

static void mic_eq_en_press_cb(button_item_t *item) {
    subject_set_int(cfg.eq.mic.en.val, !subject_get_int(cfg.eq.mic.en.val));
}

static void reset_press_cb(button_item_t *item) {
    controls.reset();
}

static void slider_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    size_t p = (size_t)lv_obj_get_user_data(obj);
    int32_t val = lv_slider_get_value(obj);
    controls.set(p, val);
}

static void on_mode_change(Subject * subj, void *user_data) {
    if (controls.get_eq_type() != EQ_TYPE_MIC) {
        controls.set_type(get_rx_eq_type());
    }
}

void on_rx_en_change(Subject *subj, void *user_data) {
    buttons_refresh(&btn_rx_eq_en);
}

eq_type_t get_rx_eq_type() {
    if (subject_get_int(cfg_cur.mode) == x6100_mode_wfm) {
        return EQ_TYPE_RX_WFM;
    } else {
        return EQ_TYPE_RX;
    }
}
