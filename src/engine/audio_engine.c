#include "engine/audio_engine.h"
#include "engine/modulators.h"
#include "dsp/biquad.h"
#include "dsp/global_eq.h"
#include "dsp/io_filter.h"
#include "engine/drums.h"
#include "engine/host_abi.h"
#include "engine/latency.h"
#include "engine/looper.h"
#include "engine/overload.h"
#include "engine/scope.h"
#include "platform/audio.h"
#include "platform/time.h"
#include "usb/usb_audio.h"
#include <string.h>

#define FULL_SCALE      980586112.0f        /* 0x4E69CA3A in the original loop */
#define IN_SCALE        (1.0f / FULL_SCALE)
#define CONV_SCALE      2147483648.0f       /* the converters' full scale: a sample of 1.0 is 6.8 dB below it */
#define GAIN_STEP       0.01f               /* input / AUX gain change per block (DAT_00032230) */
#define SUSPEND_WAIT_MS 30
#define TUNER_RING      2048                /* guitar samples at 22.05 kHz for the tuner (93 ms) */

/* spillover */
#define SPILL_TAILS     2                   /* the one ringing out and the one being cut (rapid changes) */
#define SPILL_IN_FADE   4                   /* blocks the guitar takes to leave the old preset (5.8 ms) */
/* The whole old chain runs until the slots before its tails are silent (their output under SPILL_FLOOR for
   SPILL_STILL blocks; an amp, its cab and EQ take 100-300 ms) or SPILL_DRAIN_MS have passed (a block with a
   noise of its own), and only then the tails alone: earlier, what those slots still put out would be lost */
#define SPILL_DRAIN_MS  300
#define SPILL_STILL     4
#define SPILL_CUT       4                   /* blocks to fade out a tail cut by the next change */
#define SPILL_END_MS    1000                /* the fade at the end of the time allowed */
#define SPILL_QUIET_MS  2500                /* silence this long ends a tail (longer than the delays' repeats) */
#define SPILL_FLOOR     1e-5f               /* -100 dB of full scale */
#define SPILL_CPU_MS    1.05f               /* a slower block cuts the tails (the overload guard counts 1.25) */
#define BLOCKS_OF_MS(ms) ((uint32_t)((ms) * (float)AUDIO_RATE / (1000.0f * AUDIO_FRAMES)) + 1u)

struct spill {
    struct chain *volatile chain;           /* 0 = free; a copy of the chain that ran (chain_tail) */
    uint16_t skip;                          /* slots left out after the drain */
    uint32_t blocks, quiet, still;
    float level_l, level_r;                 /* the old preset's volume / pan */
    float gain, step;                       /* 1, down by `step` per block when it ends */
};

/* input level -20..+20 dB (FUN_00026C0C, table 0x47070) */
static const float level_db[41] = {
    0.10000000149011612f, 0.11220184713602066f, 0.1258925348520279f, 0.1412537544965744f, 0.15848931670188904f,
    0.17782793939113617f, 0.19952623546123505f, 0.223872110247612f, 0.25118863582611084f, 0.2818382978439331f,
    0.3162277638912201f, 0.3548133969306946f, 0.3981071710586548f, 0.4466835856437683f, 0.5011872053146362f,
    0.5623413324356079f, 0.6309573650360107f, 0.7079457640647888f, 0.7943282127380371f, 0.8912509083747864f,
    1.0f, 1.1220184564590454f, 1.258925437927246f, 1.4125375747680664f, 1.5848932266235352f,
    1.778279423713684f, 1.9952622652053833f, 2.2387211322784424f, 2.5118863582611084f, 2.818382978439331f,
    3.1622776985168457f, 3.5481338500976562f, 3.981071710586548f, 4.466835975646973f, 5.011872291564941f,
    5.6234130859375f, 6.309573650360107f, 7.079457759857178f, 7.943282127380371f, 8.912508964538574f, 10.0f,
};

