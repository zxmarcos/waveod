/* The latency test against a simulated pedal: the converters' words go out, come back through a "cable" after
   the buffers' 128 samples plus a converter delay, with noise, gain, polarity; and through a fake chain. */
#include "engine/latency.h"
#include "platform/audio.h"
#include "test.h"
#include <math.h>
#include <string.h>

#define RAW     2147483648.0
#define HIST    16384

struct sim {
    int route_out;              /* the output word the cable takes, -1 = no cable */
    int in_a, in_b;             /* input words (differential a - b) */
    double gain, noise;         /* cable gain (negative: inverted), noise amplitude (of full scale) */
    int extra;                  /* converter delay beyond the buffers' 128 samples */
    int half;                   /* the cable also averages two samples: 0.5 more */
    int ts;                     /* a TS cable: the input's other side grounded */
    int chain_delay;            /* PRESET: the fake chain's delay, and a louder echo later */
    int32_t played[HIST];       /* output stream of route_out, by sample */
    uint32_t t;
    uint32_t rng;
    int dirty_out;              /* an output word other than the chirp's was not silent after the first block */
};

static double noise(struct sim *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return ((double)(s->rng >> 8) / 16777216.0 * 2.0 - 1.0) * s->noise;
}

static double played(struct sim *s, long t)
{
    return t < 0 ? 0.0 : (double)s->played[t % HIST] / RAW;
}

/* one block: the input (the cable: what was played 128 + extra samples ago), the engine's output (music),
   the test's hooks */
static void block(struct sim *s, int blockno)
{
    int32_t in[AUDIO_FRAMES * AUDIO_IN_WORDS], out[AUDIO_FRAMES * AUDIO_OUT_WORDS];
    memset(in, 0, sizeof in);
    for (int f = 0; f < AUDIO_FRAMES; f++) {
        long t = (long)s->t + f - LAT_BUFFERS - s->extra;
        double x = 0;
        if (s->route_out >= 0)
            x = s->half ? 0.5 * (played(s, t) + played(s, t - 1)) : played(s, t);
        x = x * s->gain + noise(s);
        x = x > 0.999 ? 0.999 : x < -0.999 ? -0.999 : x;           /* the converter saturates */
        in[f * AUDIO_IN_WORDS + s->in_a] = (int32_t)(x * RAW);
        in[f * AUDIO_IN_WORDS + s->in_b] = s->ts ? 0 : (int32_t)(-x * RAW);
    }
    for (int i = 0; i < AUDIO_FRAMES * AUDIO_OUT_WORDS; i++)
        out[i] = 0x12345600;                                         /* the engine's sound */
    if (latency_preset()) {
        static float hist[2 * HIST];
        float io[2 * AUDIO_FRAMES];
        for (int i = 0; i < 2 * AUDIO_FRAMES; i++)
            io[i] = 0.3f;
        latency_chain_in(io, AUDIO_FRAMES);
        for (int f = 0; f < AUDIO_FRAMES; f++) {
            uint32_t t = s->t + (uint32_t)f;
            hist[t % HIST] = io[2 * f];
            long d1 = (long)t - s->chain_delay, d2 = (long)t - s->chain_delay - 600;
            float y = (d1 >= 0 ? 0.5f * hist[d1 % HIST] : 0) + (d2 >= 0 ? 1.5f * hist[d2 % HIST] : 0);
            io[2 * f] = y;
            io[2 * f + 1] = 0.2f * y;
        }
        latency_chain_out(io, AUDIO_FRAMES);
    }
    int active = latency_quiet();
    latency_io(in, out, AUDIO_FRAMES);
    for (int f = 0; f < AUDIO_FRAMES; f++) {
        for (int w = 0; w < AUDIO_OUT_WORDS; w++)
            if (active && blockno > 0 && w != s->route_out && out[f * AUDIO_OUT_WORDS + w] != 0)
                s->dirty_out = 1;
        s->played[(s->t + (uint32_t)f) % HIST] = s->route_out >= 0 ? out[f * AUDIO_OUT_WORDS + s->route_out] : 0;
    }
    s->t += AUDIO_FRAMES;
}

static void run(struct sim *s, int route, struct latency_result *r)
{
    latency_start(route);
    for (int b = 0; b < 4000; b++) {
        block(s, b);
        latency_poll();
        latency_get(r);
        if (r->status != LAT_BUSY)
            return;
    }
}

