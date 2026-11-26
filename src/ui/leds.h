/*
 * What the footswitch LEDs show, after the original (docs/architecture.md, "Footswitch LEDs"):
 *   - normal screens: each key's LED follows its press links in the footswitch mode (blocks / parameters: lit
 *     while the key's state is on; functions as the original shows them, FUN_0003C484); LED 6 always shows the
 *     EXP 1 group;
 *   - the tuner and a warning on screen (the overload screen 0x0F): all off;
 *   - the looper screen: A and B show the looper, D its AUTO REC.
 * Blinking works like led_config / led_tick (FUN_00022B14 / FUN_00022B44): a toggle every 30000 / rate ms,
 * forever or a number of times.
 */
#pragma once

struct ui_ctx;

void leds_tick(struct ui_ctx *ui);          /* every loop */

/* The active preset's footswitch again switched the preset CTRL: the LED of the preset's letter (A-D)
   blinks white, then stays white. */
void leds_preset_ctrl(int letter);
