/* Simulator: audio callback run on demand; the guitar input is silent, a test tone, or a cable from an output
   (host_audio_loop: what the output played 128 samples, the buffers, plus `extra` ago). */
#include "host/host.h"
#include "platform/audio.h"
#include <math.h>


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
            if (loop_out >= 0) {                /* half the level, into the guitar input or the FX return */
                long t = (long)loop_t + f - 128 - loop_extra;
                int32_t x = t < 0 ? 0 : loop_played[t % LOOP_HIST] / 2;
                int a = loop_out == 1 ? 4 : 3, b = loop_out == 1 ? 1 : 0;
                in[f * AUDIO_IN_WORDS + a] = x;
                in[f * AUDIO_IN_WORDS + b] = -x;
            }
        }
        audio_cb(in, out, AUDIO_FRAMES);
        if (loop_out >= 0)
            for (int f = 0; f < AUDIO_FRAMES; f++)
                loop_played[(loop_t + (uint32_t)f) % LOOP_HIST] = out[f * AUDIO_OUT_WORDS + loop_out];
        loop_t += AUDIO_FRAMES;
        blocks++;
    }
}
