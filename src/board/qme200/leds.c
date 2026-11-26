/*
 * Footswitch LEDs (platform/leds.h) on the button / LED shift register chain, as the original drives them
 * (led_set = FUN_00022BB4, pin table 0x47162): virtual pin n is bit (n & 7) of shift_reg_out[n >> 3], the LEDs
 * are active low (0 = lit). controls.c sends shift_reg_out with every key read. Only the main loop writes
 * shift_reg_out (here and audio_set_input_mode, which shares byte 3).
 */
#include "board/qme200/board.h"
#include "platform/leds.h"

#define NO_PIN 0xff

/* red, green, blue pin of each LED */
static const uint8_t pins[LED_COUNT][3] = {
    { 0x09, 0x0a, 0x0b },       /* A */
    { 0x0e, 0x0d, 0x0c },       /* B */
    { 0x03, 0x02, 0x01 },       /* C */
    { 0x04, 0x05, 0x06 },       /* D */
    { 0x11, 0x12, 0x13 },       /* BANK - */
    { 0x16, 0x15, 0x14 },       /* BANK + */
    { 0x1d, 0x1c, NO_PIN },     /* EXP 1 A / B: red and green only */
};

void leds_set(int led, int colour)
{
    if (led < 0 || led >= LED_COUNT)
        return;
    for (int c = 0; c < 3; c++) {
        uint8_t pin = pins[led][c];
        if (pin == NO_PIN)
            continue;
        uint8_t bit = (uint8_t)(1u << (pin & 7));
        if (colour & (1 << c))
            shift_reg_out[pin >> 3] &= (uint8_t)~bit;
        else
            shift_reg_out[pin >> 3] |= bit;
    }
}
