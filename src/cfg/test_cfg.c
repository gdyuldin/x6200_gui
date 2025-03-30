#include "cfg.private.h"

#include "band.private.h"
#include "mode.private.h"

#include <aether_radio/x6200_control/control.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define kHz 1000
#define MHz 1000 * kHz

#define CHECK(fn)                                                                                                      \
    ({                                                                                                                 \
        printf("Start " #fn ":\n");                                                                                    \
        fn;                                                                                                            \
        printf(" OK\n");                                                                                               \
    })
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

static void test_load_band_by_freq() {
    struct data_t {
        uint32_t freq;
        int32_t  expected_bands;
    } data[] = {
        {400 * kHz,       -1},
        {14 * MHz - 1,    -1},
        {14 * MHz,        -1},
        {14 * MHz + 1,    6 },
        {14070 * kHz - 1, 6 },
        {14070 * kHz,     6 },
        {14070 * kHz + 1, 7 },
        {14350 * kHz,     7 },
        {14350 * kHz + 1, -1},
        {99 * MHz,        -1},
        {14350 * kHz,     7 },
        {14070 * kHz + 1, 7 },
        {14070 * kHz,     6 },
        {14 * MHz + 1,    6 },
        {14 * MHz,        -1},
    };
    size_t freq_len = sizeof(data) / sizeof(*data);
    bool   success = true;
    for (size_t i = 0; i < freq_len; i++) {
        int32_t id = get_band_info_by_freq(data[i].freq)->id;
        if (id != data[i].expected_bands) {
            printf("Wrong band id for freq %lu: expected: %i, actual: %i\n", data[i].freq, data[i].expected_bands, id);
            success = false;
        }
    }
    if (!success)
        exit(1);
}

static void test_load_band_by_pk() {
    band_info_t *info = get_band_info_by_pk(7);
    assert(info->start_freq == 14070 * kHz && "Wrong start freq");
    assert(info->stop_freq == 14350 * kHz && "Wrong stop freq");
    assert(strcmp("20m SSB", info->name) == 0 && "Wrong name");
}

static void test_set_another_band() {
    subject_set_int(cfg.band_id.val, 6);
    subject_set_int(cfg_band.vfo_a.freq.val, 14050000);
    subject_set_int(cfg_band.vfo_b.freq.val, 14060000);

    subject_set_int(cfg.band_id.val, 7);
    printf("freq_a: %lu, freq_b: %lu\n", subject_get_int(cfg_band.vfo_a.freq.val),
           subject_get_int(cfg_band.vfo_b.freq.val));
    assert(subject_get_int(cfg_band.vfo_a.freq.val) != 14050000 && "Freq is not changed");
    assert(subject_get_int(cfg_band.vfo_b.freq.val) != 14060000 && "Freq is not changed");
    assert(cfg_band.vfo_a.freq.pk == 7);
    assert(cfg_band.vfo_b.freq.pk == 7);
    assert(cfg_band.vfo.pk == 7);
}

static void test_set_another_band_using_freq() {
    subject_set_int(cfg.band_id.val, 6);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_A);
    uint32_t prev_a_freq = subject_get_int(cfg_band.vfo_a.freq.val);
    subject_set_int(cfg_band.vfo_a.freq.val, 14200 * kHz);
    assert(subject_get_int(cfg.band_id.val) == 7);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14200 * kHz);
    assert(cfg_band.vfo.pk == 7);
    assert(cfg_band.vfo_a.freq.pk == 7);
    assert(cfg_band.vfo_b.freq.pk == 7);

    subject_set_int(cfg.band_id.val, 6);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == prev_a_freq);
}