static struct {
    struct chain *chain;
    float cycles_to_ms;
    volatile uint8_t want_run;              /* main loop -> interrupt */
    volatile uint8_t running;               /* interrupt -> main loop */
    float in_gain, in_target;               /* input level */
    float aux_gain, aux_target;             /* AUX IN level */
    float rec_gain, rec_target;             /* USB recording level (channels 1 / 2) */
    float mon_gain, mon_target;             /* USB playback into the outputs */
    volatile uint8_t usb_rec_aux, usb_rec_wet[2];
    float preset_l, preset_r;               /* preset volume / pan: targets and current gains */
    float level_l, level_r;
    float master;                           /* MASTER, target */
    float out_gain;                         /* MASTER x output fade x full scale, current */
    float fade;                             /* the chain's output, 0..1, ramps in after a resume */
    float out_fade;                         /* the output, 0..1: down with the chain, up while a tail rings */
    struct spill spill[SPILL_TAILS];
    struct chain *volatile spill_req;       /* main loop -> interrupt: take this tail */
    volatile uint8_t spill_stop;            /* main loop -> interrupt: cut every tail */
    volatile uint8_t spill_on;              /* interrupt: tails running */
    uint32_t spill_max;                     /* blocks a tail may ring (0 = no spillover) */
    float spill_in[2 * AUDIO_FRAMES], spill_aux[2 * AUDIO_FRAMES];   /* the input of this block */
    float eq_in[2 * AUDIO_FRAMES];          /* the global EQ's input, for the analyser */
    float spill_io[2 * AUDIO_FRAMES], spill_tmp[2 * AUDIO_FRAMES];
    float spill_send[2 * AUDIO_FRAMES], spill_ret[2 * AUDIO_FRAMES];
    float *spill_extra[2];
    float spill_mix[2 * AUDIO_FRAMES];      /* the tails' sum, added after the preset level */
    volatile uint8_t no_cab;                /* CAB_OFF_x */
    volatile uint8_t eq_on;
    float eq_params[EQ_PARAMS];
    struct global_eq eq;
    struct biquad_state in_state[2], out_state[2], send_state[2];
    struct engine_stats stats;
    struct overload_guard guard;
    volatile uint8_t overloaded;            /* interrupt -> main loop: the guard switched the chain off */
    float io[2 * AUDIO_FRAMES];
    float aux[2 * AUDIO_FRAMES];
    float fx_send[2 * AUDIO_FRAMES];
    float fx_return[2 * AUDIO_FRAMES];
    float aux_in[2 * AUDIO_FRAMES];
    float dry[AUDIO_FRAMES];                /* the filtered guitar alone (USB recording, tuner) */
    float usb_play[USB_PLAY_CHANNELS * AUDIO_FRAMES];
    float usb_rec[USB_REC_CHANNELS * AUDIO_FRAMES];
    float *extra[2];
    volatile uint8_t mute;                  /* output silenced (tuner), the chain is not run */
    volatile uint8_t tuner_feed;
    volatile uint32_t tuner_head;           /* written by the interrupt */
    uint32_t tuner_tail;                    /* read by the main loop */
    float tuner_odd;                        /* first half of a decimation pair */
    float tuner_ring[TUNER_RING];
    uint32_t sec_t;                         /* the section timer: cycle count at the last mark */
    uint32_t sec_blocks;
    volatile uint8_t sec_reset;             /* main loop -> interrupt */
    uint64_t sec_sum[ENGINE_SECTIONS];
} eng;

/* the cycles since the last mark go to section `s` */
static inline void mark(int s)
{
    uint32_t now = time_cycles();
    eng.sec_sum[s] += now - eng.sec_t;
    eng.sec_t = now;
}

uint32_t engine_sections(uint32_t mean[ENGINE_SECTIONS])
{
    uint32_t n = eng.sec_blocks;
    for (int s = 0; s < ENGINE_SECTIONS; s++)
        mean[s] = n ? (uint32_t)(eng.sec_sum[s] / n) : 0;      /* a block may land in between: harmless */
    return n;
}

void engine_sections_reset(void)
{
    eng.sec_reset = 1;
}

void engine_init(struct chain *chain, uint32_t core_hz)
{
    memset(&eng, 0, sizeof eng);
    eng.chain = chain;
    eng.cycles_to_ms = core_hz ? 1000.0f / (float)core_hz : 0.0f;
    eng.master = 0.5f;
    eng.preset_l = eng.preset_r = 1.0f;
    eng.in_target = eng.in_gain = 1.0f;
    eng.aux_target = eng.aux_gain = 0.5f;
    eng.rec_target = eng.rec_gain = eng.mon_target = eng.mon_gain = 1.0f;
    for (int k = 0; k < EQ_PARAMS; k++)
        eng.eq_params[k] = eq_param_ranges[k].def;
    global_eq_init(&eng.eq);
    eng.extra[0] = eng.fx_send;
    eng.extra[1] = eng.fx_return;
    eng.spill_extra[0] = eng.spill_send;    /* a tail's FX send goes nowhere, its return is silent */
    eng.spill_extra[1] = eng.spill_ret;
}

