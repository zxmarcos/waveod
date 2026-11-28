/* Simulator: audio callback run on demand; the guitar input is silent, a test tone, or a cable from an output
   (host_audio_loop: what the output played 128 samples, the buffers, plus `extra` ago). */
#include "host/host.h"
#include "dsp/io_filter.h"
#include "platform/audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static audio_callback audio_cb;
static uint32_t blocks;
static double tone_hz, tone_level, phase;

#define LOOP_HIST 8192
static int loop_out = -1, loop_extra;
static int32_t loop_played[LOOP_HIST];
static uint32_t loop_t;

void host_audio_loop(int out_word, int extra)
{
    loop_out = out_word;
    loop_extra = extra;
    loop_t = 0;
    for (int i = 0; i < LOOP_HIST; i++)
        loop_played[i] = 0;
}

void host_audio_tone(double hz, double level)
{
    tone_hz = hz;
    tone_level = level;
}

/* a WAV file on the guitar input: its first channel, 1.0 = what full scale on USB playback channel 3 (re-amping)
   brings into the chain, at the same place (past the input filter) */
static float *wav;
static long wav_n, wav_at;
static double wav_gain = 1.0;
static FILE *aout;                      /* host_audio_out: L, R as float32 on full scale */

int host_audio_out(const char *path)
{
    if (aout)
        fclose(aout);
    aout = path && *path ? fopen(path, "wb") : 0;
    return !path || !*path || aout ? 0 : -1;
}

static uint32_t le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

int host_audio_wav(const char *path, double gain_db)
{
    free(wav);
    wav = 0;
    wav_n = wav_at = 0;
    wav_gain = pow(10.0, gain_db / 20.0);
    if (!path || !*path)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)size);
    if (!d || fread(d, 1, (size_t)size, f) != (size_t)size || size < 12 || memcmp(d, "RIFF", 4) ||
        memcmp(d + 8, "WAVE", 4)) {
        fclose(f);
        free(d);
        return -1;
    }
    fclose(f);
    int fmt = 0, ch = 0, bits = 0;
    for (long at = 12; at + 8 <= size;) {
        uint32_t n = le(d + at + 4, 4);
        const uint8_t *c = d + at + 8;
        if (!memcmp(d + at, "fmt ", 4) && n >= 16) {
            fmt = (int)le(c, 2);
            ch = (int)le(c + 2, 2);
            bits = (int)le(c + 14, 2);
            if (fmt == 0xFFFE && n >= 26)
                fmt = (int)le(c + 24, 2);       /* WAVE_FORMAT_EXTENSIBLE: the sub-format's first word */
        } else if (!memcmp(d + at, "data", 4) && ch && bits) {
            int bytes = bits / 8;
            long frames = (long)((n < (uint32_t)(size - at - 8) ? n : (uint32_t)(size - at - 8)) / (uint32_t)(bytes * ch));
            wav = malloc(sizeof(float) * (size_t)(frames ? frames : 1));
            for (long i = 0; i < frames; i++) {
                const uint8_t *s = c + i * bytes * ch;
                float v;
                if (fmt == 3 && bits == 32) {
                    uint32_t u = le(s, 4);
                    memcpy(&v, &u, 4);
                } else if (bits == 16) {
                    v = (float)(int16_t)le(s, 2) / 32768.0f;
                } else if (bits == 24) {
                    v = (float)((int32_t)(le(s, 3) << 8) >> 8) / 8388608.0f;
                } else {
                    v = (float)((double)(int32_t)le(s, 4) / 2147483648.0);
                }
                wav[i] = v;
            }
            wav_n = frames;
            /* 1 / H(z) first: the guitar input's fixed filter (dsp/io_filter.h) then gives the samples back as
               they are, where USB playback 3 joins the chain (a DI recorded from the pedal's dry USB channels is
               already past H(z)) */
            const struct biquad_coef *q = &io_filter_out;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (long i = 0; i < frames; i++) {
                double x = wav[i], y = (q->b0 * x + q->b1 * x1 + q->b2 * x2 - q->a1 * y1 - q->a2 * y2) / q->a0;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                wav[i] = (float)y;
            }
            break;
        }
        at += 8 + (long)n + (n & 1);
    }
    free(d);
    return wav ? 0 : -1;
}

int audio_start(audio_callback cb)
{
    audio_cb = cb;
    return 0;
}

int audio_streaming(void) { return 0; }       /* blocks only run when the script asks */
void audio_set_input_mode(int mode) { (void)mode; }
uint32_t audio_blocks(void) { return blocks; }
uint32_t audio_late_blocks(void) { return 0; }

void host_audio_run(int n)
{
    static int32_t in[AUDIO_FRAMES * AUDIO_IN_WORDS], out[AUDIO_FRAMES * AUDIO_OUT_WORDS];
    for (int i = 0; i < n && audio_cb; i++) {
        for (int f = 0; f < AUDIO_FRAMES; f++) {
            /* a string-like tone on the guitar input: right word of ADC0 (the engine takes R - L) */
            double v = 0;
            for (int h = 1; h <= 4 && tone_hz > 0; h++)
                v += sin(phase * h) / h;
            phase += 2 * 3.14159265358979 * tone_hz / 44100.0;
            in[f * AUDIO_IN_WORDS + 3] = (int32_t)(v * tone_level * 980586112.0 * 2);
            in[f * AUDIO_IN_WORDS + 0] = 0;
            if (wav_at < wav_n) {               /* differential, w3 / 2 - w0 / 2 = the sample on 2^31 */
                double x = wav[wav_at++] * wav_gain;
                x = x > 0.999 ? 0.999 : x < -0.999 ? -0.999 : x;
                in[f * AUDIO_IN_WORDS + 3] += (int32_t)(x * 2147483647.0);
                in[f * AUDIO_IN_WORDS + 0] = -(int32_t)(x * 2147483647.0);
            }
            if (loop_out >= 0) {                /* half the level, into the guitar input or the FX return */
                long t = (long)loop_t + f - 128 - loop_extra;
                int32_t x = t < 0 ? 0 : loop_played[t % LOOP_HIST] / 2;
                int a = loop_out == 1 ? 4 : 3, b = loop_out == 1 ? 1 : 0;
                in[f * AUDIO_IN_WORDS + a] = x;
                in[f * AUDIO_IN_WORDS + b] = -x;
            }
        }
        audio_cb(in, out, AUDIO_FRAMES);
        if (aout) {
            float lr[2 * AUDIO_FRAMES];
            for (int f = 0; f < AUDIO_FRAMES; f++) {
                lr[2 * f] = (float)out[f * AUDIO_OUT_WORDS + 0] / 980586112.0f;
                lr[2 * f + 1] = (float)out[f * AUDIO_OUT_WORDS + 2] / 980586112.0f;
            }
            fwrite(lr, sizeof lr, 1, aout);
        }
        if (loop_out >= 0)
            for (int f = 0; f < AUDIO_FRAMES; f++)
                loop_played[(loop_t + (uint32_t)f) % LOOP_HIST] = out[f * AUDIO_OUT_WORDS + loop_out];
        loop_t += AUDIO_FRAMES;
        blocks++;
    }
}