static void test_switch_band_up() {
    struct {
        uint32_t freq;
        int32_t  cur_id;
        int32_t  expected_id;
    } data[] = {
        {14000 * kHz - 1, BAND_UNDEFINED, 6 },
        {14000 * kHz,     6,              7 },
        {14000 * kHz + 1, 6,              7 },
        {14070 * kHz - 1, 6,              7 },
        {14070 * kHz,     6,              7 },
        {14070 * kHz,     7,              8 },
        {600 * MHz,       -1,             -1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(data); i++) {
        band_info_t *band = get_band_info_next(data[i].freq, true, data[i].cur_id);
        if (data[i].expected_id < 0) {
            assert(band == NULL);
        } else {
            assert(band != NULL);
            printf("%lu next -> %s, %lu, %lu, %i\n", data[i].freq, band->name, band->start_freq, band->stop_freq,
                   band->id);
            assert(band->id == data[i].expected_id);
        }
    }
}

static void test_switch_band_down() {
    struct {
        uint32_t freq;
        int32_t  cur_id;
        int32_t  expected_id;
    } data[] = {
        {14350 * kHz + 1, BAND_UNDEFINED, 7 },
        {14350 * kHz,     7,              6 },
        {14070 * kHz + 1, 7,              6 },
        {14070 * kHz,     7,              6 },
        {14070 * kHz,     6,              5 },
        {14000 * kHz,     6,              5 },
        {1000 * kHz,      -1,             -1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(data); i++) {
        band_info_t *band = get_band_info_next(data[i].freq, false, data[i].cur_id);
        if (data[i].expected_id < 0) {
            assert(band == NULL);
        } else {
            assert(band != NULL);
            printf("%lu prev -> %s, %lu, %lu, %i\n", data[i].freq, band->name, band->start_freq, band->stop_freq,
                   band->id);
            assert(band->id == data[i].expected_id);
        }
    }
}

static void test_cur_fg_bg_freq_tracking() {
    subject_set_int(cfg_band.vfo_a.freq.val, 14200000);
    subject_set_int(cfg_band.vfo_b.freq.val, 14210000);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_A);
    assert(subject_get_int(cfg_cur.fg_freq) == 14200000);
    assert(subject_get_int(cfg_cur.bg_freq) == 14210000);
    subject_set_int(cfg_band.vfo_a.freq.val, 14220000);
    assert(subject_get_int(cfg_cur.fg_freq) == 14220000);
    assert(subject_get_int(cfg_cur.bg_freq) == 14210000);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_B);
    assert(subject_get_int(cfg_cur.fg_freq) == 14210000);
    assert(subject_get_int(cfg_cur.bg_freq) == 14220000);
}

static void test_ab_freq_tracking_cur() {
    subject_set_int(cfg_band.vfo_a.freq.val, 14200000);
    subject_set_int(cfg_band.vfo_b.freq.val, 14210000);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_A);
    subject_set_int(cfg_cur.fg_freq, 14220000);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14220000);
    assert(subject_get_int(cfg_band.vfo_b.freq.val) == 14210000);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_B);
    subject_set_int(cfg_cur.fg_freq, 14230000);
    assert(subject_get_int(cfg_band.vfo_b.freq.val) == 14230000);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14220000);
    subject_set_int(cfg_cur.bg_freq, 14240000);
    assert(subject_get_int(cfg_band.vfo_b.freq.val) == 14230000);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14240000);
}

static void test_cur_mode_tracking() {
    subject_set_int(cfg_band.vfo_a.mode.val, x6200_mode_am);
    subject_set_int(cfg_band.vfo_b.mode.val, x6200_mode_cw);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_A);
    assert(subject_get_int(cfg_cur.mode) == x6200_mode_am);
    subject_set_int(cfg_band.vfo_a.mode.val, x6200_mode_lsb);
    assert(subject_get_int(cfg_cur.mode) == x6200_mode_lsb);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_B);
    assert(subject_get_int(cfg_cur.mode) == x6200_mode_cw);
}

static void test_ab_mode_tracking_cur() {
    subject_set_int(cfg_band.vfo_a.mode.val, x6200_mode_usb_dig);
    subject_set_int(cfg_band.vfo_b.mode.val, x6200_mode_cwr);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_A);
    subject_set_int(cfg_cur.mode, x6200_mode_am);
    assert(subject_get_int(cfg_band.vfo_a.mode.val) == x6200_mode_am);
    assert(subject_get_int(cfg_band.vfo_b.mode.val) == x6200_mode_cwr);
    subject_set_int(cfg_band.vfo.val, X6200_VFO_B);
    subject_set_int(cfg_cur.mode, x6200_mode_nfm);
    assert(subject_get_int(cfg_band.vfo_b.mode.val) == x6200_mode_nfm);
    assert(subject_get_int(cfg_band.vfo_a.mode.val) == x6200_mode_am);
}

static void test_change_filters_am_fm() {
    x6200_mode_t modes[] = {x6200_mode_am, x6200_mode_nfm};
    for (size_t i = 0; i < 2; i++) {
        subject_set_int(cfg_cur.mode, modes[i]);
        subject_set_int(cfg_cur.filter.low, 0);
        subject_set_int(cfg_cur.filter.high, 4000);
        assert(subject_get_int(cfg_cur.filter.bw) == 4000);
        assert(subject_get_int(cfg_mode.filter_high.val) == 4000);
        assert(subject_get_int(cfg_cur.filter.low) == 0);

        subject_set_int(cfg_cur.filter.high, 3500);
        assert(subject_get_int(cfg_cur.filter.bw) == 3500);
        assert(subject_get_int(cfg_mode.filter_high.val) == 3500);
        assert(subject_get_int(cfg_cur.filter.low) == 0);

        subject_set_int(cfg_cur.filter.bw, 4000);
        assert(subject_get_int(cfg_cur.filter.high) == 4000);
        assert(subject_get_int(cfg_cur.filter.low) == 0);
        assert(subject_get_int(cfg_mode.filter_high.val) == 4000);

        subject_set_int(cfg_mode.filter_high.val, 3900);
        assert(subject_get_int(cfg_cur.filter.high) == 3900);
        assert(subject_get_int(cfg_cur.filter.low) == 0);
        assert(subject_get_int(cfg_cur.filter.bw) == 3900);
    }
}

