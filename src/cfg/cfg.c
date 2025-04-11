#include "cfg.private.h"

#include "atu.private.h"
#include "band.private.h"
#include "mode.private.h"
#include "params.private.h"
#include "transverter.private.h"
#include "memory.private.h"
#include "digital_modes.private.h"

#include "../lvgl/lvgl.h"
#include "../util.h"
#include <aether_radio/x6200_control/control.h>

#include <stdio.h>
#include <stdlib.h>

cfg_t cfg;

cfg_cur_t cfg_cur;

static band_info_t cur_band_info;

static int init_params_cfg(sqlite3 *db);
static void bind_observers();


static void *params_save_thread(void *arg);

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_item_change(Subject *subj, void *user_data);
static void on_vfo_change(Subject *subj, void *user_data);
// static void on_band_id_change(Subject *subj, void *user_data);
static void on_ab_freq_change(Subject *subj, void *user_data);
static void on_ab_mode_change(Subject *subj, void *user_data);
static void update_cur_low_filter(Subject *subj, void *user_data);
static void update_cur_high_filter(Subject *subj, void *user_data);
static void on_freq_step_change(Subject *subj, void *user_data);
static void on_zoom_change(Subject *subj, void *user_data);

static void on_bg_freq_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);
static void on_cur_filter_low_change(Subject *subj, void *user_data);
static void on_cur_filter_high_change(Subject *subj, void *user_data);
static void on_cur_filter_bw_change(Subject *subj, void *user_data);
static void on_cur_freq_step_change(Subject *subj, void *user_data);
static void on_cur_zoom_change(Subject *subj, void *user_data);


// #define TEST_CFG
#ifdef TEST_CFG
#include "test_cfg.c"
#include "cfg.h"
#endif

int cfg_init(sqlite3 *db) {
    int rc;

    rc = init_params_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading params");
        // return rc;
    }
    // rc = init_band_cfg(db);
    // if (rc != 0) {
    //     LV_LOG_ERROR("Error during loading band params");
    //     return rc;
    // }
    cfg_band_params_init(db);
    cfg_cur.band = &cfg_band;

    cfg_mode_params_init(db);
    // rc = init_mode_cfg(db);
    // if (rc != 0) {
    //     LV_LOG_ERROR("Error during loading mode params");
    //     return rc;
    // }

    cfg_atu_init(db);
    cfg_cur.atu = &atu_network;

    cfg_transverter_init(db);
    cfg_memory_init(db);
    cfg_digital_modes_init(db);

    bind_observers();

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);

#ifdef TEST_CFG
    run_tests();
#endif
    return rc;
}

const char *cfg_dnf_label_get() {
    switch (subject_get_int(cfg.dnf.val)) {
        case x6200_dnf_off:
            return "Off";
        case x6200_dnf_manual:
            return "Manual";
        case x6200_dnf_auto:
            return "Auto";
    }
    return "--";
}

/**
 * Delayed save of item
 */
static void on_item_change(Subject *subj, void *user_data) {
    cfg_item_t *item = (cfg_item_t *)user_data;
    pthread_mutex_lock(&item->dirty->mux);
    if (item->dirty->val != ITEM_STATE_LOADING) {
        item->dirty->val = ITEM_STATE_CHANGED;
        LV_LOG_INFO("Set dirty %s (pk=%i)", item->db_name, item->pk);
    }
    pthread_mutex_unlock(&item->dirty->mux);
}

/**
 * Changing of key tone
 */
static void on_key_tone_change(Subject *subj, void *user_data) {
    // int32_t key_tone = subject_get_int(subj);
    // if (cfg_cur.mode == NULL) {
    //     LV_LOG_USER("Skip update filters, cfg_cur.mode is not initialized");
    //     return;
    // }
    // x6200_mode_t db_mode = xmode_2_db(subject_get_int(cfg_cur.mode));
    // if (db_mode == x6200_mode_cw) {
    //     int32_t high, low, bw;
    //     bw   = subject_get_int(cfg_cur.filter.bw);
    //     low  = key_tone - bw / 2;
    //     high = low + bw;
    //     subject_set_int(cfg_cur.filter.high, high);
    //     subject_set_int(cfg_cur.filter.low, low);
    // }
}

