/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6200 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <aether_radio/x6200_control/control.h>

#include "lvgl/lvgl.h"

#define RADIO_SAMPLES   (512)

typedef enum {
    RADIO_RX = 0,
    RADIO_TX,
    RADIO_ATU_START,
    RADIO_ATU_WAIT,
    RADIO_ATU_RUN,
    RADIO_SWRSCAN,

    RADIO_POWEROFF,
    RADIO_OFF
} radio_state_t;

typedef enum {
    RADIO_CHARGER_OFF = 0,
    RADIO_CHARGER_ON,
    RADIO_CHARGER_SHADOW
} radio_charger_t;

typedef void (*radio_state_change_t) ();

void radio_init(radio_state_change_t tx_cb, radio_state_change_t rx_cb);
void radio_bb_reset();
bool radio_tick();
radio_state_t radio_get_state();

/**
 * Set freq for radio without updating corresponding subject.
 * Useful for FT8 TX freq change and SWR scan
 */
void radio_set_freq(int32_t freq);
bool radio_check_freq(int32_t freq);

x6200_vfo_t radio_toggle_vfo();

uint16_t radio_change_vol(int16_t df);
uint16_t radio_change_moni(int16_t df);
bool radio_change_spmode(int16_t df);

void radio_change_mute();

void radio_set_pwr(float d);

radio_charger_t radio_change_charger(int16_t d);

x6200_mic_sel_t radio_change_mic(int16_t d);
uint8_t radio_change_hmic(int16_t d);
uint8_t radio_change_imic(int16_t d);

void radio_start_atu();

bool radio_start_swrscan();
void radio_stop_swrscan();

void radio_poweroff();
void radio_set_ptt(bool tx);
// void radio_set_modem(bool tx);


int16_t radio_change_xit(int16_t d);
int16_t radio_change_rit(int16_t d);

void radio_set_line_in(uint8_t d);
void radio_set_line_out(uint8_t d);

void radio_set_morse_key(bool on);