void engine_set_input_level(int db)
{
    eng.in_target = level_db[(db < -20 ? -20 : db > 20 ? 20 : db) + 20];
}

void engine_set_aux_level(int percent)
{
    eng.aux_target = (float)(percent < 0 ? 0 : percent > 100 ? 100 : percent) * 0.01f;
}

void engine_set_usb(int rec_db, int mon_db, int rec_aux, int wet_l, int wet_r)
{
    eng.rec_target = level_db[(rec_db < -20 ? -20 : rec_db > 20 ? 20 : rec_db) + 20];
    eng.mon_target = level_db[(mon_db < -20 ? -20 : mon_db > 20 ? 20 : mon_db) + 20];
    eng.usb_rec_aux = (uint8_t)(rec_aux != 0);
    eng.usb_rec_wet[0] = (uint8_t)(wet_l != 0);
    eng.usb_rec_wet[1] = (uint8_t)(wet_r != 0);
}

void engine_set_no_cab(int left, int right)
{
    eng.no_cab = (uint8_t)((left ? CAB_OFF_L : 0) | (right ? CAB_OFF_R : 0));
}

void engine_set_global_eq(int on, const float params[EQ_PARAMS])
{
    /* single floats: the interrupt may see a mix of old and new values for one block, which is harmless */
    for (int k = 0; k < EQ_PARAMS; k++)
        eng.eq_params[k] = params[k];
    eng.eq_on = (uint8_t)(on != 0);
}

void engine_set_master(float linear)
{
    eng.master = linear < 0 ? 0 : linear > 1 ? 1 : linear;
}

void engine_set_preset_level(uint8_t volume, int8_t pan)
{
    /* FUN_00026C7C: volume * 0.02 (50 = unity), pan attenuates the opposite side */
    float l = volume * 0.02f, r = l;
    if (pan > 0)
        l *= (100 - pan) * 0.01f;
    else if (pan < 0)
        r *= (pan + 100) * 0.01f;
    eng.preset_l = l;
    eng.preset_r = r;
}
static int tails(void)
{
    int n = 0;
    for (int i = 0; i < SPILL_TAILS; i++)
        n += eng.spill[i].chain != 0;
    return n;
}

void engine_suspend(void)
{
    eng.want_run = 0;
    if (eng.spill_on)
        eng.spill_stop = 1;
    if (audio_streaming()) {                /* let the interrupt finish its fade-out block */
        uint32_t t0 = time_ms();
        while ((eng.running || eng.spill_on) && time_ms() - t0 < SUSPEND_WAIT_MS) {}
    }
    eng.running = 0;
    if (eng.spill_on) {                     /* the interrupt is not running: nothing rings any more */
        for (int i = 0; i < SPILL_TAILS; i++)
            eng.spill[i].chain = 0;
        eng.spill_on = 0;
    }
    eng.spill_stop = 0;
}

void engine_set_spill(uint32_t ms)
{
    eng.spill_max = ms ? BLOCKS_OF_MS(ms) : 0;
}

int engine_spill(struct chain *tail)
{
    if (!eng.spill_max || !audio_streaming() || !eng.want_run || !eng.running || eng.mute || !eng.chain ||
        latency_preset())
        return 0;
    eng.spill_req = tail;
    uint32_t t0 = time_ms();
    while (eng.spill_req && time_ms() - t0 < SUSPEND_WAIT_MS) {}
    if (eng.spill_req) {                    /* the interrupt did not come: as engine_suspend */
        eng.spill_req = 0;
        return 0;
    }
    /* the tail cut by this change must be gone before its side of the pools is loaded again */
    t0 = time_ms();
    while (tails() > 1 && time_ms() - t0 < SUSPEND_WAIT_MS) {}
    return tails() <= 1;                    /* else the caller suspends, which cuts them all */
}

int engine_spilling(void)
{
    return eng.spill_on;
}

void engine_resume(void)
{
    eng.fade = 0.0f;
    eng.want_run = 1;
}

int engine_flash_begin(void)
{
    int reads = eng.chain && eng.chain->flash_data;
    for (int i = 0; i < SPILL_TAILS; i++)
        reads |= eng.spill[i].chain && eng.spill[i].chain->flash_data;
    if (!reads)
        return 0;
    engine_suspend();
    return 1;
}

void engine_flash_end(int suspended)
{
    if (suspended)
        engine_resume();
}

void engine_set_mute(int on)
{
    eng.mute = (uint8_t)(on != 0);
}

