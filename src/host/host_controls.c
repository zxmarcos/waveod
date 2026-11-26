/* Simulator: control state set by the script. */
#include "host/host.h"
#include "platform/controls.h"


static struct controls_raw controls = { 0, { 0 }, { 2048, 0, 0 } };

void controls_read(struct controls_raw *out) { *out = controls; }

void host_buttons_set(uint32_t mask, int pressed)
{
    if (pressed)
        controls.buttons |= mask;
    else
        controls.buttons &= ~mask;
}

void host_encoder_turn(int enc, int detents)
{
    if (enc >= 0 && enc < ENC_COUNT)
        controls.enc[enc] += 4 * detents;
}

void host_pot_set(int pot, uint16_t value)
{
    if (pot >= 0 && pot < POT_COUNT)
        controls.pot[pot] = value;
}