static void cable(struct sim *s, int route)
{
    memset(s, 0, sizeof *s);
    s->route_out = route == LAT_OUT_L ? 0 : route == LAT_OUT_R ? 2 : 1;
    s->in_a = route == LAT_SEND ? 4 : 3;
    s->in_b = route == LAT_SEND ? 1 : 0;
    s->gain = 0.5;
    s->noise = 0.0003;                  /* -70 dBFS */
    s->extra = 7;
    s->rng = 12345;
}

static void test_find(void)
{
    static float ref[512], x[4096];
    double ph = 0;
    for (int i = 0; i < 512; i++) {
        double t = i / 511.0;
        ref[i] = (float)((0.5 - 0.5 * cos(2 * M_PI * t)) * sin(ph));
        ph += 2 * M_PI * (300 + 11700 * t) / 44100;
    }
    memset(x, 0, sizeof x);
    for (int i = 0; i < 512; i++)
        x[200 + i] = -0.25f * ref[i];
    float m, lag = latency_find(ref, 512, x, 4096, &m);
    CHECK(fabsf(lag - 200) < 0.01f && m > 0.99f, "lag %g match %g", lag, m);
    memset(x, 0, sizeof x);
    float lag2 = latency_find(ref, 512, x, 4096, &m);
    CHECK(lag2 < 0 && m == 0, "silence: lag %g match %g", lag2, m);
}

void test_latency(void)
{
    static struct sim s;
    struct latency_result r;
    test_find();

    cable(&s, LAT_OUT_L);
    run(&s, LAT_OUT_L, &r);
    CHECK(r.status == LAT_OK && r.runs == LAT_RUNS && fabsf(r.mean - 135) < 0.05f && r.max - r.min < 0.1f,
          "OUT L: status %d, %d runs, %g (%g..%g)", r.status, r.runs, r.mean, r.min, r.max);
    CHECK(r.level_db == -30 && r.match > 0.95f, "OUT L: level %d dB, match %g", r.level_db, r.match);
    CHECK(!s.dirty_out, "OUT L: the other outputs silent");
    CHECK(!latency_quiet(), "OUT L: the outputs back to the engine");

    cable(&s, LAT_OUT_R);
    s.gain = -0.3;
    s.extra = 23;
    run(&s, LAT_OUT_R, &r);
    CHECK(r.status == LAT_OK && fabsf(r.mean - 151) < 0.05f, "OUT R inverted: status %d, %g", r.status, r.mean);

    cable(&s, LAT_SEND);
    s.half = 1;
    run(&s, LAT_SEND, &r);
    CHECK(r.status == LAT_OK && fabsf(r.mean - 135.5f) < 0.2f, "SEND, half a sample more: status %d, %g",
          r.status, r.mean);

    cable(&s, LAT_OUT_L);
    s.gain = 0.01;                      /* a quiet return: -30 and -20 dBFS drown in the noise */
    s.noise = 0.003;
    run(&s, LAT_OUT_L, &r);
    CHECK(r.status == LAT_OK && r.level_db > -30 && fabsf(r.mean - 135) < 0.3f, "quiet: status %d, %d dB, %g",
          r.status, r.level_db, r.mean);

    cable(&s, LAT_OUT_L);
    s.route_out = -1;                   /* no cable */
    run(&s, LAT_OUT_L, &r);
    CHECK(r.status == LAT_NO_SIGNAL && r.level_db == -10 && r.runs == 0, "no cable: status %d, %d dB",
          r.status, r.level_db);

    cable(&s, LAT_OUT_L);
    s.gain = 40;                        /* -30 dBFS already clips */
    s.ts = 1;
    run(&s, LAT_OUT_L, &r);
    CHECK(r.status == LAT_CLIPPED, "clipping: status %d", r.status);

    cable(&s, LAT_OUT_L);
    s.ts = 1;
    run(&s, LAT_OUT_L, &r);
    CHECK(r.status == LAT_OK && fabsf(r.mean - 135) < 0.05f, "TS cable: status %d, %g", r.status, r.mean);

    cable(&s, LAT_OUT_L);
    s.route_out = -1;
    s.chain_delay = 37;
    run(&s, LAT_PRESET, &r);
    CHECK(r.status == LAT_OK && fabsf(r.mean - 37) < 0.05f && r.level_db == 0, "PRESET: status %d, %g",
          r.status, r.mean);

    cable(&s, LAT_OUT_L);
    latency_start(LAT_OUT_L);
    for (int b = 0; b < 30; b++)
        block(&s, b);
    latency_cancel();
    latency_get(&r);
    CHECK(r.status == LAT_CANCELLED && !latency_quiet() && !latency_preset(), "cancelled: status %d", r.status);
    CHECK(!strcmp(latency_status_name(LAT_NO_SIGNAL), "NO SIGNAL"), "status name");
}
