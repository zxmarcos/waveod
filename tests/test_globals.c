/*
 * The original's global settings record and the DSP around the chain.
 *
 * `--dsp-apply OPS OUT` (for tools/check_dsp.py): OPS holds blocks {u32 kind, u32 on, f32 params[31],
 * f32 io[128], f32 coef[48]}, kind 0 = global EQ, 1 = input filter, 2 = output filter, each kind with its own
 * running state. On the EQ, bit 1 of `on` makes it use `coef` (b0 b1 b2 a0 a1 a2 of the 8 stages) instead of
 * its own design. OUT gets {f32 io[128], f32 coef[48]} per block: the processed io and the EQ's coefficients.
 */
#include "dsp/global_eq.h"
#include "dsp/io_filter.h"
#include "fw/globals_format.h"
#include "test.h"
#include <math.h>
#include <string.h>

static void test_record(void)
{
    struct globals g;
    CHECK(globals_decode(globals_factory, &g) == 0, "factory record decodes");
    CHECK(g.input_level == 0 && g.input_mode == INPUT_ELECTRIC && g.aux_level == 50 && !g.eq_on && g.auto_cab,
          "factory values: level %d mode %d aux %d eq %d", g.input_level, g.input_mode, g.aux_level, g.eq_on);
    CHECK(g.eq[EQ_LEVEL] == 50.0f && g.eq[EQ_B2_F] == 500.0f && g.eq[EQ_HICUT_F] == 20001.0f && g.bpm == 0,
          "factory EQ");
    CHECK(g.fs_group == 1 && g.exp_min[0] == 0 && g.exp_max[0] == 200 && g.exp_toe[0] == 250 &&
          g.exp2_jack == EXP2_PEDAL && g.ext_fn[0] == 0, "factory controls: preset mode, EXP 0-200, toe 250, EXP 2 pedal");

    static uint8_t rec[GLOBALS_RECORD_SIZE], out[GLOBALS_RECORD_SIZE];
    memcpy(rec, globals_factory, sizeof rec);
    rec[0x232] = 200;                       /* out of range: read as 50, kept as stored */
    globals_decode(rec, &g);
    CHECK(g.aux_level == 50, "AUX level out of range reads as 50");
    globals_encode(&g, rec, out);
    CHECK(memcmp(out, rec, sizeof out) == 0, "unchanged settings encode back byte for byte");

    g.input_level = -7;
    g.eq[EQ_B3_G] = 4.0f;
    globals_encode(&g, rec, out);
    int diff = 0;
    for (unsigned i = 0; i < sizeof out; i++)
        diff += out[i] != rec[i];
    float gain;
    memcpy(&gain, out + 0x7c + 4 * EQ_B3_G, 4);
    CHECK(diff == 1 + 2 && (int8_t)out[0x1a] == -7 && gain == 4.0f, "only the changed fields are written (%d)", diff);

    CHECK(globals_decode(0, &g) == -1 && g.aux_level == 50, "no record: factory values");
}

static void test_filters(void)
{
    /* input then output filter: the signal comes back */
    struct biquad_state a[2] = { 0 }, b[2] = { 0 };
    static float x[2 * 1024], y[2 * 1024];
    for (int i = 0; i < 1024; i++)
        x[2 * i] = x[2 * i + 1] = sinf(0.05f * (float)i) + 0.3f * sinf(1.7f * (float)i);
    memcpy(y, x, sizeof y);
    biquad_stereo(a, &io_filter_in, y, 1024);
    biquad_stereo(b, &io_filter_out, y, 1024);
    float err = 0;
    for (int i = 0; i < 2 * 1024; i++)
        err = fmaxf(err, fabsf(y[i] - x[i]));
    CHECK(err < 1e-4f, "output filter undoes the input filter (%g)", err);

    /* a +6 dB peak at 1 kHz */
    float p[EQ_PARAMS];
    for (int k = 0; k < EQ_PARAMS; k++)
        p[k] = eq_param_ranges[k].def;
    p[EQ_B1_ON] = 1.0f;
    p[EQ_B1_F] = 1000.0f;
    p[EQ_B1_G] = 6.0f;
    float db = global_eq_response_db(p, 1000.0f), flat = global_eq_response_db(p, 60.0f);
    CHECK(fabsf(db - 6.0f) < 0.05f && fabsf(flat) < 0.5f, "response: %.2f dB at 1 kHz, %.2f at 60 Hz", db, flat);

    struct global_eq eq;
    global_eq_init(&eq);
    static float s[2 * 64];
    float peak = 0;
    for (int blk = 0; blk < 400; blk++) {
        for (int f = 0; f < 64; f++)
            s[2 * f] = s[2 * f + 1] = sinf(6.2831853f * 1000.0f * (float)(blk * 64 + f) / 44100.0f);
        global_eq_process(&eq, s, 64, p, 1);
        if (blk >= 300)
            for (int f = 0; f < 128; f++)
                peak = fmaxf(peak, fabsf(s[f]));
    }
    CHECK(fabsf(peak - 1.995f) < 0.02f, "1 kHz through the EQ: %.3f (+6 dB)", peak);
}

void test_globals(void)
{
    test_record();
    test_filters();
}

int dsp_apply(const char *ops_path, const char *out_path)
{
    FILE *in = fopen(ops_path, "rb"), *out = fopen(out_path, "wb");
    if (!in || !out)
        return 1;
    static struct global_eq eq;
    struct biquad_state fin[2] = { 0 }, fout[2] = { 0 };
    global_eq_init(&eq);
    uint32_t head[2];
    float params[EQ_PARAMS], io[128], coef[GEQ_STAGES * 6];
    while (fread(head, sizeof head, 1, in) == 1 && fread(params, sizeof params, 1, in) == 1 &&
           fread(io, sizeof io, 1, in) == 1 && fread(coef, sizeof coef, 1, in) == 1) {
        if (head[0] == 0) {
            if (head[1] & 2) {              /* the given coefficients, as if designed for these parameters */
                memcpy(eq.seen, params, sizeof params);
                eq.valid = 1;
                memcpy(eq.coef, coef, sizeof coef);
            }
            global_eq_process(&eq, io, 64, params, (int)(head[1] & 1));
            memcpy(coef, eq.coef, sizeof coef);
        } else {
            biquad_stereo(head[0] == 1 ? fin : fout, head[0] == 1 ? &io_filter_in : &io_filter_out, io, 64);
            memset(coef, 0, sizeof coef);
        }
        fwrite(io, sizeof io, 1, out);
        fwrite(coef, sizeof coef, 1, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}