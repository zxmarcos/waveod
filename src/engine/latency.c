#include "engine/latency.h"
#include "dsp/fmath.h"
#include "platform/audio.h"
#include "platform/sdram.h"
#include "ui/i18n.h"                       /* the route and status names */
#include <string.h>

#define RAW             2147483648.0f           /* a converter word's full scale */
#define CLIP            0.9f                    /* a converter word this close to full scale: clipping */
#define FIRST           0.25f                   /* an earlier arrival this strong (of the peak) counts */
#define MATCH_CABLE     0.5f                    /* a delayed copy through a cable is close to 1 */
#define MATCH_PRESET    0.2f                    /* amps and cabinets change the chirp a lot */
#define GAP_CABLE       8                       /* blocks of silence before each run */
#define GAP_PRESET      48                      /* 70 ms: the tail of the last chirp dies down a little */
#define PRESET_LEVEL    0.25f                   /* the engine's scale: a strong guitar */
#define CLUSTER         16                      /* an earlier arrival is one at least this far before the peak */
#define F0              300.0f
#define F1              12000.0f

enum phase { P_IDLE, P_SETTLE, P_EMIT, P_READY };

static const int8_t level_dbs[LAT_LEVELS] = { -30, -20, -10 };
static const float level_amp[LAT_LEVELS] = { 0.031622777f, 0.1f, 0.31622777f };

static struct {
    volatile uint8_t phase;                     /* main loop <-> interrupt (enum phase) */
    uint8_t route;
    uint16_t gap;                               /* interrupt: blocks left before the next run */
    uint32_t hold;                              /* interrupt: blocks since the start */
    uint32_t pos;                               /* interrupt: samples since the chirp started */
    uint32_t peak;                              /* interrupt: the recorded input's largest word (magnitude) */
    float amp;
    int level, lowered;
    float lags[LAT_RUNS];
    struct latency_result res;
} lat;

static float stim[LAT_STIMULUS] SDRAM_BSS;
static float cap[2][LAT_CAPTURE] SDRAM_BSS;
static float corr[LAT_CAPTURE] SDRAM_BSS;
static int stim_ready;

static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }
static inline float fabs_(float v) { return v < 0 ? -v : v; }

/* linear chirp F0 -> F1 under a Hann window */
static void make_stimulus(void)
{
    const float two_pi = 6.2831853f;
    float ph = 0;
    for (int i = 0; i < LAT_STIMULUS; i++) {
        float t = (float)i / (float)(LAT_STIMULUS - 1);
        float w = 0.5f - 0.5f * cosf(two_pi * t);
        stim[i] = w * sinf(ph);
        ph += two_pi * (F0 + (F1 - F0) * t) / (float)AUDIO_RATE;
        if (ph > two_pi)
            ph -= two_pi;
    }
    stim_ready = 1;
}

static int is_cable(int route) { return route != LAT_PRESET; }

/* ---------------------------------------------------------------- the search */

float latency_find(const float *ref, int nref, const float *x, int nx, float *match)
{
    int lags = nx - nref + 1;
    *match = 0;
    if (lags <= 0 || lags > LAT_CAPTURE)
        return -1;
    float er = 0, best = 0;
    int best_at = 0;
    for (int k = 0; k < nref; k++)
        er += ref[k] * ref[k];
    for (int l = 0; l < lags; l++) {
        const float *p = x + l;
        float c = 0;
        for (int k = 0; k < nref; k++)
            c += ref[k] * p[k];
        corr[l] = c;
        if (fabs_(c) > best) {
            best = fabs_(c);
            best_at = l;
        }
    }
    if (best <= 0 || er <= 0)
        return -1;
    /* the first strong arrival: a reverb or a delay may repeat it louder later */
    int at = best_at;
    for (int l = 0; l + CLUSTER <= best_at; l++) {
        if (fabs_(corr[l]) >= FIRST * best) {
            at = l;
            for (int m = l; m < l + CLUSTER; m++)
                if (fabs_(corr[m]) > fabs_(corr[at]))
                    at = m;
            break;
        }
    }
    float ex = 0;
    for (int k = 0; k < nref; k++)
        ex += x[at + k] * x[at + k];
    if (ex > 0)
        *match = fabs_(corr[at]) / (float)sqrt((double)er * (double)ex);
    /* a parabola through the peak and its neighbours (signed: the input may invert the polarity) */
    float d = 0;
    if (at > 0 && at < lags - 1) {
        float sg = corr[at] < 0 ? -1.0f : 1.0f;
        float y0 = sg * corr[at - 1], y1 = sg * corr[at], y2 = sg * corr[at + 1];
        float den = y0 - 2.0f * y1 + y2;
        if (den < 0) {
            d = 0.5f * (y0 - y2) / den;
            d = d < -0.5f ? -0.5f : d > 0.5f ? 0.5f : d;
        }
    }
    return (float)at + d;
}

