#include "input/remote.h"
#include "platform/time.h"

#define COUNTS_PER_DETENT 4             /* input.c's */

static struct {
    uint32_t buttons;
    int32_t enc[ENC_COUNT];             /* counts added since start-up (they stay: input.c works on differences) */
    int8_t touch;
    int16_t x, y;
    uint16_t pot[POT_COUNT];
    uint32_t at;                        /* time of the last message */
    uint8_t live;
} rm;

void remote_set(uint32_t buttons, const int32_t turn[ENC_COUNT], int touch, int x, int y,
                const uint16_t pot[POT_COUNT])
{
    rm.buttons = buttons;
    for (int e = 0; e < ENC_COUNT; e++)
        rm.enc[e] += turn[e] * COUNTS_PER_DETENT;
    rm.touch = (int8_t)(touch < REMOTE_TOUCH_NONE || touch > REMOTE_TOUCH_DOWN ? REMOTE_TOUCH_NONE : touch);
    rm.x = (int16_t)x;
    rm.y = (int16_t)y;
    for (int i = 0; i < POT_COUNT; i++)
        rm.pot[i] = pot[i] > 4095 ? REMOTE_POT_NONE : pot[i];
    rm.at = time_ms();
    rm.live = 1;
}

static int live(void)
{
    if (rm.live && time_ms() - rm.at > REMOTE_TIMEOUT_MS) {
        rm.live = 0;
        rm.buttons = 0;
        rm.touch = REMOTE_TOUCH_NONE;
    }
    return rm.live;
}

void remote_apply(struct controls_raw *raw)
{
    for (int e = 0; e < ENC_COUNT; e++)
        raw->enc[e] += rm.enc[e];
    if (!live())
        return;
    raw->buttons |= rm.buttons;
    for (int i = 0; i < POT_COUNT; i++)
        if (rm.pot[i] != REMOTE_POT_NONE)
            raw->pot[i] = rm.pot[i];
}

void remote_touch(struct touch_point *p)
{
    if (!live() || rm.touch == REMOTE_TOUCH_NONE)
        return;
    p->pressed = rm.touch == REMOTE_TOUCH_DOWN;
    p->x = rm.x;
    p->y = rm.y;
}