/**
 * Changing fft width
 */
static void update_fft_width(Subject *subj, void *user_data) {
    subject_set_int(cfg_cur.fft_width, FFT_FULL_WIDTH / (1 << subject_get_int(subj)));
}

/**
 * Update sql_level value
 */
static void update_sql_level(Subject *subj, void *user_data) {
    switch (subject_get_int(cfg_cur.mode)) {
        case x6200_mode_nfm:
        case x6200_mode_wfm:
            subject_set_int(cfg_cur.sql_level, subject_get_int(cfg.sql_fm.val));
            break;
        default:
            subject_set_int(cfg_cur.sql_level, subject_get_int(cfg.sql.val));
            break;
    }
}

static void on_sql_level_change(Subject *subj, void *user_data) {
    switch (subject_get_int(cfg_cur.mode)) {
        case x6200_mode_nfm:
        case x6200_mode_wfm:
            subject_set_int(cfg.sql_fm.val, subject_get_int(subj));
            break;
        default:
            subject_set_int(cfg.sql.val, subject_get_int(subj));
            break;
    }
}

/**
 * Init cfg items
 */
void init_items(cfg_item_t *cfg_arr, uint32_t count, int (*load)(struct cfg_item_t *item),
                int (*save)(struct cfg_item_t *item)) {
    for (size_t i = 0; i < count; i++) {
        cfg_arr[i].load       = load;
        cfg_arr[i].save       = save;
        cfg_arr[i].dirty      = malloc(sizeof(*cfg_arr[i].dirty));
        cfg_arr[i].dirty->val = ITEM_STATE_CLEAN;
        pthread_mutex_init(&cfg_arr[i].dirty->mux, NULL);
        Observer *o = subject_add_observer(cfg_arr[i].val, on_item_change, &cfg_arr[i]);
    }
}
/**
 * Load items from db
 */
int load_items_from_db(cfg_item_t *cfg_arr, uint32_t count) {
    int rc;
    for (size_t i = 0; i < count; i++) {
        cfg_arr[i].dirty->val = ITEM_STATE_LOADING;
        rc = cfg_arr[i].load(&cfg_arr[i]);
        if (rc != 0) {
            LV_LOG_USER("Can't load %s (pk=%i)", cfg_arr[i].db_name, cfg_arr[i].pk);
        } else {

        }
        cfg_arr[i].dirty->val = ITEM_STATE_CLEAN;
    }
    return rc;
}

/**
 * Save items to db
 */

void save_item_to_db(cfg_item_t *item, bool force) {
    int rc;
    pthread_mutex_lock(&item->dirty->mux);
    if ((item->dirty->val == ITEM_STATE_CHANGED) || force) {
        rc = item->save(item);
        if (rc != 0) {
            LV_LOG_USER("Can't save %s (pk=%i)", item->db_name, item->pk);
        }
        item->dirty->val = ITEM_STATE_CLEAN;
    }
    pthread_mutex_unlock(&item->dirty->mux);
}

void save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size) {
    int rc;
    for (size_t i = 0; i < cfg_size; i++) {
        save_item_to_db(&cfg_arr[i], false);
    }
}

/**
 * Helpers for initialization
 */
void fill_cfg_item(cfg_item_t *item, Subject * val, const char * db_name) {
    item->db_name = strdup(db_name);
    item->val = val;
}

void fill_cfg_item_float(cfg_item_t *item, Subject * val, float db_scale, const char * db_name) {
    item->db_name = strdup(db_name);
    item->db_scale = db_scale;
    item->val = val;
}


/**
 * Save thread
 */
