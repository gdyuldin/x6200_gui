#pragma once

#include "common.h"
#include "atu.h"
#include "band.h"

#include <pthread.h>
#include <sqlite3.h>

#define FFT_FULL_WIDTH 384000

typedef struct {
    cfg_item_t en;
    cfg_item_t p1;
    cfg_item_t p2;
    cfg_item_t p3;
    cfg_item_t p4;
    cfg_item_t p5;
} cfg_eq_t;

/* configuration structs. Should contain same types (for correct initialization) */
typedef struct {
    cfg_item_t vol;
    cfg_item_t sql;
    cfg_item_t pwr;

    cfg_item_t fft_dec;

    cfg_item_t key_tone;
    cfg_item_t band_id;
    cfg_item_t ant_id;
    cfg_item_t atu_enabled;

    /* key */
    cfg_item_t key_speed;
    cfg_item_t key_mode;
    cfg_item_t iambic_mode;
    cfg_item_t key_vol;
    cfg_item_t key_train;
    cfg_item_t qsk_time;
    cfg_item_t key_ratio;

    /* CW decoder */
    cfg_item_t cw_decoder;
    cfg_item_t cw_tune;
    cfg_item_t cw_decoder_snr;
    cfg_item_t cw_decoder_snr_gist;
    cfg_item_t cw_decoder_peak_beta;
    cfg_item_t cw_decoder_noise_beta;

    cfg_item_t agc_hang;
    cfg_item_t agc_knee;
    cfg_item_t agc_slope;

    cfg_item_t comp;

    // DSP
    cfg_item_t dnf;
    cfg_item_t dnf_center;
    cfg_item_t dnf_width;
    cfg_item_t dnf_auto;

    cfg_item_t nb;
    cfg_item_t nb_level;
    cfg_item_t nb_width;

    cfg_item_t nr;
    cfg_item_t nr_level;

    // SWR scan
    cfg_item_t swrscan_linear;
    cfg_item_t swrscan_span;

    // FT8
    cfg_item_t ft8_hold_freq;

    // EQ
    struct {
        cfg_eq_t rx;
        cfg_eq_t rx_wfm;
        cfg_eq_t mic;
    } eq;
} cfg_t;
extern cfg_t cfg;

/* Current band/mode params */

typedef struct {
    Subject *fg_freq;
    Subject *bg_freq;
    Subject *lo_offset;
    Subject *freq_shift;
    Subject *mode;
    Subject *agc;
    Subject *att;
    Subject *pre;
    struct {
        Subject *low;
        Subject *high;
        Subject *bw;
        struct {
            Subject *from;
            Subject *to;
        } real;
    } filter;
    Subject       *freq_step;
    Subject       *zoom;
    atu_network_t *atu;
    cfg_band_t    *band;

    Subject *fft_width;
} cfg_cur_t;

extern cfg_cur_t cfg_cur;

int cfg_init(sqlite3 *db);
