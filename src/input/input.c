#include "input/input.h"
#include "platform/time.h"

#define COUNTS_PER_DETENT 4

static struct {
    uint32_t buttons;
    uint32_t pressed_at[BTN_COUNT];
    uint32_t held;                      /* buttons whose long press was already reported */
    int32_t enc_last[ENC_COUNT];
    int32_t enc_rest[ENC_COUNT];        /* counts not yet worth a detent */
    uint32_t enc_time[ENC_COUNT];
    float pot[POT_COUNT];
    int primed;
} in;

void input_init(void)
{
    struct controls_raw raw;
    controls_read(&raw);
    in.buttons = raw.buttons;
    for (int i = 0; i < ENC_COUNT; i++)
        in.enc_last[i] = raw.enc[i];
    for (int i = 0; i < POT_COUNT; i++)
        in.pot[i] = raw.pot[i] / 4095.0f;
    in.primed = 1;
}

static int push(struct input_event *ev, int n, int max, uint8_t type, uint8_t id, int16_t delta, int16_t fast)
{
    if (n < max) {
        ev[n].type = type;
        ev[n].id = id;
        ev[n].delta = delta;
        ev[n].fast = fast;
        n++;
    }
    return n;
}

int input_poll(struct input_event *ev, int max)
{
    if (!in.primed)
        input_init();
    struct controls_raw raw;
    controls_read(&raw);
    uint32_t now = time_ms();
    int n = 0;

    uint32_t changed = raw.buttons ^ in.buttons;
    for (uint8_t id = 0; id < BTN_COUNT; id++) {
        uint32_t m = 1u << id;
        if (changed & m) {
            if (raw.buttons & m) {
                in.pressed_at[id] = now;
                in.held &= ~m;
                n = push(ev, n, max, IN_PRESS, id, 0, 0);
            } else {
                n = push(ev, n, max, IN_RELEASE, id, 0, 0);
            }
        } else if ((raw.buttons & m) && !(in.held & m) && now - in.pressed_at[id] >= INPUT_HOLD_MS) {
            in.held |= m;
            n = push(ev, n, max, IN_HOLD, id, 0, 0);
        }
    }
    in.buttons = raw.buttons;

    for (uint8_t e = 0; e < ENC_COUNT; e++) {
        int32_t counts = raw.enc[e] - in.enc_last[e] + in.enc_rest[e];
        in.enc_last[e] = raw.enc[e];
        int32_t detents = counts / COUNTS_PER_DETENT;
        in.enc_rest[e] = counts - detents * COUNTS_PER_DETENT;
        if (!detents)
            continue;
        uint32_t dt = now - in.enc_time[e];
        in.enc_time[e] = now;
        int mult = dt < 25 ? 8 : dt < 50 ? 4 : dt < 90 ? 2 : 1;
        n = push(ev, n, max, IN_TURN, e, (int16_t)detents, (int16_t)(detents * mult));
    }

    for (int i = 0; i < POT_COUNT; i++)
        in.pot[i] += (raw.pot[i] / 4095.0f - in.pot[i]) * 0.3f;
    return n;
}

float input_pot(int pot)
{
    return pot >= 0 && pot < POT_COUNT ? in.pot[pot] : 0.0f;
}