/* ---------------------------------------------------------------- main loop */

static void changed(void) { lat.res.seq++; }

static void next_run(void)
{
    lat.gap = is_cable(lat.route) ? GAP_CABLE : GAP_PRESET;
    barrier();
    lat.phase = P_SETTLE;
}

void latency_start(int route)
{
    if (route < 0 || route >= LAT_ROUTES)
        return;
    lat.phase = P_IDLE;
    barrier();
    if (!stim_ready)
        make_stimulus();
    uint32_t seq = lat.res.seq;
    memset(&lat.res, 0, sizeof lat.res);
    lat.res.seq = seq + 1;
    lat.res.status = LAT_BUSY;
    lat.res.route = (uint8_t)route;
    lat.route = (uint8_t)route;
    lat.level = 0;
    lat.lowered = 0;
    lat.amp = is_cable(route) ? level_amp[0] : PRESET_LEVEL;
    lat.res.level_db = is_cable(route) ? level_dbs[0] : 0;
    lat.hold = 0;
    next_run();
}

void latency_cancel(void)
{
    if (lat.phase == P_IDLE)
        return;
    lat.phase = P_IDLE;
    lat.res.status = LAT_CANCELLED;
    changed();
}

static void finish(int status)
{
    lat.phase = P_IDLE;
    lat.res.status = (uint8_t)status;
    changed();
}

static void set_level(int level)
{
    lat.level = level;
    lat.amp = level_amp[level];
    lat.res.level_db = level_dbs[level];
}

static void record(float lag, float match)
{
    struct latency_result *r = &lat.res;
    lat.lags[r->runs] = lag;
    if (!r->runs || match < r->match)
        r->match = match;
    r->runs++;
    float lo = lat.lags[0], hi = lo, sum = 0;
    for (int i = 0; i < r->runs; i++) {
        lo = lat.lags[i] < lo ? lat.lags[i] : lo;
        hi = lat.lags[i] > hi ? lat.lags[i] : hi;
        sum += lat.lags[i];
    }
    r->min = lo;
    r->max = hi;
    r->mean = sum / (float)r->runs;
}

int latency_poll(void)
{
    if (lat.phase != P_READY)
        return 0;
    float match = 0, lag = latency_find(stim, LAT_STIMULUS, cap[0], LAT_CAPTURE, &match);
    if (!is_cable(lat.route)) {
        float m2, lag2 = latency_find(stim, LAT_STIMULUS, cap[1], LAT_CAPTURE, &m2);
        if (m2 > match) {
            lag = lag2;
            match = m2;
        }
        if (match < MATCH_PRESET) {
            finish(LAT_NO_SIGNAL);
            return 1;
        }
    } else {
        if ((float)lat.peak > CLIP * RAW) {     /* either side of the input: a TS cable grounds the other */
            if (lat.level == 0) {
                finish(LAT_CLIPPED);
            } else {                            /* again, quieter, and never louder from now on */
                set_level(lat.level - 1);
                lat.lowered = 1;
                changed();
                next_run();
            }
            return 1;
        }
        if (match < MATCH_CABLE) {
            if (lat.lowered || lat.level == LAT_LEVELS - 1) {
                finish(LAT_NO_SIGNAL);
            } else {
                set_level(lat.level + 1);
                changed();
                next_run();
            }
            return 1;
        }
    }
    record(lag, match);
    if (lat.res.runs == LAT_RUNS)
        finish(lat.res.max - lat.res.min > LAT_SPREAD ? LAT_UNSTABLE : LAT_OK);
    else {
        changed();
        next_run();
    }
    return 1;
}

void latency_get(struct latency_result *out)
{
    *out = lat.res;
}