static void test_change_filters_cw() {
    x6200_mode_t modes[] = {x6200_mode_cw, x6200_mode_cwr};
    for (size_t i = 0; i < 2; i++) {
        subject_set_int(cfg_cur.mode, modes[i]);
        subject_set_int(cfg_cur.filter.bw, 400);
        subject_set_int(cfg.key_tone.val, 700);
        assert(subject_get_int(cfg_cur.filter.high) == 700+200);
        assert(subject_get_int(cfg_cur.filter.low) == 700-200);
        assert(subject_get_int(cfg_mode.filter_high.val) == 400);

        subject_set_int(cfg_cur.filter.bw, 200);
        assert(subject_get_int(cfg_cur.filter.high) == 700+100);
        assert(subject_get_int(cfg_cur.filter.low) == 700-100);
        assert(subject_get_int(cfg_mode.filter_high.val) == 200);

        subject_set_int(cfg.key_tone.val, 800);
        assert(subject_get_int(cfg_cur.filter.high) == 800+100);
        assert(subject_get_int(cfg_cur.filter.low) == 800-100);
        assert(subject_get_int(cfg_mode.filter_high.val) == 200);

        subject_set_int(cfg_cur.filter.high, 1000);
        assert(subject_get_int(cfg_cur.filter.bw) == 400);
        assert(subject_get_int(cfg_cur.filter.low) == 600);

        subject_set_int(cfg_cur.filter.low, 550);
        assert(subject_get_int(cfg_cur.filter.bw) == 500);
        assert(subject_get_int(cfg_cur.filter.high) == 1050);

        subject_set_int(cfg_mode.filter_high.val, 100);
        assert(subject_get_int(cfg_cur.filter.bw) == 100);
        assert(subject_get_int(cfg_cur.filter.high) == 800+50);
        assert(subject_get_int(cfg_cur.filter.low) == 800-50);
    }
}

static void test_change_filters_ssb() {
    x6200_mode_t modes[] = {x6200_mode_lsb, x6200_mode_lsb_dig, x6200_mode_usb, x6200_mode_usb_dig};
    for (size_t i = 0; i < 4; i++) {
        subject_set_int(cfg_cur.mode, modes[i]);
        subject_set_int(cfg_cur.filter.low, 100);
        subject_set_int(cfg_cur.filter.high, 2900);
        assert(subject_get_int(cfg_cur.filter.bw) == 2800);
        assert(subject_get_int(cfg_mode.filter_low.val) == 100);
        assert(subject_get_int(cfg_mode.filter_high.val) == 2900);

        subject_set_int(cfg_cur.filter.bw, 2600);
        assert(subject_get_int(cfg_cur.filter.low) == 200);
        assert(subject_get_int(cfg_cur.filter.high) == 2800);
        assert(subject_get_int(cfg_mode.filter_low.val) == 200);
        assert(subject_get_int(cfg_mode.filter_high.val) == 2800);

        subject_set_int(cfg_cur.filter.low, 100);
        assert(subject_get_int(cfg_cur.filter.bw) == 2700);
        assert(subject_get_int(cfg_mode.filter_low.val) == 100);

        subject_set_int(cfg_cur.filter.high, 2900);
        assert(subject_get_int(cfg_cur.filter.bw) == 2800);
        assert(subject_get_int(cfg_mode.filter_high.val) == 2900);

        subject_set_int(cfg_mode.filter_high.val, 2800);
        assert(subject_get_int(cfg_cur.filter.bw) == 2700);
        assert(subject_get_int(cfg_cur.filter.high) == 2800);

        subject_set_int(cfg_mode.filter_low.val, 150);
        assert(subject_get_int(cfg_cur.filter.bw) == 2650);
        assert(subject_get_int(cfg_cur.filter.low) == 150);

    }
}

static void run_tests(void) {
    subject_set_int(cfg.band_id.val, 6);
    printf("Start testing\n");
    CHECK(test_load_band_by_freq());
    CHECK(test_load_band_by_pk());
    CHECK(test_set_another_band());
    CHECK(test_set_another_band_using_freq());
    CHECK(test_switch_band_up());
    CHECK(test_switch_band_down());
    CHECK(test_cur_fg_bg_freq_tracking());
    CHECK(test_ab_freq_tracking_cur());
    CHECK(test_cur_mode_tracking());
    CHECK(test_ab_mode_tracking_cur());
    CHECK(test_change_filters_am_fm());
    CHECK(test_change_filters_cw());
    CHECK(test_change_filters_ssb());
    printf("Testing is done\n");
    exit(0);
}
