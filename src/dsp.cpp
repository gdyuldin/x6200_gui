/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
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
#include <mutex>

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

    std::mutex mutex;

    void lerp_averaged() {
        if (output_size == input_size) {
            for (size_t i = 0; i < output_size; i++) {
                averaged[i] = psd[i];
            }
        } else {
            float a, b;
            for (size_t i = 0; i < output_size; i++) {
                a = psd[positions[i]];
                b = psd[positions[i] + 1];
                averaged[i] = a + (b - a) * offsets[i];
            }
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

    void reset() {
        const std::lock_guard<std::mutex> lock(mutex);
        count = 0;
        psd.fill(0.0f);
    };

    void add_samples(float *samples) {
        liquid_vectorf_add(psd.data(), samples, psd.size(), psd.data());
        count++;
    };

    std::array<float, output_size> *get() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (!count) {
                return nullptr;
            }
            liquid_vectorf_mulscalar(psd.data(), psd.size(), 1.0f / count, psd.data());
            lerp_averaged();
        }

        for (size_t i = 0; i < averaged.size(); i++) {
            averaged[i] = 20.0f * log10f(averaged[i] + 1e-9f);
        }

        reset();
        return &averaged;
    };
};

static AveragedPSD<RADIO_SAMPLES, SPECTRUM_NFFT> spectrum_avg_psd;
static AveragedPSD<RADIO_SAMPLES, RADIO_SAMPLES> waterfall_avg_psd;

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
static bool last_tx = false;

static x6200_mode_t cur_mode;

static void dsp_update_min_max(float *data_buf, uint16_t size);
static void on_cur_freq_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);


/* * */

void dsp_init() {
    spectrum_time  = get_time();
    waterfall_time = get_time();

    psd_delay = 0;

    audio      = (cfloat *)malloc(AUDIO_CAPTURE_RATE * sizeof(cfloat));
    audio_hilb = firhilbf_create(7, 60.0f);

    cfg_cur.fg_freq->subscribe(on_cur_freq_change);
    cfg_cur.mode->subscribe(on_cur_mode_change)->notify();
    ready = true;
}

void dsp_reset() {
    waterfall_avg_psd.reset();
    spectrum_avg_psd.reset();
}

static void update_s_meter(int16_t peak_db) {
    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        meter_update(peak_db, 0.0f);
    }
}

void dsp_samples(float *buf_samples, uint16_t size, bool tx, int16_t dbm) {
    uint64_t      now = get_time();

    if (last_tx != tx) {
        last_tx = tx;
        spectrum_avg_psd.reset();
        waterfall_avg_psd.reset();
    }

    spectrum_avg_psd.add_samples(buf_samples);
    if (psd_delay) {
        psd_delay--;
    } else {
        waterfall_avg_psd.add_samples(buf_samples);
    }
    if ((now - spectrum_time > spectrum_fps_ms)) {
        auto spectrum_avg_data = spectrum_avg_psd.get();
        if (spectrum_avg_data){
            // Decrease beta for high zoom
            float new_beta = powf(spectrum_beta, ((float)spectrum_factor - 1.0f) / 2.0f + 1.0f);
            lpf_block(spectrum_psd_filtered, spectrum_avg_data->data(), new_beta, SPECTRUM_NFFT);
            spectrum_data(spectrum_psd_filtered, SPECTRUM_NFFT, tx);
            spectrum_time = now;
        }
    }
    if ((now - waterfall_time > waterfall_fps_ms)) {
        auto waterfall_avg_data = waterfall_avg_psd.get();
        if (waterfall_avg_data) {
            waterfall_data(waterfall_avg_data->data(), waterfall_avg_data->size(), tx);
            waterfall_time = now;

            // update meter
            update_s_meter(dbm);

            // update min/max
            if (!tx) {
                dsp_update_min_max(waterfall_avg_data->data(), waterfall_avg_data->size());
            }
        }
    }
}

static void on_cur_freq_change(Subject *subj, void *user_data) {
    int32_t new_freq = static_cast<SubjectT<int32_t> *>(subj)->get();
    int32_t diff = new_freq - cur_freq;
    cur_freq = new_freq;
    psd_delay = 3;
    waterfall_avg_psd.reset();
    spectrum_avg_psd.reset();
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    cur_mode = (x6200_mode_t)subject_get_int(cfg_cur.mode);
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
    } else if (cur_mode == x6200_mode_cw || cur_mode == x6200_mode_cwr) {
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
    uint16_t min_nth = size * 20 / 100;
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
