/* waveod's Cab / IR engine (engine/cabir.c) against a direct convolution in double precision, at every block
   size; and --cab-apply, which runs it on blocks given by tools/check_cab.py (the original's engine gets the
   same blocks in Unicorn). */
#include "dsp/rfft.h"
#include "engine/cabir.h"
#include "engine/blocks.h"
#include "engine/cabmic.h"
#include "model/param_format.h"
#include "test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct cabir cab;

static uint32_t rng = 1;
static float rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)((double)(rng >> 8) / 16777216.0 * 2.0 - 1.0);
}

/* the expected output: the IR in double, then level and the biquads computed like the engine (single precision:
   a low cut near DC turns float rounding into errors of 1e-5, the same in the original) */
static void reference(const int32_t *ir, const float *x, int n, const float *knobs, double *out)
{
    float lo[6], hi[6], ls[4] = { 0 }, hs[4] = { 0 };
    float w = knobs[5] * 1.4247585e-4f, cs = cosf(w), al = sinf(w) * 0.70721358f;
    float l[6] = { (cs + 1.0f) * 0.5f, -(cs + 1.0f), (cs + 1.0f) * 0.5f, al + 1.0f, cs * -2.0f, 1.0f - al };
    w = knobs[6] * 1.4247585e-4f, cs = cosf(w), al = sinf(w) * 0.70721358f;
    float h[6] = { (1.0f - cs) * 0.5f, 1.0f - cs, (1.0f - cs) * 0.5f, al + 1.0f, cs * -2.0f, 1.0f - al };
    memcpy(lo, l, sizeof lo);
    memcpy(hi, h, sizeof hi);
    float g = knobs[1] * 0.01f;
    for (int i = 0; i < n; i++) {
        double acc = 0;
        for (int k = 0; k < CABIR_LEN && k <= i; k++)
            acc += (double)ir[k] / 8388608.0 * x[i - k];
        float v = (float)acc * 0.27f * g;
        float *bs[2] = { hs, ls }, *bc[2] = { hi, lo };
        for (int s = 0; s < 2; s++) {
            float *st = bs[s], *k = bc[s];
            float y = (k[0] * v + k[1] * st[0] + k[2] * st[1] - k[4] * st[2] - k[5] * st[3]) / k[3];
            st[1] = st[0], st[0] = v, st[3] = st[2], st[2] = y;
            v = y;
        }
        out[i] = v;
    }
}

static void run_blocks(int block, const int32_t *ir, const float *knobs, const float *x, float *y, int n)
{
    cabir_init(&cab, knobs, ir, block);
    static float io[2 * CABIR_BLOCK];
    for (int b = 0; b < n; b += block) {
        for (int i = 0; i < block; i++) {
            io[2 * i] = x[b + i];
            io[2 * i + 1] = 0.5f;                   /* the right input is not used */
        }
        cabir_process(&cab, io, block, knobs, 1);
        for (int i = 0; i < block; i++) {
            y[b + i] = io[2 * i];
            if (io[2 * i + 1] != io[2 * i])
                y[b + i] = NAN;
        }
    }
}

/* the real FFT against a direct DFT, and forward + inverse */
static void test_rfft(void)
{
    static uint32_t mem[RFFT_MEM_WORDS(128)];
    static float x[128], X[130], back[128];
    for (int n = 8; n <= 128; n *= 2) {
        struct rfft f;
        CHECK(rfft_init(&f, n, mem) == 0, "rfft %d", n);
        for (int i = 0; i < n; i++)
            x[i] = rnd();
        rfft_forward(&f, x, X);
        double err = 0, mag = 0;
        for (int k = 0; k <= n / 2; k++) {
            double re = 0, im = 0;
            for (int i = 0; i < n; i++) {
                re += x[i] * cos(-2 * M_PI * k * i / n);
                im += x[i] * sin(-2 * M_PI * k * i / n);
            }
            double e = hypot(X[2 * k] - re, X[2 * k + 1] - im);
            err = e > err ? e : err;
            mag = hypot(re, im) > mag ? hypot(re, im) : mag;
        }
        rfft_inverse(&f, X, back);
        double rt = 0;
        for (int i = 0; i < n; i++)
            rt = fabs(back[i] - x[i]) > rt ? fabs(back[i] - x[i]) : rt;
        CHECK(err < mag * 1e-6 && rt < 1e-6, "rfft %d: error %.3g of %.3g, round trip %.3g", n, err, mag, rt);
    }
}

