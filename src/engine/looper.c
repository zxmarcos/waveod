#include "engine/looper.h"
#include "platform/sdram.h"
#include <string.h>

#define HALF_SAMPLES   (LOOPER_SAMPLES / 2)    /* 0x3C9000: the loop's part when undo is on */
#define REC_SCALE      109.22666931152344f     /* 32768 / 300 (DAT_0002BA98) */
#define PLAY_SCALE     9.155273232863692e-07f  /* 300 / 32768 / 1e4 (DAT_0002BA9C) */
#define TRIGGER_LEVEL  0.008f
#define MERGE_BLOCK    256                      /* samples merged as a unit */
#define MERGE_BLOCKS   (HALF_SAMPLES / MERGE_BLOCK)
#define MERGE_PER_CALL 16                       /* merged per audio block besides the part being played */
#define WAVE_SHIFT     9                        /* the waveform: one peak per 512 samples of the buffer */
#define WAVE_BINS      (LOOPER_SAMPLES >> WAVE_SHIFT)

#ifdef WAVEOD_HOST
#define LOOPER_MEM
#else
#define LOOPER_MEM __attribute__((section(".looper"), aligned(32)))
#endif

static int16_t buffer[LOOPER_SAMPLES] LOOPER_MEM;

/* peaks (|sample| >> 7) of the loop and of the last overdub, for the screen */
static uint8_t wave_base[WAVE_BINS] SDRAM_BSS, wave_layer[WAVE_BINS] SDRAM_BSS;

static inline void peak(uint8_t *bins, uint32_t i, int32_t v)
{
    uint8_t a = (uint8_t)((v < 0 ? -v : v) >> 7), *p = &bins[i >> WAVE_SHIFT];
    if (!(i & ((1u << WAVE_SHIFT) - 1)))
        *p = 0;                             /* the bin's first sample: it starts again */
    if (a > *p)
        *p = a;
}

static struct {
    volatile uint8_t cmd, armed, undo_req;
    uint8_t auto_rec;                   /* +0x101: recording waits for the input (main loop only) */
    volatile uint8_t state, place, channels;
    uint8_t want_place, want_undo;
    uint8_t layer_present, layer_hidden;
    int16_t *layer;                     /* last overdub, 0 without undo */
    uint32_t max;                       /* samples (frames x channels) */
    volatile uint32_t pos, len;         /* samples */
    float rec_target, rec_gain, play_target, play_gain;
    float env;
    /* merging the overdub before last into the loop: once per block of the layer, by the interrupt */
    uint8_t merging;
    uint8_t discard;                    /* the old layer was taken back: drop it instead of adding it */
    uint32_t merge_next;
    uint8_t merged[MERGE_BLOCKS];
} lp;

void looper_init(void)
{
    memset(&lp, 0, sizeof lp);
    lp.channels = 1;
    lp.max = LOOPER_SAMPLES;
    looper_set(100, 100, 0, 1);
}

void looper_set(int rec_level, int play_level, int post, int undo)
{
    lp.rec_target = (float)(rec_level < 0 ? 0 : rec_level > 100 ? 100 : rec_level) * REC_SCALE;
    lp.play_target = (float)(play_level < 0 ? 0 : play_level > 100 ? 100 : play_level) * PLAY_SCALE;
    lp.want_place = (uint8_t)(post != 0);
    lp.want_undo = (uint8_t)(undo != 0);
}

void looper_command(int cmd)
{
    lp.cmd |= (uint8_t)cmd;
}

void looper_arm(int on)
{
    if (on)
        lp.env = 0.0f;
    lp.armed = (uint8_t)(on != 0);
}

void looper_set_auto(int on)
{
    lp.auto_rec = (uint8_t)(on != 0);
}

int looper_auto(void)
{
    return lp.auto_rec;
}

void looper_undo(void)
{
    lp.undo_req = 1;
}

void looper_status(struct looper_status *out)
{
    out->state = lp.state;
    out->place = lp.place;
    out->armed = lp.armed;
    out->layer = lp.layer_present ? (lp.layer_hidden ? 2 : 1) : 0;
    uint32_t c = lp.channels ? lp.channels : 1;
    out->pos = lp.pos / c;
    out->len = lp.len / c;
    out->max = lp.max / c;
}

