/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw.h"


#include <math.h>

#include "util.h"
#include "cfg/cfg.h"
#include "cfg/subjects.h"

extern "C" {
    #include "lvgl/lvgl.h"
    #include <pthread.h>
    #include "audio.h"
    #include "params/params.h"
    #include "cw_decoder.h"
    #include "pannel.h"
    #include "meter.h"
    #include "cw_tune_ui.h"
    #include "pubsub_ids.h"
}

typedef struct {
    uint16_t    n;
    float       val;
} fft_item_t;

#define NUM_STAGES 6
#define DECIM_FACTOR (1LL << NUM_STAGES)
#define FFT 128
#define MAX_CW_BW 500

static bool ready = false;

static fft_item_t fft_items[FFT];

static dds_cccf  ds_dec;
static wrms_t    wrms;
static cbuffercf input_cbuf;
static cbuffercf rms_cbuf;
static wdelayf   rms_delay;

static cbuffercf fft_cbuf;
static fftplan   fft_plan;
static float     window[FFT];
static cfloat    fft_time[FFT];
static cfloat    fft_freq[FFT];
static float     audio_psd_squared[FFT];

static float peak_filtered;
static float noise_filtered;
static float threshold_pulse;
static float threshold_silence;
static float rms_db_max;
static float rms_db_min;
static bool  peak_on = false;

static int32_t key_tone = 0;
static float   cw_decoder_peak_beta;
static float   cw_decoder_noise_beta;
static float   cw_decoder_snr;
static float   cw_decoder_snr_gist;
static bool    cw_decoder;
static bool    cw_tune;

static pthread_mutex_t  cw_mutex = PTHREAD_MUTEX_INITIALIZER;

static void dds_dec_init();

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_val_float_change(Subject *subj, void *user_data);
static void on_val_bool_change(Subject *subj, void *user_data);

void cw_init() {
    cfg.key_tone.val->subscribe(on_key_tone_change)->notify();
    cfg.cw_decoder_peak_beta.val->subscribe(on_val_float_change, (void*)&cw_decoder_peak_beta)->notify();
    cfg.cw_decoder_noise_beta.val->subscribe(on_val_float_change, (void*)&cw_decoder_noise_beta)->notify();
    cfg.cw_decoder_snr.val->subscribe(on_val_float_change, (void*)&cw_decoder_snr)->notify();
    cfg.cw_decoder_snr_gist.val->subscribe(on_val_float_change, (void*)&cw_decoder_snr_gist)->notify();
    cfg.cw_decoder.val->subscribe(on_val_bool_change, (void*)&cw_decoder)->notify();
    cfg.cw_tune.val->subscribe(on_val_bool_change, (void*)&cw_tune)->notify();

    input_cbuf = cbuffercf_create(10000);
    wrms = wrms_create(16, 4);
    rms_cbuf = cbuffercf_create(4000 / 8 * 2);
    fft_cbuf = cbuffercf_create(4000 / 8 * 2);

    // Window for FFT
    float scale = 0.0f;
    for (size_t i = 0; i < FFT; i++) {
        window[i] = liquid_hann(i, FFT);
        scale += window[i] * window[i];
    }
    scale = 1.0f / sqrtf(scale);
    // scale window and copy
    for (size_t i=0; i<FFT; i++)
        window[i] *= scale;

    fft_plan = fft_create_plan(FFT, fft_time, fft_freq, LIQUID_FFT_FORWARD, 0);

    rms_delay = wdelayf_create(FFT / wrms_delay(wrms));

    peak_filtered = -10.0f;
    noise_filtered = -20.0f;

    ready = true;
}

static void dds_dec_init() {
    pthread_mutex_lock(&cw_mutex);
    if (ds_dec != NULL) {
        dds_cccf_destroy(ds_dec);
    }
    float rel_freq = (float) key_tone / AUDIO_CAPTURE_RATE;
    float bw = (float) MAX_CW_BW / AUDIO_CAPTURE_RATE;
    ds_dec = dds_cccf_create(NUM_STAGES, rel_freq, bw, 60.0f);
    pthread_mutex_unlock(&cw_mutex);
}

static int compare_fft_items(const void *p1, const void *p2) {
    fft_item_t *i1 = (fft_item_t *) p1;
    fft_item_t *i2 = (fft_item_t *) p2;

    return (i1->val > i2->val) ? -1 : 1;
}

static void update_thresholds() {
    float noise;
    float sum_all = 0.0f;
    float sum_signal = 0.0f;
    float peak_val = -1.0f;
    int16_t peak_pos, peak_start, peak_end;

    // Find top positions
    for (int16_t n = 0; n < FFT; n++) {
        if (audio_psd_squared[n] > peak_val) {
            peak_val = audio_psd_squared[n];
            peak_pos = n;
        }
        sum_all += audio_psd_squared[n];
    }

    // BW for 30 WPM
    uint16_t peak_width = 30 * 4 * FFT * DECIM_FACTOR / AUDIO_CAPTURE_RATE;

    peak_start = peak_pos - peak_width / 2;
    peak_end = peak_start + peak_width;
    if (peak_start < 0) {
        peak_start = 0;
        peak_end = peak_width;
    } else if (peak_end >= FFT) {
        peak_end = FFT;
        peak_start = peak_end - peak_width;
    }

    for (uint16_t i = peak_start; i < peak_end; i++) {
        sum_signal += audio_psd_squared[i];
    }
    noise = sum_all - sum_signal;

    if (sum_signal/noise > 1) {
        lpf(&peak_filtered, LV_MAX(noise_filtered + cw_decoder_snr, rms_db_max), cw_decoder_peak_beta, S_MIN);
        threshold_pulse = 0;
    } else {
        lpf(&noise_filtered, LV_MIN(-3.0f, rms_db_min), cw_decoder_noise_beta, S_MIN);
        peak_filtered -= 0.3f;
        if (noise_filtered + cw_decoder_snr > peak_filtered) {
            peak_filtered = noise_filtered + cw_decoder_snr;
        }
        // Increase threshold for no signal
        threshold_pulse = 1;
    }
    float low = noise_filtered + cw_decoder_snr;
    threshold_pulse += LV_MAX(low, peak_filtered  - 3.0f);
    threshold_silence = threshold_pulse - cw_decoder_snr_gist;
    rms_db_min = 0.0f;
    rms_db_max = S1;
}

