/*
 * The spectrum analyser on the PC: its levels (dsp/spectrum.h) and its taps' rings (engine/scope.h).
 */
#include "dsp/spectrum.h"
#include "engine/scope.h"
#include "test.h"
#include <math.h>
#include <string.h>

#define COLS 466

static struct spectrum sp;
static float x[SPEC_N], db[COLS];

static void tone(float hz, float level_db, float noise)
{
    float a = SPEC_FULL_SCALE * powf(10.0f, level_db / 20.0f);
    uint32_t r = 1;
    for (int i = 0; i < SPEC_N; i++) {
        r = r * 1664525u + 1013904223u;
        x[i] = a * sinf(2.0f * (float)M_PI * hz * (float)i / 44100.0f + 0.3f) +
               noise * ((float)(r >> 8) / 16777216.0f - 0.5f);
    }
}

/* the loudest column within +-span of hz */
static float peak_near(float hz, int span, int *at)
{
    int c0 = (int)spectrum_x(&sp, hz);
    float m = SPEC_FLOOR_DB;
    for (int c = c0 - span; c <= c0 + span; c++)
        if (c >= 0 && c < COLS && db[c] > m) {
            m = db[c];
            if (at)
                *at = c;
        }
    return m;
}

static void test_levels(void)
{
    CHECK(spectrum_init(&sp, COLS, 20.0f, 20000.0f, 44100.0f) == 0, "set up");
    CHECK(fabsf(spectrum_hz(&sp, 0) - 20.0f) < 0.01f && fabsf(spectrum_hz(&sp, COLS) - 20000.0f) < 2.0f &&
          fabsf(spectrum_x(&sp, spectrum_hz(&sp, 123.4f)) - 123.4f) < 0.01f, "the axis: 20 Hz .. 20 kHz, x <-> Hz");
    static const struct { float hz, level; } cases[] = {{1000, -20}, {82.4f, -30}, {110, -6}, {5000, -40},
                                                        {12000, -12}, {40, -24}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        tone(cases[i].hz, cases[i].level, 0);
        spectrum_run(&sp, x, db);
        int at = -1;
        float got = peak_near(cases[i].hz, 3, &at);
        float hz = spectrum_hz(&sp, (float)at + 0.5f);
        CHECK(fabsf(got - cases[i].level) < 1.6f && fabsf(hz / cases[i].hz - 1.0f) < 0.05f,
              "%.1f Hz at %.0f dB: reads %.1f dB at %.1f Hz", cases[i].hz, cases[i].level, got, hz);
    }
    tone(1000, -20, 0);
    spectrum_run(&sp, x, db);
    CHECK(peak_near(5000, 20, 0) < -100.0f && peak_near(150, 20, 0) < -100.0f,
          "away from a tone: %.1f dB (5 kHz), %.1f dB (150 Hz)", peak_near(5000, 20, 0), peak_near(150, 20, 0));
    /* 10.8 Hz bins and the Hann main lobe (+-2 bins): notes a third apart at A4 come out as two peaks */
    tone(440, -20, 0);
    for (int i = 0; i < SPEC_N; i++)
        x[i] += 0.5f * SPEC_FULL_SCALE * 0.1f * sinf(2.0f * (float)M_PI * 554.37f * (float)i / 44100.0f);
    spectrum_run(&sp, x, db);
    float a = peak_near(494, 1, 0), b = peak_near(554.37f, 1, 0);
    CHECK(peak_near(440, 1, 0) > -22.0f && b > -28.0f && a < b - 20.0f,
          "A4 and C#5 apart: %.1f dB between them under the quieter", b - a);
    memset(x, 0, sizeof x);
    spectrum_run(&sp, x, db);
    int floor_all = 1;
    for (int c = 0; c < COLS; c++)
        floor_all &= db[c] == SPEC_FLOOR_DB;
    CHECK(floor_all, "silence: the floor everywhere");
}

static void test_rings(void)
{
    static float in[128], out[128], b[SCOPE_RING], a[SCOPE_RING];
    int dummy_chain;
    const struct chain *c = (const struct chain *)&dummy_chain;
    scope_watch(c, 3);
    CHECK(scope_taps(c, 3) && !scope_taps(c, 2) && !scope_taps((const struct chain *)&in, 3),
          "only slot 3 of that chain is tapped");
    for (int blk = 0; blk < 3; blk++) {
        for (int f = 0; f < 64; f++) {
            in[2 * f] = (float)(blk * 64 + f);
            in[2 * f + 1] = (float)(blk * 64 + f) + 2.0f;
            out[f] = -(float)(blk * 64 + f);
        }
        scope_push(in, 2, out, 1, 64);
    }
    int n = scope_read(b, a, SCOPE_RING), ok = n == 192;
    for (int i = 0; ok && i < n; i++)
        ok = b[i] == (float)i + 1.0f && a[i] == -(float)i;
    CHECK(ok, "three blocks read back in order, stereo as (L + R) / 2 (%d samples)", n);
    CHECK(scope_read(b, a, SCOPE_RING) == 0, "nothing new");
    for (int blk = 0; blk < SCOPE_RING / 64 + 10; blk++)
        scope_push(in, 2, out, 1, 64);
    CHECK(scope_read(b, a, SCOPE_RING * 2) == SCOPE_RING, "a reader behind gets the newest ring");
    scope_push(in, 2, out, 1, 64);
    scope_watch(c, SCOPE_PRESET);
    CHECK(scope_read(b, a, SCOPE_RING) == 0 && !scope_taps(c, 3) && scope_target() == SCOPE_PRESET,
          "a new target empties the rings");
    scope_watch(0, SCOPE_OFF);
}

void test_spectrum(void)
{
    test_levels();
    test_rings();
}