void looper_wave(uint8_t *out, int columns, uint32_t span)
{
    uint32_t c = lp.channels ? lp.channels : 1, n = span * c, len = lp.state == LOOP_REC ? lp.pos : lp.len;
    int layer = lp.layer && lp.layer_present && !lp.layer_hidden;
    for (int x = 0; x < columns; x++) {
        uint32_t i0 = (uint32_t)((uint64_t)n * (uint32_t)x / (uint32_t)columns);
        uint32_t i1 = (uint32_t)((uint64_t)n * (uint32_t)(x + 1) / (uint32_t)columns);
        uint8_t m = 0;
        if (i1 > len)
            i1 = len;
        for (uint32_t b = i0 >> WAVE_SHIFT; (b << WAVE_SHIFT) < i1 && b < WAVE_BINS; b++) {
            uint8_t v = wave_base[b], l = layer ? wave_layer[b] : 0;
            v = l > v ? l : v;
            m = v > m ? v : m;
        }
        out[x] = m;
    }
}

static inline int16_t sat16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* the layer of merge block b goes into the loop (or away) */
static void merge_block(uint32_t b)
{
    if (lp.merged[b])
        return;
    lp.merged[b] = 1;
    uint32_t i0 = b * MERGE_BLOCK, i1 = i0 + MERGE_BLOCK;
    for (uint32_t i = i0; i < i1; i++) {
        if (!lp.discard)
            buffer[i] = sat16(buffer[i] + lp.layer[i]);
        lp.layer[i] = 0;
    }
    uint32_t w = i0 >> WAVE_SHIFT;          /* the sum's peak, near enough: the louder of the two */
    if (!lp.discard && wave_layer[w] > wave_base[w])
        wave_base[w] = wave_layer[w];
    if (!(i1 & ((1u << WAVE_SHIFT) - 1)))
        wave_layer[w] = 0;
}

static void merge_range(uint32_t from, uint32_t n)
{
    if (!lp.merging)
        return;
    for (uint32_t b = from / MERGE_BLOCK, e = (from + n - 1) / MERGE_BLOCK; b <= e && b < MERGE_BLOCKS; b++)
        merge_block(b);
}

static void merge_background(void)
{
    if (!lp.merging)
        return;
    uint32_t blocks = (lp.len + MERGE_BLOCK - 1) / MERGE_BLOCK;
    for (int k = 0; k < MERGE_PER_CALL && lp.merge_next < blocks; k++)
        merge_block(lp.merge_next++);
    if (lp.merge_next >= blocks)
        lp.merging = 0;
}

static void start_dub(void)
{
    if (lp.layer && lp.layer_present) {         /* the overdub before goes into the loop as this one runs */
        lp.merging = 1;
        lp.discard = lp.layer_hidden;
        lp.merge_next = 0;
        memset(lp.merged, 0, sizeof lp.merged);
    }
    lp.layer_present = lp.layer != 0;
    lp.layer_hidden = 0;
    lp.state = LOOP_DUB;
}

/* the state machine (FUN_0002B98C): commands, settings of an empty looper */
static void step(void)
{
    uint8_t cmd = lp.cmd;
    lp.cmd = 0;
    if (lp.undo_req) {
        lp.undo_req = 0;
        if (lp.state != LOOP_DUB && lp.layer_present)
            lp.layer_hidden = !lp.layer_hidden;
    }
    if (cmd & LOOP_CMD_CLEAR) {
        lp.state = LOOP_EMPTY;
        lp.pos = lp.len = 0;
        lp.layer_present = lp.layer_hidden = 0;
        lp.merging = 0;
        cmd = 0;
    }
    if (lp.state == LOOP_EMPTY) {
        lp.place = lp.want_place;
        lp.channels = lp.place == LOOP_POST ? 2 : 1;
        lp.layer = lp.want_undo ? buffer + HALF_SAMPLES : 0;
        lp.max = lp.want_undo ? HALF_SAMPLES : LOOPER_SAMPLES;
        lp.max -= lp.max % 2;
        if (cmd & (LOOP_CMD_REC | LOOP_CMD_DUB)) {
            lp.pos = 0;
            lp.layer_present = 0;
            lp.rec_gain = 0.0f;
            lp.state = LOOP_REC;
        }
        return;
    }
    switch (lp.state) {
    case LOOP_REC:
        if (cmd & (LOOP_CMD_PLAY | LOOP_CMD_DUB | LOOP_CMD_REC | LOOP_CMD_STOP)) {
            lp.len = lp.pos;
            lp.pos = 0;
            lp.state = cmd & LOOP_CMD_STOP ? LOOP_STOPPED : cmd & LOOP_CMD_DUB ? LOOP_DUB : LOOP_PLAY;
            if (!lp.len)
                lp.state = LOOP_EMPTY;
        }
        break;
    case LOOP_PLAY:
        if (cmd & LOOP_CMD_STOP) {
            lp.pos = 0;
            lp.state = LOOP_STOPPED;
        } else if (cmd & (LOOP_CMD_DUB | LOOP_CMD_REC)) {
            start_dub();
        }
        break;
    case LOOP_DUB:
        if (cmd & LOOP_CMD_STOP) {
            lp.pos = 0;
            lp.state = LOOP_STOPPED;
        } else if (cmd & LOOP_CMD_PLAY) {
            lp.state = LOOP_PLAY;
        }
        break;
    case LOOP_STOPPED:
        if (cmd & (LOOP_CMD_PLAY | LOOP_CMD_DUB | LOOP_CMD_REC)) {
            lp.pos = 0;
            lp.play_gain = 0.0f;
            lp.state = LOOP_PLAY;
        }
        break;
    default:
        break;
    }
}

