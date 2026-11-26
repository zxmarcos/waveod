#include "model/tuner.h"
#include "dsp/pitch.h"
#include "engine/audio_engine.h"
#include "platform/time.h"
#include <string.h>
#include "platform/sdram.h"

#define HOP          256                    /* new samples between estimates */
#define MIN_CLARITY  0.80f
#define HOLD_MS      400
#define SMOOTH       0.35f                  /* weight of a new estimate while the note holds */

static struct {
    int running;
    float a4;
    float buf[PITCH_N];
    int fresh;                              /* samples received since the last estimate */
    int candidate, candidate_count;         /* a different note waiting for confirmation (midi number) */
    int shown_midi;
    uint32_t seen_at;
    struct tuner_reading r;
} t SDRAM_BSS;

static const char *const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char *tuner_note_name(int note)
{
    return note >= 0 && note < 12 ? names[note] : "-";
}

void tuner_start(uint16_t a4_hz, int mute)
{
    memset(&t, 0, sizeof t);
    t.a4 = a4_hz ? (float)a4_hz : 440.0f;
    t.running = 1;
    t.shown_midi = -1;
    engine_set_mute(mute);
    engine_tuner_feed(1);
}

void tuner_stop(void)
{
    engine_tuner_feed(0);
    engine_set_mute(0);
    t.running = 0;
}

int tuner_running(void)
{
    return t.running;
}

static int round_int(float v)
{
    return (int)(v >= 0 ? v + 0.5f : v - 0.5f);
}

static int analyse(void)
{
    struct pitch_result p;
    pitch_detect(t.buf, &p);
    uint32_t now = time_ms();
    if (p.hz <= 0 || p.clarity < MIN_CLARITY) {
        if (t.r.valid && now - t.seen_at > HOLD_MS) {
            t.r.valid = 0;
            t.shown_midi = -1;
            return 1;
        }
        return 0;
    }
    float midi = 69.0f + 12.0f * pitch_log2(p.hz / t.a4);
    int n = round_int(midi);
    float cents = (midi - (float)n) * 100.0f;
    if (n != t.shown_midi) {
        /* a new note: show it once it is seen twice in a row (or at once when nothing is shown) */
        if (t.shown_midi >= 0 && (n != t.candidate || ++t.candidate_count < 2)) {
            if (n != t.candidate) {
                t.candidate = n;
                t.candidate_count = 1;
            }
            return 0;
        }
        t.shown_midi = n;
        t.r.cents = cents;
        t.r.hz = p.hz;
    } else {
        t.r.cents += SMOOTH * (cents - t.r.cents);
        t.r.hz += SMOOTH * (p.hz - t.r.hz);
    }
    t.candidate_count = 0;
    t.seen_at = now;
    t.r.valid = 1;
    t.r.note = ((n % 12) + 12) % 12;
    t.r.octave = n / 12 - 1;
    return 1;
}

int tuner_poll(void)
{
    if (!t.running)
        return 0;
    float in[HOP];
    int changed = 0, n;
    while ((n = engine_tuner_read(in, HOP)) > 0) {
        memmove(t.buf, t.buf + n, sizeof(float) * (PITCH_N - n));
        memcpy(t.buf + PITCH_N - n, in, sizeof(float) * n);
        t.fresh += n;
        if (t.fresh >= HOP) {
            t.fresh = 0;
            changed |= analyse();
        }
    }
    if (t.r.valid && time_ms() - t.seen_at > HOLD_MS) {
        t.r.valid = 0;
        t.shown_midi = -1;
        changed = 1;
    }
    return changed;
}

void tuner_get(struct tuner_reading *out)
{
    *out = t.r;
}