void engine_tuner_feed(int on)
{
    eng.tuner_tail = eng.tuner_head;
    eng.tuner_feed = (uint8_t)(on != 0);
}

int engine_tuner_read(float *dst, int max)
{
    uint32_t head = eng.tuner_head;
    if (head - eng.tuner_tail > TUNER_RING)                 /* the reader fell behind: keep the newest */
        eng.tuner_tail = head - TUNER_RING;
    int n = 0;
    while (eng.tuner_tail != head && n < max)
        dst[n++] = eng.tuner_ring[eng.tuner_tail++ & (TUNER_RING - 1)];
    return n;
}

void engine_stats(struct engine_stats *out)
{
    *out = eng.stats;
    eng.stats.in_peak = eng.stats.out_peak = 0;
    eng.stats.worst_ms = 0;
}

void engine_stats_peek(struct engine_stats *out)
{
    *out = eng.stats;
}

int engine_overload_take(void)
{
    if (!eng.overloaded)
        return 0;
    eng.overloaded = 0;
    return 1;
}

/* n words (a multiple of 8) from the DMA buffer (non-cacheable OCRAM) to the stack (DTCM), 32 bytes per LDM: one
   burst on the bus instead of a transaction per word (the block's words: 8.0k -> 3.2k cycles on the pedal; the
   other way, the stores to the transmit buffer staged and copied the same way were slower than straight: they
   are buffered) */