static void update_peak_freq(float freq) {
    if (peak_on) {
        cw_tune_set_freq(freq);
    }
}

static bool decode(float rms_db) {
    if (peak_on) {
        if (rms_db < threshold_silence) {
            peak_on = false;
        }
    } else {
        if (rms_db > threshold_pulse) {
            peak_on = true;
        }
    }
    // printf("CW_levels%f,%f,%f,%f\n", rms_db, noise_filtered, peak_filtered, threshold_pulse);
    return peak_on;
}


void cw_put_audio_samples(unsigned int n, cfloat *samples) {
    if (!ready) {
        return;
    }
    if ((!cw_decoder) && (!cw_tune)) {
        return;
    }
    cfloat sample;
    float rms_db, peak_freq;
    size_t max_pos;

    // fill input buffer
    cbuffercf_write(input_cbuf, samples, n);

    // Downsample and freq shift
    const size_t desired_num = (1<<NUM_STAGES);
    while (cbuffercf_size(input_cbuf) >= desired_num) {
        unsigned int n;
        cfloat *buf;
        cbuffercf_read(input_cbuf, desired_num, &buf, &n);
        pthread_mutex_lock(&cw_mutex);
        dds_cccf_decim_execute(ds_dec, buf, &sample);
        pthread_mutex_unlock(&cw_mutex);
        cbuffercf_release(input_cbuf, desired_num);
        cbuffercf_push(rms_cbuf, sample);
        cbuffercf_push(fft_cbuf, sample);

        // Process FFT
        while (cbuffercf_size(fft_cbuf) >= FFT) {
            for (size_t i = 0; i < FFT; i++) {
                cbuffercf_pop(fft_cbuf, &fft_time[i]);
                fft_time[i] *= window[i];
            }
            fft_execute(fft_plan);

            for (size_t i = 0; i < FFT; i++) {
                audio_psd_squared[i] = std::real(fft_freq[i] * std::conj(fft_freq[i]));
            }
            update_thresholds();

            max_pos = argmax(audio_psd_squared, FFT);
            // Fix fft order
            peak_freq = (((float) (FFT - (max_pos + FFT / 2) % FFT) / FFT) - 0.5f) * ((float) AUDIO_CAPTURE_RATE / DECIM_FACTOR);
            update_peak_freq(peak_freq);
        }

        // Process RMS
        while (cbuffercf_size(rms_cbuf)) {
            cbuffercf_pop(rms_cbuf, &sample);
            wrms_pushcf(wrms, sample);
            if (wrms_ready(wrms)) {
                rms_db = wrms_get_val(wrms);
                rms_db_min = LV_MIN(rms_db_min, rms_db);
                rms_db_max = LV_MAX(rms_db_max, rms_db);
                wdelayf_push(rms_delay, rms_db);
                wdelayf_read(rms_delay, &rms_db);
                cw_decoder_signal(decode(rms_db), 1000.0f / AUDIO_CAPTURE_RATE * DECIM_FACTOR * wrms_delay(wrms));
            }
        }
    }
}

// bool cw_change_decoder(int16_t df) {
//     if (df == 0) {
//         return params.cw_decoder;
//     }

//     params_lock();
//     params.cw_decoder = !params.cw_decoder;
//     params_unlock(&params.dirty.cw_decoder);
//     lv_msg_send(MSG_PARAM_CHANGED, NULL);

//     pannel_visible();

//     return params.cw_decoder;
// }

// float cw_change_snr(int16_t df) {
//     if (df == 0) {
//         return params.cw_decoder_snr;
//     }

//     float x = params.cw_decoder_snr + df * 0.1f;

//     if (x < 3.0f) {
//         x = 3.0f;
//     } else if (x > 30.0f) {
//         x = 30.0f;
//     }

//     params_lock();
//     params.cw_decoder_snr = x;
//     params_unlock(&params.dirty.cw_decoder_snr);
//     lv_msg_send(MSG_PARAM_CHANGED, NULL);

//     return params.cw_decoder_snr;
// }

// float cw_change_peak_beta(int16_t df) {
//     if (df == 0) {
//         return params.cw_decoder_peak_beta;
//     }

//     float x = params.cw_decoder_peak_beta + df * 0.01f;

//     if (x < 0.1f) {
//         x = 0.1f;
//     } else if (x > 0.95f) {
//         x = 0.95f;
//     }

//     params_lock();
//     params.cw_decoder_peak_beta = x;
//     params_unlock(&params.dirty.cw_decoder_peak_beta);
//     lv_msg_send(MSG_PARAM_CHANGED, NULL);

//     return params.cw_decoder_peak_beta;
// }

static void on_key_tone_change(Subject *subj, void *user_data) {
    key_tone = static_cast<SubjectT<int32_t>*>(subj)->get();
    dds_dec_init();
}

static void on_val_float_change(Subject *subj, void *user_data) {
    *(float*)user_data = static_cast<SubjectT<float>*>(subj)->get();
}

static void on_val_bool_change(Subject *subj, void *user_data) {
    *(bool*)user_data = static_cast<SubjectT<int32_t>*>(subj)->get();
}
