/*
 * Raw state of the physical controls. Edge detection and encoder detents are done in src/input.
 */
#pragma once

#include <stdint.h>

/* Buttons and footswitches (bit set = pressed). */
enum {
    BTN_MENU      = 1u << 0,    /* push of the big encoder */
    BTN_EXIT      = 1u << 1,
    BTN_EFX       = 1u << 2,
    BTN_SAVE      = 1u << 3,
    BTN_DRUM      = 1u << 4,
    BTN_GLOBAL    = 1u << 5,
    BTN_CTRL      = 1u << 6,
    BTN_EXP       = 1u << 7,
    BTN_FS_A      = 1u << 8,
    BTN_FS_B      = 1u << 9,
    BTN_FS_C      = 1u << 10,
    BTN_FS_D      = 1u << 11,
    BTN_BANK_UP   = 1u << 12,
    BTN_BANK_DOWN = 1u << 13,
    BTN_EXT1      = 1u << 14,   /* footswitches on the EXP 2 jack (global setting: jack mode) */
    BTN_EXT2      = 1u << 15,
    BTN_COUNT     = 16
};

/* Endless encoders. */
enum { ENC_MENU, ENC_KNOB1, ENC_KNOB2, ENC_KNOB3, ENC_COUNT };

/* Potentiometers / pedals, 12-bit. */
enum { POT_MASTER, POT_EXP1, POT_EXP2, POT_COUNT };

struct controls_raw {
    uint32_t buttons;
    int32_t enc[ENC_COUNT];         /* quadrature counts, 4 per detent */
    uint16_t pot[POT_COUNT];
};

void controls_read(struct controls_raw *out);