/* |H| of n samples at hz, dB */
static double response_db(const float *h, int n, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) {
        re += h[i] * cos(-2 * M_PI * hz * i / 44100.0);
        im += h[i] * sin(-2 * M_PI * hz * i / 44100.0);
    }
    return 10 * log10(re * re + im * im + 1e-30);
}

static void test_mic(const int32_t *ir)
{
    static float plain[CABIR_LEN], moved[CABIR_LEN];
    float flat[CABMIC_BANDS] = { 0 }, db[CABMIC_BANDS];
    for (int k = 0; k < CABIR_LEN; k++)
        plain[k] = (float)ir[k] / 8388608.0f;
    cabmic_apply(ir, CABIR_LEN, flat, moved);
    double err = 0, peak = 0;
    for (int k = 0; k < CABIR_LEN - 64; k++) {                 /* the last 64 are faded */
        err = fabs(moved[k] - plain[k]) > err ? fabs(moved[k] - plain[k]) : err;
        peak = fabs(plain[k]) > peak ? fabs(plain[k]) : peak;
    }
    CHECK(err < peak * 1e-4, "mic, flat curve: the IR as it is (error %.3g of %.3g)", err, peak);

    float knobs[15] = { 0 };
    CHECK(cabmic_neutral(knobs), "mic: zeros are neutral");
    knobs[CABMIC_KNOB_POSITION] = 100, knobs[CABMIC_KNOB_DISTANCE] = -100, knobs[CABMIC_KNOB_ANGLE] = 30;
    cabmic_curve(knobs, db);
    cabmic_apply(ir, CABIR_LEN, db, moved);
    double worst = 0;
    for (int k = 4; k < CABMIC_BANDS - 1; k++) {                 /* 63 Hz .. 12.8 kHz */
        double hz = cabmic_bands[k], got = 0, want = 0;
        /* averaged over the band (the IR's own response is ragged) */
        for (int j = -4; j <= 4; j++) {
            double f = hz * pow(2.0, j / 27.0);
            got += response_db(moved, CABIR_LEN, f) - response_db(plain, CABIR_LEN, f);
            want += cabmic_db_at(db, (float)f);
        }
        double e = fabs(got - want) / 9;
        worst = e > worst ? e : worst;
    }
    CHECK(worst < 1.5, "mic, edge / on the grille / 30 degrees: the change follows the curve (worst %.2f dB)", worst);
    CHECK(db[24] < -5.0f && db[2] > 0.3f, "mic: the edge darker (%.1f dB at 6.4 kHz), the grille fuller (%.1f dB at 40 Hz)",
          db[24], db[2]);

    /* mic types: the 57 where the IR was captured is its curve against the median mic, with its own axes */
    float kt[15] = { 0 };
    kt[CABMIC_KNOB_TYPE] = 1;
    CHECK(cabmic_type(kt) == 1 && !cabmic_neutral(kt), "mic type 1: the 57, not neutral");
    cabmic_curve(kt, db);
    double off = 0;
    for (int k = 0; k < CABMIC_BANDS; k++)
        off = fabs(db[k] - cabmic_type_curve[0][k]) > off ? fabs(db[k] - cabmic_type_curve[0][k]) : off;
    CHECK(off < 1e-5, "mic 57 at the captured place: its curve (off by %.3g dB)", off);
    CHECK(db[2] < -4.0f && db[24] > 3.0f, "mic 57: low end cut (%.1f dB at 40 Hz), presence peak (%.1f dB at 6.4 kHz)",
          db[2], db[24]);
    kt[CABMIC_KNOB_POSITION] = 100;
    cabmic_curve(kt, db);
    off = 0;
    for (int k = 0; k < CABMIC_BANDS; k++) {
        float want = cabmic_type_curve[0][k] + cabmic_type_position[0][3][k] - cabmic_type_position[0][CABMIC_REF][k];
        want = want < -18.0f ? -18.0f : want > 18.0f ? 18.0f : want;
        off = fabs(db[k] - want) > off ? fabs(db[k] - want) : off;
    }
    CHECK(off < 1e-5, "mic 57 at the edge: the 57's own position curve (off by %.3g dB)", off);
    kt[CABMIC_KNOB_POSITION] = 0;
    kt[CABMIC_KNOB_TYPE] = (float)(CABMIC_TYPES + 1);
    CHECK(cabmic_type(kt) == 0 && cabmic_neutral(kt), "mic type past the last: as captured");
    kt[CABMIC_KNOB_TYPE] = -1;
    CHECK(cabmic_type(kt) == 0, "mic type below 0: as captured");
    struct lib_param lp = { 0 };
    const struct wvfx_desc *engine = blocks_engine(MENU_CAB, 1);
    CHECK(engine && blocks_param(engine, CABMIC_PARAMS - 1, &lp) == 0 && lp.idx == CABMIC_KNOB_TYPE,
          "mic type: the engine block's last parameter");
    int names_ok = param_list_size(&lp) == CABMIC_TYPES + 1;
    for (int t = 0; t < CABMIC_TYPES && names_ok; t++)
        names_ok = strcmp(param_list_item(&lp, t + 1), cabmic_type_names[t]) == 0;
    CHECK(names_ok, "mic type names: the parameter's list matches the tables");
    char text[24];
    lp.min = 0, lp.max = CABMIC_TYPES;
    param_format(&lp, 1.0f, 0, text, sizeof text);
    CHECK(strcmp(text, "57 Dynamic") == 0, "mic type 1 shows \"%s\"", text);

    /* a mic move while playing: after the crossfade the output is the one of an engine set up that way */
    enum { B = 64, N = 64 * 40 };
    static float x[N], y1[N], y2[N];
    for (int i = 0; i < N; i++)
        x[i] = 0.3f * rnd();
    float k0[15] = { 0 };
    k0[1] = 50, k0[5] = 19, k0[6] = 20001;
    float k1[15];
    memcpy(k1, k0, sizeof k1);
    k1[CABMIC_KNOB_POSITION] = 60;
    static struct cabir other;
    cabir_init(&other, k1, ir, B);
    cabir_init(&cab, k0, ir, B);
    int switched = 0;
    for (int b = 0; b < N; b += B) {
        float io[2 * B], io2[2 * B];
        for (int i = 0; i < B; i++)
            io[2 * i] = io2[2 * i] = x[b + i], io[2 * i + 1] = io2[2 * i + 1] = 0;
        if (b == 10 * B)
            switched = cabir_poll(&cab, k1);
        cabir_process(&cab, io, B, b >= 10 * B ? k1 : k0, 1);
        cabir_process(&other, io2, B, k1, 1);
        for (int i = 0; i < B; i++)
            y1[b + i] = io[2 * i], y2[b + i] = io2[2 * i];
    }
    double diff = 0, top = 0;
    for (int i = 30 * B; i < N; i++) {                          /* the history is the same from 10 + 16 blocks on; */
        diff = fabs(y1[i] - y2[i]) > diff ? fabs(y1[i] - y2[i]) : diff;   /* the 19 Hz low cut still remembers */
        top = fabs(y2[i]) > top ? fabs(y2[i]) : top;
    }
    CHECK(switched && diff < top * 1e-3, "mic moved while playing: switched %d, then as set up (%.3g of %.3g)", switched,
          diff, top);
    CHECK(cabir_poll(&cab, k1) == 0, "mic: unchanged knobs, nothing to do");
    cabir_process(&cab, (float[2 * B]){ 0 }, B, k1, 1);          /* let a pending switch finish */
    cabir_process(&cab, (float[2 * B]){ 0 }, B, k1, 1);
    k1[CABMIC_KNOB_TYPE] = 1;
    CHECK(cabir_poll(&cab, k1) == 1, "mic: a new type recomputes the IR");
}

