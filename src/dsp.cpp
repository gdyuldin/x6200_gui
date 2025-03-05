/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dsp.h"

#include "cw.h"
#include "util.h"
#include "buttons.h"
#include "cfg/subjects.h"

#include <algorithm>
#include <numeric>

extern "C" {
    #include "audio.h"
    #include "cfg/cfg.h"
    #include "dialog_msg_voice.h"
    #include "meter.h"
    #include "recorder.h"
    #include "rtty.h"
    #include "spectrum.h"
    #include "waterfall.h"

    #include <math.h>
    #include <pthread.h>
    #include <stdlib.h>
}


#define ANF_DECIM_FACTOR 8 // 100000 -> 12500
#define ANF_STEP 25 // Hz
#define ANF_NFFT 100000 / ANF_DECIM_FACTOR / ANF_STEP
#define ANF_INTERVAL_MS 500
#define ANF_HIST_LEN 3

template <size_t input_size, size_t output_size> class AveragedPSD {
  private:
    int8_t                         count;
    std::array<float, input_size>  psd;
    std::array<float, output_size> averaged;

    size_t positions[output_size];
    float offsets[output_size];

    void lerp_averaged() {
        float a, b;
        for (size_t i = 0; i < output_size; i++) {
            a = psd[positions[i]];
            b = psd[positions[i] + 1];
            averaged[i] = a + (b - a) * offsets[i];
        }
    }

  public:
    AveragedPSD() {
        for (size_t i = 0; i < output_size; i++) {
            float f = (float)i / output_size * input_size;
            positions[i] = f;
            offsets[i] = f - positions[i];
        }
    }

    void add_samples(float *samples) {
        liquid_vectorf_add(psd.data(), samples, psd.size(), psd.data());
        count++;
    };
    std::array<float, output_size> *get() {

        liquid_vectorf_mulscalar(psd.data(), psd.size(), 1.0f / count, psd.data());

        // averaged = psd;
        lerp_averaged();

        for (size_t i = 0; i < averaged.size(); i++) {
            averaged[i] = 20.0f * log10f(averaged[i]);
        }

        count = 0;
        psd.fill(0.0f);
        return &averaged;
    };
};

static pthread_mutex_t spectrum_mux = PTHREAD_MUTEX_INITIALIZER;

static AveragedPSD<RADIO_SAMPLES, SPECTRUM_NFFT> spectrum_avg_psd;
static AveragedPSD<RADIO_SAMPLES, WATERFALL_NFFT> waterfall_avg_psd;

static uint8_t       spectrum_factor = 1;

static float          spectrum_psd[SPECTRUM_NFFT];
static float          spectrum_psd_filtered[SPECTRUM_NFFT];
static float          spectrum_beta   = 0.7f;
static uint8_t        spectrum_fps_ms = (1000 / 15);
static uint64_t       spectrum_time;
static cfloat         spectrum_dec_buf[SPECTRUM_NFFT / 2];

static float          waterfall_psd[WATERFALL_NFFT];
static uint8_t        waterfall_fps_ms = (1000 / 25);
static uint64_t       waterfall_time;

static cfloat buf_filtered[RADIO_SAMPLES];

static uint32_t cur_freq;
static uint8_t  psd_delay;
static uint8_t  min_max_delay;

static firhilbf audio_hilb;
static cfloat  *audio;

static bool ready = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static x6100_mode_t cur_mode;

static void dsp_update_min_max(float *data_buf, uint16_t size);
static void on_zoom_change(Subject *subj, void *user_data);
static void on_real_filter_from_change(Subject *subj, void *user_data);
static void on_real_filter_to_change(Subject *subj, void *user_data);
static void on_cur_freq_change(Subject *subj, void *user_data);


/* * */

void dsp_init() {
    spectrum_time  = get_time();
    waterfall_time = get_time();

    psd_delay = 0;

    audio      = (cfloat *)malloc(AUDIO_CAPTURE_RATE * sizeof(cfloat));
    audio_hilb = firhilbf_create(7, 60.0f);

    // subject_add_observer_and_call(cfg_cur.zoom, on_zoom_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.from, on_real_filter_from_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.to, on_real_filter_to_change, NULL);

    cfg_cur.fg_freq->subscribe(on_cur_freq_change);
    ready = true;
}

void dsp_reset() {

}

static void update_s_meter(int16_t peak_db) {
    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        // printf("peak_db: %i\n", peak_db);
        meter_update(peak_db, 0.8f);
    }
}

