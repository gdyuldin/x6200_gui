#include "controls.h"
#include "cfg/subjects.h"

extern "C" {
#include "cfg/cfg.h"
#include "msg.h"
#include "voice.h"
}

static inline bool toggle_subj(Subject *subj);


void controls_toggle_agc_hang(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.agc_hang.val);
    voice_say_bool("Auto gain hang", new_val);
}

void controls_toggle_key_train(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.key_train.val);
    voice_say_bool("CW key train", new_val);
}

void controls_toggle_key_iambic_mode(button_item_t *btn) {
    x6200_iambic_mode_t new_mode = subject_get_int(cfg.iambic_mode.val) == x6200_iambic_a ? x6200_iambic_b : x6200_iambic_a;
    subject_set_int(cfg.iambic_mode.val, new_mode);
    char *str = params_iambic_mode_str_ger(new_mode);
    voice_say_text("Iambic mode", str);
}

void controls_toggle_cw_decoder(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.cw_decoder.val);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_cw_tuner(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.cw_tune.val);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_dnf(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.dnf.val);
    voice_say_bool("DNF", new_val);
}

void controls_toggle_dnf_auto(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.dnf_auto.val);
    voice_say_bool("DNF auto", new_val);
}

void controls_toggle_nb(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.nb.val);
    voice_say_bool("NB", new_val);
}

void controls_toggle_nr(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.nr.val);
    voice_say_bool("NR", new_val);
}

void controls_toggle_att(button_item_t *btn) {
    bool new_val = toggle_subj(cfg_cur.att);
    voice_say_bool("ATT", new_val);
}

void controls_toggle_pre(button_item_t *btn) {
    bool new_val = toggle_subj(cfg_cur.pre);
    voice_say_bool("PRE", new_val);
}

void controls_toggle_comp(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.comp.val);
    voice_say_bool("COMP", new_val);
}

static inline bool toggle_subj(Subject *subj) {
    bool new_val = !subject_get_int(subj);
    subject_set_int(subj, new_val);
    return new_val;
}
