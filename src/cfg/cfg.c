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
#include <aether_radio/x6100_control/control.h>

#include <stdio.h>
#include <stdlib.h>

cfg_t cfg;

cfg_cur_t cfg_cur;

static band_info_t cur_band_info;

static int init_params_cfg(sqlite3 *db);
// static int init_band_cfg(sqlite3 *db);
// static int init_mode_cfg(sqlite3 *db);


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

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);

#ifdef TEST_CFG
    run_tests();
#endif
    return rc;
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
    int32_t key_tone = subject_get_int(subj);
    if (cfg_cur.mode == NULL) {
        LV_LOG_USER("Skip update filters, cfg_cur.mode is not initialized");
        return;
    }
    x6100_mode_t db_mode = xmode_2_db(subject_get_int(cfg_cur.mode));
    if (db_mode == x6100_mode_cw) {
        int32_t high, low, bw;
        bw   = subject_get_int(cfg_cur.filter.bw);
        low  = key_tone - bw / 2;
        high = low + bw;
        subject_set_int(cfg_cur.filter.high, high);
        subject_set_int(cfg_cur.filter.low, low);
    }
}

/**
 * Changing fft width
 */
static void update_fft_width(Subject *subj, void *user_data) {
    subject_set_int(cfg_cur.fft_width, FFT_FULL_WIDTH / (1 << subject_get_int(subj)));
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
    cfg.vol = (cfg_item_t){.val = subject_create_int(20), .db_name = "vol"};
    cfg.sql = (cfg_item_t){.val = subject_create_int(0), .db_name = "sql"};
    cfg.pwr = (cfg_item_t){.val = subject_create_float(5.0f), .db_scale = 0.1f, .db_name = "pwr"};

    cfg.fft_dec = (cfg_item_t){.val = subject_create_int(1), .db_name = "fft_dec"};

    cfg.key_tone    = (cfg_item_t){.val = subject_create_int(700), .db_name = "key_tone"};
    cfg.band_id     = (cfg_item_t){.val = subject_create_int(5), .db_name = "band"};
    cfg.ant_id      = (cfg_item_t){.val = subject_create_int(1), .db_name = "ant"};
    cfg.atu_enabled = (cfg_item_t){.val = subject_create_int(false), .db_name = "atu"};

    cfg.key_speed = (cfg_item_t){.val = subject_create_int(15), .db_name="key_speed"};
    cfg.key_mode = (cfg_item_t){.val = subject_create_int(x6100_key_manual), .db_name="key_mode"};
    cfg.iambic_mode = (cfg_item_t){.val = subject_create_int(x6100_iambic_a), .db_name="iambic_mode"};
    cfg.key_vol = (cfg_item_t){.val = subject_create_int(10), .db_name="key_vol"};
    cfg.key_train = (cfg_item_t){.val = subject_create_int(false), .db_name="key_train"};
    cfg.qsk_time = (cfg_item_t){.val = subject_create_int(100), .db_name="qsk_time"};
    cfg.key_ratio = (cfg_item_t){.val = subject_create_float(3.0f), .db_scale=0.1f, .db_name="key_ratio"};

    /* CW decoder */
    cfg.cw_decoder = (cfg_item_t){.val = subject_create_int(true), .db_name="cw_decoder"};
    cfg.cw_tune = (cfg_item_t){.val = subject_create_int(false), .db_name="cw_tune"};
    cfg.cw_decoder_snr = (cfg_item_t){.val = subject_create_float(5.0f), .db_scale=0.1f, .db_name="cw_decoder_snr_2"};
    cfg.cw_decoder_snr_gist = (cfg_item_t){.val = subject_create_float(1.0f), .db_scale=0.1f, .db_name="cw_decoder_snr_gist"};
    cfg.cw_decoder_peak_beta = (cfg_item_t){.val = subject_create_float(0.10f), .db_scale=0.01f, .db_name="cw_decoder_peak_beta"};
    cfg.cw_decoder_noise_beta = (cfg_item_t){.val = subject_create_float(0.80f), .db_scale=0.01f, .db_name="cw_decoder_noise_beta"};

    cfg.agc_hang = (cfg_item_t){.val=subject_create_int(false), .db_name="agc_hang"};
    cfg.agc_knee = (cfg_item_t){.val=subject_create_int(-60), .db_name="agc_knee"};
    cfg.agc_slope = (cfg_item_t){.val=subject_create_int(6), .db_name="agc_slope"};

    // DSP
    cfg.dnf = (cfg_item_t){.val = subject_create_int(false), .db_name = "dnf"};
    cfg.dnf_center = (cfg_item_t){.val = subject_create_int(1000), .db_name = "dnf_center"};
    cfg.dnf_width = (cfg_item_t){.val = subject_create_int(50), .db_name = "dnf_width"};
    cfg.dnf_auto = (cfg_item_t){.val = subject_create_int(false), .db_name = "dnf_auto"};

    cfg.nb = (cfg_item_t){.val = subject_create_int(false), .db_name = "nb"};
    cfg.nb_level = (cfg_item_t){.val = subject_create_int(10), .db_name = "nb_level"};
    cfg.nb_width = (cfg_item_t){.val = subject_create_int(10), .db_name = "nb_width"};

    cfg.nr = (cfg_item_t){.val = subject_create_int(false), .db_name = "nr"};
    cfg.nr_level = (cfg_item_t){.val = subject_create_int(0), .db_name = "nr_level"};

    // SWR scan
    cfg.swrscan_linear = (cfg_item_t){.val=subject_create_int(true), .db_name="swrscan_linear"};
    cfg.swrscan_span = (cfg_item_t){.val=subject_create_int(200000), .db_name="swrscan_span"};

    // FT8
    cfg.ft8_hold_freq = (cfg_item_t){.val=subject_create_int(true), .db_name="ft8_hold_freq"};


    cfg_cur.fft_width = subject_create_int(FFT_FULL_WIDTH);
    /* Bind callbacks */
    // subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);
    subject_add_observer(cfg.key_tone.val, on_key_tone_change, NULL);
    subject_add_observer(cfg.fft_dec.val, update_fft_width, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr  = (cfg_item_t *)&cfg;
    uint32_t    cfg_size = sizeof(cfg) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size, cfg_params_load_item, cfg_params_save_item);
    return load_items_from_db(cfg_arr, cfg_size);
}
