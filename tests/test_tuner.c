/* Tuner pitch detection on synthetic strings, and the settings record next to the original's records. */
#include "dsp/pitch.h"
#include "fw/flash_map.h"
#include "fw/settings_record.h"
#include "host/host.h"
#include "test.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* a plucked-string-like tone at 44.1 kHz, decimated by pair averages like the engine does */
static void string_tone(float *out, float hz, float level, unsigned seed)
{
    srand(seed);
    for (int i = 0; i < PITCH_N; i++) {
        float v[2];
        for (int k = 0; k < 2; k++) {
            float t = (float)(2 * i + k) / 44100.0f, s = 0;
            for (int h = 1; h <= 8; h++)                  /* strong upper harmonics, the 2nd strongest */
                s += (h == 2 ? 1.2f : 1.0f / (float)h) * sinf(6.2831853f * hz * (float)h * t + 0.7f * (float)h);
            v[k] = level * s / 3.0f + level * 0.01f * ((float)rand() / RAND_MAX - 0.5f);
        }
        out[i] = 0.5f * (v[0] + v[1]);
    }
}

static float cents_off(float got, float want)
{
    return 1200.0f * pitch_log2(got / want);
}

static void test_pitch(void)
{
    static const float notes[] = { 55.0f, 61.74f, 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 440.0f,
                                   659.26f, 987.77f };
    static float buf[PITCH_N];
    float worst = 0;
    for (unsigned i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        for (int detune = -30; detune <= 30; detune += 15) {
            float f = notes[i] * powf(2.0f, (float)detune / 1200.0f);
            string_tone(buf, f, 0.05f, i * 7 + (unsigned)detune + 100);
            struct pitch_result r;
            pitch_detect(buf, &r);
            float err = r.hz > 0 ? cents_off(r.hz, f) : 9999.0f;
            if (fabsf(err) > fabsf(worst))
                worst = err;
            CHECK(r.hz > 0 && fabsf(err) < 1.0f && r.clarity > 0.8f, "%.2f Hz read as %.2f Hz (%.2f cents, clarity %.2f)",
                  f, r.hz, err, r.clarity);
        }
    }
    printf("tuner: worst error %.3f cents over %d tones\n", worst, (int)(sizeof notes / sizeof notes[0]) * 5);

    memset(buf, 0, sizeof buf);
    struct pitch_result r;
    pitch_detect(buf, &r);
    CHECK(r.hz == 0, "silence gave %.2f Hz", r.hz);
    for (int i = 0; i < PITCH_N; i++)
        buf[i] = 0.05f * ((float)rand() / RAND_MAX - 0.5f);
    pitch_detect(buf, &r);
    CHECK(r.hz == 0 || r.clarity < 0.8f, "noise gave %.2f Hz with clarity %.2f", r.hz, r.clarity);

    for (float x = 0.01f; x < 1000.0f; x *= 1.37f)
        CHECK(fabsf(pitch_log2(x) - log2f(x)) < 2e-6f * (1.0f + fabsf(log2f(x))), "log2(%g)", x);
}

static void test_settings_record(void)
{
    memset(host_flash_image() + FLASH_RECORD_STORE, 0xff, FLASH_RECORD_STORE_SIZE);
    struct preset_source src;
    presets_open(&src);
    uint8_t in[6] = { 70, 1, 0, 0, 0xb6, 0x01 }, out[6];
    CHECK(settings_record_read(&src, out, sizeof out) < 0, "no settings on a blank store");
    CHECK(settings_record_write(&src, in, sizeof in) == 0, "write settings");
    CHECK(settings_record_read(&src, out, sizeof out) == 6 && memcmp(in, out, 6) == 0, "read back");
    CHECK(store_record(&src.store, 299) != 0, "kept in record 299");
    for (int i = 0; i < PRESET_COUNT; i++)
        CHECK(src.map[i] != 299, "a preset uses record 299");

    /* a pedal whose preset map points at 299: the settings move to 298, the preset is not touched */
    src.map[7] = 299;
    CHECK(settings_record_read(&src, out, sizeof out) < 0, "299 belongs to a preset now");
    CHECK(settings_record_write(&src, in, sizeof in) == 0 && store_record(&src.store, 298), "moved to 298");
    memset(host_flash_image() + FLASH_RECORD_STORE, 0xff, FLASH_RECORD_STORE_SIZE);
}

void test_tuner(void)
{
    test_pitch();
    test_settings_record();
}
