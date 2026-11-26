#include "engine/scope.h"
#include "platform/sdram.h"

static struct {
    const struct chain *volatile chain;
    volatile int8_t target;
    volatile uint32_t head;                     /* written by the interrupt */
    uint32_t tail;                              /* read by the main loop */
} sc = { 0, SCOPE_OFF, 0, 0 };

/* 128 samples per block written, a few thousand read per frame: the SDRAM (cached) is fast enough */
static struct {
    float before[SCOPE_RING], after[SCOPE_RING];
} ring SDRAM_BSS;

void scope_watch(const struct chain *c, int target)
{
    if (c == sc.chain && target == sc.target)
        return;
    sc.target = SCOPE_OFF;                      /* the interrupt stops writing before the rest changes */
    sc.chain = c;
    sc.tail = sc.head;
    sc.target = (int8_t)target;
}

int scope_target(void)
{
    return sc.target;
}

int scope_read(float *before, float *after, int max)
{
    uint32_t head = sc.head;
    if (head - sc.tail > SCOPE_RING)            /* the reader fell behind: keep the newest */
        sc.tail = head - SCOPE_RING;
    int n = 0;
    while (sc.tail != head && n < max) {
        uint32_t i = sc.tail++ & (SCOPE_RING - 1);
        before[n] = ring.before[i];
        after[n++] = ring.after[i];
    }
    return n;
}

int scope_taps(const struct chain *c, int slot)
{
    return sc.target >= 0 && sc.target == slot && c == sc.chain;
}

static inline float mono(const float *x, int ch, int f)
{
    return ch == 2 ? 0.5f * (x[2 * f] + x[2 * f + 1]) : x[f];
}

void scope_push(const float *before, int before_ch, const float *after, int after_ch, int frames)
{
    uint32_t h = sc.head;
    for (int f = 0; f < frames; f++, h++) {
        ring.before[h & (SCOPE_RING - 1)] = mono(before, before_ch, f);
        ring.after[h & (SCOPE_RING - 1)] = mono(after, after_ch, f);
    }
    sc.head = h;
}