const char *latency_route_name(int route)
{
    switch (route) {
    case LAT_OUT_L:  return T(MSG_LATENCY_ROUTE_OUT_L);
    case LAT_OUT_R:  return T(MSG_LATENCY_ROUTE_OUT_R);
    case LAT_SEND:   return "SEND \xC2\xBB RETURN";
    case LAT_PRESET: return T(MSG_LATENCY_ROUTE_PRESET);
    default:         return "-";
    }
}

const char *latency_status_name(int status)
{
    switch (status) {
    case LAT_BUSY:      return T(MSG_LATENCY_STATUS_BUSY);
    case LAT_OK:        return "OK";
    case LAT_UNSTABLE:  return T(MSG_LATENCY_STATUS_UNSTABLE);
    case LAT_NO_SIGNAL: return T(MSG_LATENCY_STATUS_NO_SIGNAL);
    case LAT_CLIPPED:   return T(MSG_LATENCY_STATUS_CLIPPED);
    case LAT_CANCELLED: return T(MSG_LATENCY_STATUS_CANCELLED);
    default:            return "-";
    }
}

/* ---------------------------------------------------------------- audio interrupt */

int latency_quiet(void) { return lat.phase != P_IDLE; }
int latency_preset(void) { return lat.phase != P_IDLE && lat.route == LAT_PRESET; }

void latency_chain_in(float *io, int frames)
{
    for (int f = 0; f < frames; f++) {
        uint32_t i = lat.pos + (uint32_t)f;
        float v = lat.phase == P_EMIT && i < LAT_STIMULUS ? stim[i] * lat.amp : 0.0f;
        io[2 * f] = io[2 * f + 1] = v;
    }
}

void latency_chain_out(const float *io, int frames)
{
    if (lat.phase != P_EMIT)
        return;
    for (int f = 0; f < frames; f++) {
        uint32_t i = lat.pos + (uint32_t)f;
        if (i < LAT_CAPTURE) {
            cap[0][i] = io[2 * f];
            cap[1][i] = io[2 * f + 1];
        }
    }
}

static inline int32_t to_word(float v)
{
    float s = v * RAW;
    if (s >= 2147483520.0f)
        s = 2147483520.0f;
    if (s <= -2147483520.0f)
        s = -2147483520.0f;
    return (int32_t)s & ~0xff;                  /* 24-bit data, left justified */
}

void latency_io(const int32_t *in, int32_t *out, int frames)
{
    int phase = lat.phase;
    if (phase == P_IDLE)
        return;
    /* the first block fades the engine's output out (latency_quiet), from then on the test owns the outputs */
    if (lat.hold++ > 0)
        for (int i = 0; i < frames * AUDIO_OUT_WORDS; i++)
            out[i] = 0;
    if (phase == P_SETTLE) {
        if (lat.gap)
            lat.gap--;
        else {
            lat.pos = 0;
            lat.peak = 0;
            lat.phase = P_EMIT;                 /* the next block starts the chirp at its first frame */
        }
        return;
    }
    if (phase != P_EMIT)
        return;
    if (is_cable(lat.route)) {
        int word = lat.route == LAT_OUT_L ? 0 : lat.route == LAT_OUT_R ? 2 : 1;
        int a = lat.route == LAT_SEND ? 4 : 3, b = lat.route == LAT_SEND ? 1 : 0;
        for (int f = 0; f < frames; f++) {
            uint32_t i = lat.pos + (uint32_t)f;
            if (i < LAT_STIMULUS)
                out[f * AUDIO_OUT_WORDS + word] = to_word(stim[i] * lat.amp);
            if (i < LAT_CAPTURE) {
                const int32_t *p = in + f * AUDIO_IN_WORDS;
                cap[0][i] = (float)(p[a] / 2 - p[b] / 2) * (1.0f / RAW);      /* differential, as engine_audio */
                uint32_t ma = p[a] < 0 ? 0u - (uint32_t)p[a] : (uint32_t)p[a];
                uint32_t mb = p[b] < 0 ? 0u - (uint32_t)p[b] : (uint32_t)p[b];
                ma = ma > mb ? ma : mb;
                lat.peak = ma > lat.peak ? ma : lat.peak;
            }
        }
    }
    lat.pos += (uint32_t)frames;
    if (lat.pos >= LAT_CAPTURE)
        lat.phase = P_READY;
}