static void *params_save_thread(void *arg) {
    cfg_item_t *cfg_arr;
    cfg_arr           = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg) / sizeof(cfg_item_t);

    cfg_item_t *cfg_band_arr;
    cfg_band_arr           = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t *cfg_mode_arr;
    cfg_mode_arr           = (cfg_item_t *)&cfg_mode;
    uint32_t cfg_mode_size = sizeof(cfg_mode) / sizeof(cfg_item_t);

    cfg_item_t *cfg_transverter_arr;
    cfg_transverter_arr           = (cfg_item_t *)&cfg_transverters;
    uint32_t cfg_transverter_size = sizeof(cfg_transverters) / sizeof(cfg_item_t);

    while (true) {
        save_items_to_db(cfg_arr, cfg_size);
        save_items_to_db(cfg_band_arr, cfg_band_size);
        save_items_to_db(cfg_mode_arr, cfg_mode_size);
        save_items_to_db(cfg_transverter_arr, cfg_transverter_size);
        sleep_usec(10000000);
    }
}

/**
 * Initialization functions
 */
static int init_params_cfg(sqlite3 *db) {
    /* Init db modules */
    cfg_params_init(db);

    /* Fill configuration */
    fill_cfg_item(&cfg.vol, subject_create_int(20), "vol");
    fill_cfg_item(&cfg.sql, subject_create_int(0), "sql");
    fill_cfg_item(&cfg.sql_fm, subject_create_int(0), "sql_fm");
    fill_cfg_item_float(&cfg.pwr, subject_create_float(5.0f), 0.1f, "pwr");

    fill_cfg_item(&cfg.fft_dec, subject_create_int(0), "fft_dec");

    fill_cfg_item(&cfg.key_tone   , subject_create_int(700), "key_tone");
    fill_cfg_item(&cfg.band_id    , subject_create_int(5), "band");
    fill_cfg_item(&cfg.ant_id     , subject_create_int(1), "ant");
    fill_cfg_item(&cfg.atu_enabled, subject_create_int(false), "atu");

    fill_cfg_item(&cfg.key_speed, subject_create_int(15), "key_speed");
    fill_cfg_item(&cfg.key_mode, subject_create_int(x6200_key_manual), "key_mode");
    fill_cfg_item(&cfg.iambic_mode, subject_create_int(x6200_iambic_a), "iambic_mode");
    fill_cfg_item(&cfg.key_vol, subject_create_int(10), "key_vol");
    fill_cfg_item(&cfg.key_train, subject_create_int(false), "key_train");
    fill_cfg_item(&cfg.qsk_time, subject_create_int(100), "qsk_time");
    fill_cfg_item_float(&cfg.key_ratio, subject_create_float(3.0f), 0.1f, "key_ratio");

    /* CW decoder */
    fill_cfg_item(&cfg.cw_decoder, subject_create_int(true), "cw_decoder");
    fill_cfg_item(&cfg.cw_tune, subject_create_int(false), "cw_tune");
    fill_cfg_item_float(&cfg.cw_decoder_snr, subject_create_float(5.0f), 0.1f, "cw_decoder_snr_2");
    fill_cfg_item_float(&cfg.cw_decoder_snr_gist, subject_create_float(1.0f), 0.1f, "cw_decoder_snr_gist");
    fill_cfg_item_float(&cfg.cw_decoder_peak_beta, subject_create_float(0.10f), 0.01f, "cw_decoder_peak_beta");
    fill_cfg_item_float(&cfg.cw_decoder_noise_beta, subject_create_float(0.80f), 0.01f, "cw_decoder_noise_beta");

    //
    fill_cfg_item(&cfg.agc_hang, subject_create_int(false), "agc_hang");
    fill_cfg_item(&cfg.agc_knee, subject_create_int(-60), "agc_knee");
    fill_cfg_item(&cfg.agc_slope, subject_create_int(6), "agc_slope");

    fill_cfg_item(&cfg.comp, subject_create_int(false), "comp");

    // DSP
    fill_cfg_item(&cfg.dnf, subject_create_int(x6200_dnf_off), "dnf");
    fill_cfg_item(&cfg.dnf_center, subject_create_int(1000), "dnf_center");
    fill_cfg_item(&cfg.dnf_width, subject_create_int(50), "dnf_width");

    fill_cfg_item(&cfg.nb, subject_create_int(false), "nb");
    fill_cfg_item(&cfg.nb_level, subject_create_int(10), "nb_level");
    fill_cfg_item(&cfg.nb_width, subject_create_int(10), "nb_width");

    fill_cfg_item(&cfg.nr, subject_create_int(false), "nr");
    fill_cfg_item(&cfg.nr_level, subject_create_int(0), "nr_level");

    // SWR scan
    fill_cfg_item(&cfg.swrscan_linear, subject_create_int(true), "swrscan_linear");
    fill_cfg_item(&cfg.swrscan_span, subject_create_int(200000), "swrscan_span");

    // FT8
    fill_cfg_item(&cfg.ft8_hold_freq, subject_create_int(true), "ft8_hold_freq");

    // EQ
    fill_cfg_item(&cfg.eq.rx.en, subject_create_int(false), "eq_rx_en");
    fill_cfg_item(&cfg.eq.rx.p1, subject_create_int(0), "eq_rx_p1");
    fill_cfg_item(&cfg.eq.rx.p2, subject_create_int(0), "eq_rx_p2");
    fill_cfg_item(&cfg.eq.rx.p3, subject_create_int(0), "eq_rx_p3");
    fill_cfg_item(&cfg.eq.rx.p4, subject_create_int(0), "eq_rx_p4");
    fill_cfg_item(&cfg.eq.rx.p5, subject_create_int(0), "eq_rx_p5");

    fill_cfg_item(&cfg.eq.rx_wfm.en, subject_create_int(false), "eq_rx_wfm_en");
    fill_cfg_item(&cfg.eq.rx_wfm.p1, subject_create_int(0), "eq_rx_wfm_p1");
    fill_cfg_item(&cfg.eq.rx_wfm.p2, subject_create_int(0), "eq_rx_wfm_p2");
    fill_cfg_item(&cfg.eq.rx_wfm.p3, subject_create_int(0), "eq_rx_wfm_p3");
    fill_cfg_item(&cfg.eq.rx_wfm.p4, subject_create_int(0), "eq_rx_wfm_p4");
    fill_cfg_item(&cfg.eq.rx_wfm.p5, subject_create_int(0), "eq_rx_wfm_p5");

    fill_cfg_item(&cfg.eq.mic.en, subject_create_int(false), "eq_mic_en");

    fill_cfg_item(&cfg.eq.mic.p1, subject_create_int(0), "eq_mic_p1");
    fill_cfg_item(&cfg.eq.mic.p2, subject_create_int(0), "eq_mic_p2");
    fill_cfg_item(&cfg.eq.mic.p3, subject_create_int(0), "eq_mic_p3");
    fill_cfg_item(&cfg.eq.mic.p4, subject_create_int(0), "eq_mic_p4");
    fill_cfg_item(&cfg.eq.mic.p5, subject_create_int(0), "eq_mic_p5");


    cfg_cur.fft_width = subject_create_int(FFT_FULL_WIDTH);
    cfg_cur.sql_level = subject_create_int(0);
    /* Bind callbacks */
    // subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);
    subject_add_observer(cfg.key_tone.val, on_key_tone_change, NULL);
    subject_add_observer_and_call(cfg.fft_dec.val, update_fft_width, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr  = (cfg_item_t *)&cfg;
    uint32_t    cfg_size = sizeof(cfg) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size, cfg_params_load_item, cfg_params_save_item);
    return load_items_from_db(cfg_arr, cfg_size);
}

static void bind_observers() {
    subject_add_observer(cfg.sql.val, update_sql_level, NULL);
    subject_add_observer(cfg.sql_fm.val, update_sql_level, NULL);
    subject_add_observer_and_call(cfg_cur.mode, update_sql_level, NULL);
    subject_add_observer(cfg_cur.sql_level, on_sql_level_change, NULL);
}