void test_cabir(void)
{
    test_rfft();
    enum { N = 4096 };
    static int32_t ir[CABIR_LEN];
    static float x[N], y[N];
    static double ref[N];
    for (int k = 0; k < CABIR_LEN; k++)              /* a decaying noise burst, 24-bit like a cab */
        ir[k] = (int32_t)(rnd() * 8000000.0f * expf(-(float)k / 150.0f));
    for (int i = 0; i < N; i++)
        x[i] = 0.3f * rnd();
    float knobs[15] = { 0 };
    knobs[1] = 70, knobs[5] = 1000, knobs[6] = 9000;      /* a low cut near DC would show its own float
                                                            rounding (about -80 dB), not the convolution's */
    reference(ir, x, N, knobs, ref);
    double peak = 0;
    for (int i = 0; i < N; i++)
        peak = fabs(ref[i]) > peak ? fabs(ref[i]) : peak;
    const int blocks[3] = { 64, 32, 16 };
    for (int s = 0; s < 3; s++) {
        run_blocks(blocks[s], ir, knobs, x, y, N);
        double err = 0, sig = 0;
        int both = 1;
        for (int i = 0; i < N; i++) {
            if (isnan(y[i]))
                both = 0;
            err += (y[i] - ref[i]) * (y[i] - ref[i]);
            sig += ref[i] * ref[i];
        }
        double db = 10.0 * log10(sig / (err > 0 ? err : 1e-300));
        CHECK(both && db > 100.0, "block %d: %.1f dB under the signal, L = R %d", blocks[s], db, both);
    }

    test_mic(ir);

    /* the level moves 0.002 per block towards the knob; disabled = untouched; no IR = silence */
    cabir_init(&cab, knobs, ir, 64);
    float io[2 * 64];
    knobs[1] = 100;
    for (int b = 0; b < 10; b++) {
        memset(io, 0, sizeof io);
        cabir_process(&cab, io, 64, knobs, 1);
    }
    CHECK(fabsf(cab.gain - (0.7f + 10 * 0.002f + 0.3f * 10 * 2.2676e-4f)) < 2e-4f, "level ramp %g", cab.gain);
    for (int i = 0; i < 128; i++)
        io[i] = 0.25f;
    cabir_process(&cab, io, 64, knobs, 0);
    CHECK(io[0] == 0.25f && io[127] == 0.25f, "disabled: untouched");
    cabir_init(&cab, knobs, 0, 64);
    cabir_process(&cab, io, 64, knobs, 1);
    CHECK(io[0] == 0.0f && io[127] == 0.0f, "no IR: silence");
}

