/* Simulator: the footswitch LEDs, printed by the script's "leds" command. */
#include "host/host.h"
#include "platform/leds.h"
#include <stdio.h>

static int colours[LED_COUNT];

void leds_set(int led, int colour)
{
    if (led >= 0 && led < LED_COUNT)
        colours[led] = led == LED_EXP1 ? colour & 3 : colour & 7;
}

void host_leds_text(char *out, unsigned size)
{
    static const char *const keys[LED_COUNT] = { "A", "B", "C", "D", "BANK-", "BANK+", "EXP" };
    static const char *const names[8] = { "off", "red", "green", "yellow", "blue", "magenta", "cyan", "white" };
    unsigned n = 0;
    for (int i = 0; i < LED_COUNT && n < size; i++)
        n += (unsigned)snprintf(out + n, size - n, "%s%s=%s", i ? " " : "", keys[i], names[colours[i]]);
}
