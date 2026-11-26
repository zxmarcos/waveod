/*
 * The footswitch LEDs: one RGB LED per footswitch key (A-D, BANK -, BANK +) and the EXP 1 A / B indicator.
 * Each colour component is on or off (no brightness), so a LED shows 8 colours. A change shows within one
 * control poll (5 ms).
 */
#pragma once

enum { LED_FS_A, LED_FS_B, LED_FS_C, LED_FS_D, LED_BANK_DOWN, LED_BANK_UP, LED_EXP1, LED_COUNT };

/* bit 0 = red, 1 = green, 2 = blue, as the original's codes */
enum led_colour {
    LED_OFF, LED_RED, LED_GREEN, LED_YELLOW, LED_BLUE, LED_MAGENTA, LED_CYAN, LED_WHITE,
};

void leds_set(int led, int colour);         /* LED_EXP1 has no blue */