/*
 * OPS: u32 n, int32 ir[n] (n = 0: no data); then records {u32 frames, u32 enabled, f32 knobs[15], f32 io[2 x
 * frames]} (frames = 0: Init with these knobs, no audio). OUT: io of every record after the block.
 */
int cab_apply(const char *ops_path, const char *out_path)
{
    FILE *in = fopen(ops_path, "rb"), *out = fopen(out_path, "wb");
    if (!in || !out)
        return 1;
    static int32_t ir[CABIR_LEN];
    uint32_t n = 0;
    if (fread(&n, 4, 1, in) != 1 || n > CABIR_LEN || fread(ir, 4, n, in) != n)
        return 1;
    uint32_t head[2];
    float knobs[15];
    static float io[2 * CABIR_BLOCK];
    while (fread(head, sizeof head, 1, in) == 1 && fread(knobs, sizeof knobs, 1, in) == 1) {
        if (head[0] == 0) {
            cabir_init(&cab, knobs, n ? ir : 0, CABIR_BLOCK);
            continue;
        }
        if (head[0] > CABIR_BLOCK || fread(io, sizeof(float), 2 * head[0], in) != 2 * head[0])
            return 1;
        cabir_process(&cab, io, (int)head[0], knobs, (int)head[1]);
        fwrite(io, sizeof(float), 2 * head[0], out);
    }
    fclose(in);
    fclose(out);
    return 0;
}