void dsp_samples(float *buf_samples, uint16_t size, bool tx, int16_t dbm) {
    uint64_t      now = get_time();

    // float *samples = (float*) calloc(sizeof(float), size);
    // for (size_t i = 0; i < size; i++) {
    //     samples[i] = 20.0f * log10f(buf_samples[i]);
    // }
    // spectrum_avg_psd.add_samples(samples);
    // waterfall_avg_psd.add_samples(samples);

    spectrum_avg_psd.add_samples(buf_samples);
    waterfall_avg_psd.add_samples(buf_samples);
    if ((now - spectrum_time > spectrum_fps_ms)) {
        auto spectrum_avg_data = spectrum_avg_psd.get();
        for (size_t i = 0; i < spectrum_avg_data->size(); i++) {
            spectrum_psd[i] = spectrum_avg_data->at(i);
        }

        // sp_sg->get_psd(spectrum_psd);
        // liquid_vectorf_addscalar(spectrum_psd, SPECTRUM_NFFT, -30.0f, spectrum_psd);
        // Decrease beta for high zoom
        float new_beta = powf(spectrum_beta, ((float)spectrum_factor - 1.0f) / 2.0f + 1.0f);
        lpf_block(spectrum_psd_filtered, spectrum_psd, new_beta, SPECTRUM_NFFT);
        spectrum_data(spectrum_psd_filtered, SPECTRUM_NFFT, tx);
        spectrum_time = now;
    }
    if ((now - waterfall_time > waterfall_fps_ms)) {
        // wf_sg->get_psd(waterfall_psd);
        // liquid_vectorf_addscalar(waterfall_psd, WATERFALL_NFFT, -30.0f, waterfall_psd);
        auto waterfall_avg_data = waterfall_avg_psd.get();
        waterfall_data(waterfall_avg_data->data(), waterfall_avg_data->size(), tx);
        waterfall_time = now;

        // update meter
        update_s_meter(dbm);

        // update min/max
        dsp_update_min_max(waterfall_avg_data->data(), waterfall_avg_data->size());
    }

    // auto averaged_return = psd.get();
    // printf("averaged 0: %f\n", averaged_return->at(0));


    // firdecim_crcf sp_decim;
    // ChunkedSpgram *sp_sg, *wf_sg;

    // if (psd_delay) {
    //     psd_delay--;
    // }

    // pthread_mutex_lock(&spectrum_mux);
    // if (tx) {
    //     sp_decim = spectrum_decim_tx;
    //     sp_sg    = spectrum_sg_tx;
    //     wf_sg    = waterfall_sg_tx;
    // } else {
    //     sp_decim = spectrum_decim_rx;
    //     sp_sg    = spectrum_sg_rx;
    //     wf_sg    = waterfall_sg_rx;
    // }
    // process_samples(buf_samples, size, sp_decim, sp_sg, wf_sg, tx);
    // update_spectrum(sp_sg, now, tx);
    // pthread_mutex_unlock(&spectrum_mux);
    // if (update_waterfall(wf_sg, now, tx)) {
    //     update_s_meter();
    //     // TODO: skip on disabled auto min/max
    //     if (!tx) {
    //         dsp_update_min_max(waterfall_psd, WATERFALL_NFFT);
    //     } else {
    //         min_max_delay = 2;
    //     }
    // }
    // if (!tx  && !psd_delay) {
    //     anf->update(now, cur_mode==x6100_mode_lsb);
    // }
}

static void on_zoom_change(Subject *subj, void *user_data) {

}

static void on_real_filter_from_change(Subject *subj, void *user_data) {
    filter_from = subject_get_int(subj);
}

static void on_real_filter_to_change(Subject *subj, void *user_data) {
    filter_to = subject_get_int(subj);
}

static void on_cur_freq_change(Subject *subj, void *user_data) {
    int32_t new_freq = static_cast<SubjectT<int32_t> *>(subj)->get();
    int32_t diff = new_freq - cur_freq;
    cur_freq = new_freq;
    // waterfall_sg_rx->reset();
    psd_delay = 1;
}

float dsp_get_spectrum_beta() {
    return spectrum_beta;
}

void dsp_set_spectrum_beta(float x) {
    spectrum_beta = x;
}

void dsp_put_audio_samples(size_t nsamples, int16_t *samples) {
    if (!ready) {
        return;
    }

    if (dialog_msg_voice_get_state() == MSG_VOICE_RECORD) {
        dialog_msg_voice_put_audio_samples(nsamples, samples);
        return;
    }

    if (recorder_is_on()) {
        recorder_put_audio_samples(nsamples, samples);
    }

    for (uint16_t i = 0; i < nsamples; i++)
        firhilbf_r2c_execute(audio_hilb, samples[i] / 32768.0f, &audio[i]);

    if (rtty_get_state() == RTTY_RX) {
        rtty_put_audio_samples(nsamples, audio);
    } else if (cur_mode == x6100_mode_cw || cur_mode == x6100_mode_cwr) {
        cw_put_audio_samples(nsamples, audio);
    } else {
        dialog_audio_samples(nsamples, audio);
    }
}

static int compare_fft(const void *p1, const void *p2) {
    float *i1 = (float *)p1;
    float *i2 = (float *)p2;

    return (*i1 < *i2) ? -1 : 1;
}

static void dsp_update_min_max(float *data_buf, uint16_t size) {
    if (min_max_delay) {
        min_max_delay--;
        return;
    }
    qsort(data_buf, size, sizeof(float), compare_fft);
    uint16_t min_nth = size * 15 / 100;
    uint16_t max_nth = size * 10 / 100;

    float min = data_buf[min_nth];
    // float max = data_buf[size - max_nth - 1];


    if (min < S_MIN) {
        min = S_MIN;
    } else if (min > S8) {
        min = S8;
    }
    float max = min + 48;

    spectrum_update_min(min);
    waterfall_update_min(min);

    spectrum_update_max(max);
    waterfall_update_max(max);
}