void looper_block(const float *in, int frames)
{
    if (lp.armed && (lp.state == LOOP_EMPTY || lp.state == LOOP_PLAY)) {   /* FUN_0002B318: record / overdub */
        float env = lp.env;
        for (int f = 0; f < frames; f++) {
            float x = in[2 * f] < 0 ? -in[2 * f] : in[2 * f];
            env = x + (env - x) * (x > env ? 0.995f : 0.999f);
        }
        lp.env = env;
        if (env > TRIGGER_LEVEL) {
            lp.cmd |= LOOP_CMD_REC;
            lp.armed = 0;
        }
    }
    step();
    merge_background();
}

void looper_process(float *io, int frames, int place)
{
    uint8_t st = lp.state;
    if (place != lp.place || (st != LOOP_REC && st != LOOP_PLAY && st != LOOP_DUB))
        return;
    int c = lp.channels;
    float rg = lp.rec_gain, rs = (lp.rec_target - rg) / (float)frames;
    float pg = lp.play_gain, ps = (lp.play_target - pg) / (float)frames;
    uint32_t pos = lp.pos;

    if (st == LOOP_REC) {
        for (int f = 0; f < frames && pos + (uint32_t)c <= lp.max; f++) {
            rg += rs;
            for (int k = 0; k < c; k++) {
                buffer[pos] = sat16((int32_t)(io[2 * f + k] * rg));
                peak(wave_base, pos, buffer[pos]);
                if (lp.layer) {
                    lp.layer[pos] = 0;
                    peak(wave_layer, pos, 0);
                }
                pos++;
            }
        }
        lp.pos = pos;
        lp.rec_gain = lp.rec_target;
        if (pos + (uint32_t)c > lp.max)         /* full: play what was recorded */
            lp.cmd |= LOOP_CMD_PLAY;
        return;
    }

    uint32_t len = lp.len;
    if (st == LOOP_DUB)
        merge_range(pos, (uint32_t)(frames * c) <= len - pos ? (uint32_t)(frames * c) : len - pos);
    int show_layer = lp.layer && lp.layer_present && !lp.layer_hidden;
    for (int f = 0; f < frames; f++) {
        if (pos >= len) {
            pos = 0;
            if (st == LOOP_DUB)
                merge_range(0, (uint32_t)((frames - f) * c));
        }
        pg += ps;
        rg += rs;
        for (int k = 0; k < c; k++) {
            uint32_t i = pos + (uint32_t)k;
            int32_t v = buffer[i] + (show_layer ? lp.layer[i] : 0);
            float in = io[2 * f + k];
            if (st == LOOP_DUB) {
                int16_t *dst = lp.layer ? &lp.layer[i] : &buffer[i];
                *dst = sat16(*dst + (int32_t)(in * rg));
                uint8_t a = (uint8_t)((*dst < 0 ? -*dst : *dst) >> 7), *p = &(lp.layer ? wave_layer : wave_base)[i >> WAVE_SHIFT];
                if (a > *p)
                    *p = a;
            }
            float out = (float)v * pg;
            if (c == 1) {
                io[2 * f] += out;
                io[2 * f + 1] += out;
            } else {
                io[2 * f + k] += out;
            }
        }
        pos += (uint32_t)c;
    }
    lp.pos = pos >= len ? 0 : pos;
    lp.play_gain = lp.play_target;
    lp.rec_gain = lp.rec_target;
}