static void copy_words(int32_t *d, const int32_t *s, int n)
{
#if defined(__arm__) && !defined(WAVEOD_HOST)
    for (; n >= 8; n -= 8)
        __asm__ volatile("ldmia %1!, {r4-r6, r8-r12}\n\tstmia %0!, {r4-r6, r8-r12}"    /* not r7 (frame pointer) */
                         : "+r"(d), "+r"(s) : : "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "memory");
#else
    memcpy(d, s, sizeof(int32_t) * (unsigned)n);
#endif
}

/* v >= 2147483520 ? INT32_MAX : v <= -2147483520 ? INT32_MIN : (int32_t)v, without float compares (VCMP / VMRS
   stall the FPU around them): held in range by VMAXNM / VMINNM, converted, then the two ends as integers. The
   same word for every v but NaN. */
static inline int32_t to_dac(float v)
{
    int32_t w = (int32_t)__builtin_fminf(__builtin_fmaxf(v, -2147483648.0f), 2147483520.0f);
    w = w >= 2147483520 ? 2147483647 : w;
    return w <= -2147483520 ? -2147483647 - 1 : w;
}

static inline float fabs_(float v) { return v < 0 ? -v : v; }

/* one block's step of a gain towards its target (the original's input and AUX ramps) */
static float approach(float cur, float target, float step)
{
    if (cur < target)
        return cur + step > target ? target : cur + step;
    if (cur > target)
        return cur - step < target ? target : cur - step;
    return cur;
}

/* converters -> io (guitar on both sides), FX return, AUX IN; FUN_00031E18 up to the chain */
static float read_inputs(const int32_t *in, int frames)
{
    eng.in_gain = approach(eng.in_gain, eng.in_target, GAIN_STEP);
    eng.aux_gain = approach(eng.aux_gain, eng.aux_target, GAIN_STEP);
    float gin = eng.in_gain * IN_SCALE, gaux = eng.aux_gain * IN_SCALE;
    float pair[2 * AUDIO_FRAMES], *aux_in = eng.aux_in;
    int32_t words[AUDIO_FRAMES * AUDIO_IN_WORDS];
    if (frames == AUDIO_FRAMES) {                   /* the block's words in bursts to the stack, read from there */
        copy_words(words, in, AUDIO_FRAMES * AUDIO_IN_WORDS);
        in = words;
    }
    for (int f = 0; f < frames; f++) {
        const int32_t *p = in + f * AUDIO_IN_WORDS;
        int32_t w0 = p[0], w1 = p[1], w2 = p[2], w3 = p[3], w4 = p[4], w5 = p[5];   /* one pass over the DMA words */
        pair[2 * f] = (float)(w3 / 2 - w0 / 2) * gin;               /* guitar, differential */
        pair[2 * f + 1] = (float)(w4 / 2 - w1 / 2) * IN_SCALE;      /* FX return */
        aux_in[2 * f] = (float)-w2 * gaux;
        aux_in[2 * f + 1] = (float)-w5 * gaux;
    }
    mark(ESEC_CONVERT);
    biquad_stereo(eng.in_state, &io_filter_in, pair, frames);
    mark(ESEC_IN_FILTER);
    usb_audio_play(eng.usb_play, frames);
    mark(ESEC_USB_PLAY);

    float peak = 0, *io = eng.io, *ret = eng.fx_return, *send = eng.fx_send, *dry = eng.dry;
    const float *play = eng.usb_play;
    for (int f = 0; f < frames; f++) {
        float g = pair[2 * f], r = pair[2 * f + 1];
        dry[f] = g;
        /* USB playback channel 3 joins the guitar on its way into the chain (re-amping) */
        io[2 * f] = io[2 * f + 1] = g + play[USB_PLAY_CHANNELS * f + 2] * IN_SCALE;
        ret[2 * f] = ret[2 * f + 1] = r;
        send[2 * f] = send[2 * f + 1] = 0.0f;
        peak = __builtin_fmaxf(peak, __builtin_fabsf(g));           /* VMAXNM: no flags */
    }
    if (eng.tuner_feed) {                                           /* 2:1 decimation (pair average) */
        uint32_t head = eng.tuner_head;
        float odd = eng.tuner_odd;
        for (int f = 0; f < frames; f++) {
            float g = pair[2 * f];
            if (f & 1)
                eng.tuner_ring[head++ & (TUNER_RING - 1)] = 0.5f * (odd + g);
            else
                odd = g;
        }
        eng.tuner_odd = odd;
        eng.tuner_head = head;
    }
    return peak;
}

/* ---------------------------------------------------------------- spillover */

/* Block start: a tail handed over takes the chain's place (the chain stops at once: the tail runs its modules
   from this block on); a stop or the mute cuts every tail within the block. */
static void spill_take(void)
{
    struct chain *req = eng.spill_req;
    for (int i = 0; i < SPILL_TAILS; i++) {
        struct spill *s = &eng.spill[i];
        if (!s->chain)
            continue;
        if (eng.spill_stop || eng.mute || latency_preset() || latency_quiet())
            s->step = 1.0f;                         /* a latency test hears nothing but its chirp */
        else if (req && s->step < 1.0f / SPILL_CUT)
            s->step = 1.0f / SPILL_CUT;
    }
    if (req) {
        struct spill *f = 0;
        for (int i = 0; i < SPILL_TAILS && !f; i++)
            if (!eng.spill[i].chain)
                f = &eng.spill[i];
        if (!f)                                     /* the main loop waits for a free one; never here */
            f = &eng.spill[0];
        f->skip = req->skip;
        req->skip = 0;                              /* the whole chain until the drain is over */
        f->blocks = f->quiet = f->still = 0;
        f->level_l = eng.level_l;
        f->level_r = eng.level_r;
        f->gain = 1.0f;
        f->step = 0.0f;
        f->chain = req;
        eng.want_run = 0;
        eng.running = 0;
        eng.stats.spills++;
        eng.spill_req = 0;
    }
    eng.spill_on = (uint8_t)(tails() != 0);
}

/* One block of every tail into spill_mix (at its preset's level); 1 when there is something to add. The
   guitar leaves the old chain over SPILL_IN_FADE blocks; after the drain (above), and once the new chain runs,
   only the slots from the delays and reverbs on run. A tail ends after the time allowed (faded over
   SPILL_END_MS), after SPILL_QUIET_MS of silence, or faded out when cut. */
static int spill_run(int frames)
{
    int any = 0;
    uint32_t end = BLOCKS_OF_MS(SPILL_END_MS), quiet = BLOCKS_OF_MS(SPILL_QUIET_MS);
    if (end > eng.spill_max)
        end = eng.spill_max ? eng.spill_max : 1;
    for (int i = 0; i < SPILL_TAILS; i++) {
        struct spill *s = &eng.spill[i];
        struct chain *c = s->chain;
        if (!c)
            continue;
        float *io = eng.spill_io, *aux = eng.spill_tmp;
        if (s->blocks < SPILL_IN_FADE) {
            float g = 1.0f - (float)s->blocks / SPILL_IN_FADE, dg = -1.0f / (SPILL_IN_FADE * (float)frames);
            for (int f = 0; f < frames; f++, g += dg) {
                io[2 * f] = eng.spill_in[2 * f] * g;
                io[2 * f + 1] = eng.spill_in[2 * f + 1] * g;
                aux[2 * f] = eng.spill_aux[2 * f] * g;
                aux[2 * f + 1] = eng.spill_aux[2 * f + 1] * g;
            }
        } else {
            memset(io, 0, sizeof(float) * 2 * (unsigned)frames);
            memset(aux, 0, sizeof(float) * 2 * (unsigned)frames);
        }
        memset(eng.spill_send, 0, sizeof eng.spill_send);
        memset(eng.spill_ret, 0, sizeof eng.spill_ret);
        int draining = c->skip != s->skip;
        if (draining) {                             /* how loud the slots to leave out still are */
            c->watch = s->skip;
            c->watch_peak = 0.0f;
        }
        chain_process(c, io, aux, eng.spill_extra, frames, eng.no_cab);
        if (draining) {
            s->still = c->watch_peak < SPILL_FLOOR ? s->still + 1 : 0;
            if (eng.running && s->blocks >= SPILL_IN_FADE &&
                (s->still >= SPILL_STILL || s->blocks >= BLOCKS_OF_MS(SPILL_DRAIN_MS))) {
                c->skip = s->skip;                  /* from the next block */
                c->watch = 0;
            }
        }

        float g = s->gain, next = g - s->step;
        if (next < 0.0f)
            next = 0.0f;
        float dg = (next - g) / (float)frames, peak = 0.0f;
        for (int f = 0; f < frames; f++) {
            g += dg;
            float l = io[2 * f] * g * s->level_l, r = io[2 * f + 1] * g * s->level_r;
            eng.spill_mix[2 * f] = any ? eng.spill_mix[2 * f] + l : l;
            eng.spill_mix[2 * f + 1] = any ? eng.spill_mix[2 * f + 1] + r : r;
            float a = fabs_(l) > fabs_(r) ? fabs_(l) : fabs_(r);
            if (a > peak)
                peak = a;
        }
        any = 1;
        s->gain = next;
        s->blocks++;
        s->quiet = !draining && peak < SPILL_FLOOR ? s->quiet + 1 : 0;
        if (!s->step && s->quiet >= quiet)
            s->gain = 0.0f, s->step = 1.0f;         /* silent already: nothing to fade */
        else if (!s->step && s->blocks + end >= eng.spill_max)
            s->step = 1.0f / (float)end;
        if (s->step && s->gain <= 0.0f)
            s->chain = 0;
    }
    eng.spill_on = (uint8_t)(tails() != 0);
    return any;
}

/* An overload, or a slow block (`slow`): the tails go first, cut within the next block. On a slow block a tail
   still draining only stops the slots before its delays and reverbs (what they still give out is lost); it is
   cut if the blocks stay slow. */
static void spill_cut_all(int slow)
{
    for (int i = 0; i < SPILL_TAILS; i++) {
        struct spill *s = &eng.spill[i];
        struct chain *c = s->chain;
        if (!c || s->step >= 1.0f)
            continue;
        if (slow && c->skip != s->skip && eng.running) {
            c->skip = s->skip;
            c->watch = 0;
            continue;
        }
        s->step = 1.0f;
        eng.stats.spill_cuts++;
    }
}

/* The FX send through 1 / H(z). Without an FX Loop / Send block in the chain the send is silent: with the filter
   at rest too (its state all zeros) the output would be zeros (+-0, the same words on the converter), so it is
   skipped and the state set to +0, which the filter gives the same results from as from -0. */
static void send_filter(int frames)
{
    typedef uint32_t __attribute__((may_alias)) word;  /* the floats' bits */
    const word *x = (const word *)eng.fx_send, *st = (const word *)eng.send_state;
    uint32_t any = 0;
    for (int i = 0; i < 8; i++)                     /* the state first, then the block until a sample is not 0 */
        any |= st[i] << 1;                          /* every bit but the sign */
    for (int i = 0; i < 2 * frames && !any; i += 4)
        any = (x[i] | x[i + 1] | x[i + 2] | x[i + 3]) << 1;
    if (any) {
        biquad_stereo(eng.send_state, &io_filter_out, eng.fx_send, frames);
        return;
    }
    memset(eng.send_state, 0, sizeof eng.send_state);
    memset(eng.fx_send, 0, sizeof(float) * 2 * (unsigned)frames);
}

/* chain output -> preset level, global EQ, AUX IN, output filters (FUN_00031E18 after the chain) */
static void post_chain(int frames, int spill)
{
    float *io = eng.io;
    const float *aux_in = eng.aux_in, *dry = eng.dry;
    float pl = eng.preset_l, pr = eng.preset_r, ll = eng.level_l, lr = eng.level_r;
    float sl = (pl - ll) / (float)frames, sr = (pr - lr) / (float)frames;
    for (int f = 0; f < frames; f++) {
        ll += sl;
        lr += sr;
        io[2 * f] *= ll;
        io[2 * f + 1] *= lr;
    }
    eng.level_l = pl;
    eng.level_r = pr;
    if (spill)                                      /* the tails, at their own preset's level */
        for (int i = 0; i < 2 * frames; i++)
            io[i] += eng.spill_mix[i];

    /* USB recording 1 / 2: dry guitar or processed (x record level), 3 / 4: processed; AUX IN on request */
    eng.rec_gain = approach(eng.rec_gain, eng.rec_target, GAIN_STEP);
    float gr = eng.rec_gain * FULL_SCALE, aux = eng.usb_rec_aux ? 1.0f : 0.0f;
    int wet_l = eng.usb_rec_wet[0], wet_r = eng.usb_rec_wet[1];
    float *rec = eng.usb_rec;
    for (int f = 0; f < frames; f++) {
        float *r = rec + USB_REC_CHANNELS * f;
        float al = aux_in[2 * f] * aux, ar = aux_in[2 * f + 1] * aux;
        float xl = io[2 * f], xr = io[2 * f + 1], d = dry[f];
        float l = wet_l ? xl : d, rr = wet_r ? xr : d;
        r[0] = (al + l) * gr;
        r[1] = (ar + rr) * gr;
        r[2] = (al + xl) * FULL_SCALE;
        r[3] = (ar + xr) * FULL_SCALE;
    }
    mark(ESEC_LEVEL);

    looper_process(eng.io, frames, LOOP_POST);
    int eq_tap = scope_target() == SCOPE_EQ;        /* the analyser on the global EQ screen: its input and output */
    if (eq_tap)
        memcpy(eng.eq_in, eng.io, sizeof(float) * 2 * (unsigned)frames);
    global_eq_process(&eng.eq, eng.io, frames, eng.eq_params, eng.eq_on);
    if (eq_tap)
        scope_push(eng.eq_in, 2, eng.io, 2, frames);
    mark(ESEC_EQ);
    drums_process(eng.io, frames);
    mark(ESEC_DRUMS);

    /* AUX IN and USB playback 1 / 2 (x monitor level) never go through the effects; recording 5 / 6 = the mix */
    eng.mon_gain = approach(eng.mon_gain, eng.mon_target, GAIN_STEP);
    float gm = eng.mon_gain * IN_SCALE;
    const float *play = eng.usb_play;
    for (int f = 0; f < frames; f++) {
        const float *p = play + USB_PLAY_CHANNELS * f;
        float xl = io[2 * f] + (aux_in[2 * f] + p[0] * gm), xr = io[2 * f + 1] + (aux_in[2 * f + 1] + p[1] * gm);
        io[2 * f] = xl;
        io[2 * f + 1] = xr;
        rec[USB_REC_CHANNELS * f + 4] = xl * FULL_SCALE;
        rec[USB_REC_CHANNELS * f + 5] = xr * FULL_SCALE;
    }
    usb_audio_record(rec, frames);
    mark(ESEC_MIX);
    biquad_stereo(eng.out_state, &io_filter_out, io, frames);
    mark(ESEC_OUT_FILTER);
    send_filter(frames);
    mark(ESEC_SEND_FILTER);
}

void engine_audio(const int32_t *in, int32_t *out, int frames)
{
    uint32_t t0 = time_cycles();
    ABI_BLOCK_TOGGLE ^= 1;
    if (eng.sec_reset) {
        memset(eng.sec_sum, 0, sizeof eng.sec_sum);
        eng.sec_blocks = 0;
        eng.sec_reset = 0;
    }
    eng.sec_t = t0;

    float in_peak = read_inputs(in, frames);
    mark(ESEC_SPREAD);
    looper_block(eng.io, frames);
    looper_process(eng.io, frames, LOOP_PRE);  /* a PRE loop goes into the chain, and the side chain sees it */
    int lat_chain = latency_preset();           /* latency test through the chain: its chirp instead */
    if (lat_chain) {
        latency_chain_in(eng.io, frames);
        memset(eng.fx_return, 0, sizeof eng.fx_return);
    }
    for (int i = 0; i < 2 * frames; i++)
        eng.aux[i] = eng.io[i] + eng.fx_return[i];

    spill_take();
    for (int i = 0; i < SPILL_TAILS; i++)
        if (eng.spill[i].chain && eng.spill[i].blocks < SPILL_IN_FADE) {    /* the old preset's input */
            memcpy(eng.spill_in, eng.io, sizeof(float) * 2 * (unsigned)frames);
            memcpy(eng.spill_aux, eng.aux, sizeof(float) * 2 * (unsigned)frames);
            break;
        }

    /* after a suspend request one more block is processed while the chain's output ramps to silence; after a
       resume it fades in over four blocks. The output follows, unless a tail is ringing. */
    int run = eng.running || eng.want_run;
    int processed = run && eng.chain && !eng.mute;
    /* the modulators move their knobs only while the chain plays (not while it is being built) */
    mods_run(processed ? eng.chain : 0, eng.io, frames, eng.chain ? eng.chain->beat_ms : 500);
    mark(ESEC_PRE);
    if (processed)
        chain_process(eng.chain, eng.io, eng.aux, eng.extra, frames, eng.no_cab);
    else
        memset(eng.io, 0, sizeof(float) * 2 * (unsigned)frames);
    mark(ESEC_CHAIN);
    if (lat_chain)
        latency_chain_out(eng.io, frames);
    if (processed && scope_target() == SCOPE_PRESET)    /* the spectrum analyser: the guitar, the chain's output */
        scope_push(eng.dry, 1, eng.io, 2, frames);
    float fade_to = 0.0f;
    if (eng.want_run && !eng.mute)
        fade_to = eng.fade + 0.25f > 1.0f ? 1.0f : eng.fade + 0.25f;
    if (eng.fade != 1.0f || fade_to != 1.0f) {
        float g = eng.fade, dg = (fade_to - eng.fade) / (float)frames;
        for (int f = 0; f < frames; f++) {
            g += dg;
            eng.io[2 * f] *= g;
            eng.io[2 * f + 1] *= g;
        }
    }
    eng.running = eng.want_run;
    int spill = spill_run(frames);
    mark(ESEC_MID);
    post_chain(frames, spill);

    float out_to = 0.0f;
    if ((eng.want_run || eng.spill_on || spill) && !eng.mute)
        out_to = eng.out_fade + 0.25f > 1.0f ? 1.0f : eng.out_fade + 0.25f;
    float target = eng.master * eng.master * out_to * FULL_SCALE;   /* the original squares the MASTER knob */
    if (latency_quiet())
        target = 0.0f;
    float step = (target - eng.out_gain) / (float)frames, g = eng.out_gain, out_peak = 0;
    const float *io = eng.io, *send = eng.fx_send;
    for (int f = 0; f < frames; f++) {               /* straight to the DMA buffer: the stores are buffered */
        g += step;
        float l = io[2 * f] * g, r = io[2 * f + 1] * g;
        int32_t *o = out + f * AUDIO_OUT_WORDS;
        o[0] = to_dac(l);
        o[1] = to_dac(send[2 * f] * FULL_SCALE);
        o[2] = to_dac(r);
        o[3] = 0;
        /* after level, MASTER, mute (VMAXNM: no flags) */
        out_peak = __builtin_fmaxf(out_peak, __builtin_fmaxf(__builtin_fabsf(l), __builtin_fabsf(r)));
    }
    eng.out_gain = target;
    eng.fade = fade_to;
    eng.out_fade = out_to;
    mark(ESEC_DAC);
    latency_io(in, out, frames);

    struct engine_stats *st = &eng.stats;
    in_peak *= FULL_SCALE / CONV_SCALE;         /* the meters: dB of the converters' full scale */
    if (in_peak > st->in_peak)
        st->in_peak = in_peak;
    out_peak *= 1.0f / CONV_SCALE;
    if (out_peak > st->out_peak)
        st->out_peak = out_peak;
    st->blocks++;
    float ms = (float)(time_cycles() - t0) * eng.cycles_to_ms;
    st->block_ms = ms;
    if (ms > st->worst_ms)
        st->worst_ms = ms;
    if (ms > st->max_ms)
        st->max_ms = ms;
    if (ms > OVERLOAD_MS)
        st->overruns++;
    if (eng.spill_on && ms > SPILL_CPU_MS)
        spill_cut_all(1);
    if (overload_block(&eng.guard, ms) && eng.chain) {
        chain_bypass_all(eng.chain);        /* here, not in the main loop: it may not get to run */
        spill_cut_all(0);
        st->overloads++;
        eng.overloaded = 1;
    }
    ABI_BLOCK_MS = ms;
    mark(ESEC_STATS);
    eng.sec_blocks++;
}