#pragma once

#include "buttons.h"

#ifdef __cplusplus
extern "C" {
#endif


void controls_toggle_agc_hang(button_item_t *btn);
void controls_toggle_key_train(button_item_t *btn);
void controls_toggle_key_iambic_mode(button_item_t *btn);
void controls_toggle_cw_decoder(button_item_t *btn);
void controls_toggle_cw_tuner(button_item_t *btn);

void controls_toggle_dnf(button_item_t *btn);
void controls_toggle_dnf_auto(button_item_t *btn);
void controls_toggle_nb(button_item_t *btn);
void controls_toggle_nr(button_item_t *btn);

void controls_toggle_att(button_item_t *btn);
void controls_toggle_pre(button_item_t *btn);
void controls_toggle_comp(button_item_t *btn);

#ifdef __cplusplus
}
#endif
