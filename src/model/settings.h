/*
 * Pedal settings (waveod's own, see fw/settings_record.h): display brightness, tuner reference and output,
 * touch marker. Loaded at start-up, applied at once when changed, written to the flash on settings_save.
 */
#pragma once

#include "fw/presets.h"
#include <stdint.h>

struct settings {
    uint8_t brightness;                     /* 10..100 % */
    uint16_t a4;                            /* 430..450 Hz */
    uint8_t tuner_mute;                     /* output silenced while tuning */
    uint8_t touch_cursor;                   /* marker under the finger */
    uint8_t scene_mode;                     /* the footswitches start in scene mode (else globals fs_group) */
    uint8_t cab_original;                   /* Cab / User IR on the original's engine instead of waveod's (A / B) */
    uint8_t spill;                          /* spillover: index of settings_spill_ms (0 = off) */
    uint8_t tuner_strobe;                   /* the tuner shows a strobe instead of the needle */
    uint8_t scene_manual;                   /* KNOBS NA CENA: MANUAL (else AUTO: a parameter turned while the scenes
                                               are in play becomes a scene parameter; rig.scene_auto) */
    uint8_t drum_user;                      /* the drum machine plays MEUS RITMOS place drum_user - 1 (0 = the bank's
                                               pattern, chosen in the globals shared with the original) */
};

#define SETTINGS_SPILL_CHOICES 5
extern const uint16_t settings_spill_ms[SETTINGS_SPILL_CHOICES];     /* 0 (off), 2, 5, 10, 20 s */

void settings_load(struct settings *s, const struct preset_source *src);
void settings_apply(const struct settings *s);

/* counts settings_apply calls (the UI redraws, and sets what it owns, when it moves) */
uint32_t settings_generation(void);

/* Write when different from what is stored. 0 = stored (or nothing to do). Mutes the audio briefly. */
int settings_save(const struct settings *s, struct preset_source *src);

/* Changed outside the settings screen: settings_poll writes it after a pause. */
void settings_touch(void);
void settings_poll(const struct settings *s, struct preset_source *src);
