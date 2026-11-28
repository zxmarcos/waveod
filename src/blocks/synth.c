/*
 * Synth (FX › Synth, waveod's): the guitar turned into a monophonic synthesizer (docs/synth.md).
 *
 * The pitch comes from the clean guitar at the chain's input (io->aux, synth/track.h), so the block can sit after
 * a drive or an amp and still follow what is played. Each block:
 *   - note (docs/synth.md): from silence two agreeing estimates; after a pick's attack (the input's first
 *     difference jumping, the level rising) one change on two agreeing estimates; otherwise votes (a candidate's
 *     estimates for, the note's and others' against; octave or larger jumps need four times as many, and under
 *     -70 dBFS there is no change), so strings ringing
 *     together never add up to a change. While a note plays, any middling pitch holds it and moves it (bends,
 *     slides) by its clarity; it ends when the guitar falls quiet, or after 1 s without a pitch. Only a pick or
 *     a note from silence restarts the filter's envelope and Hold's level, and with Attack above 0 attacks the
 *     note again (the gate falls in 3 ms and rises with Attack: a new stroke of a bow).
 *   - voice: oscillator 1 (Wave: twelve waves, see gen()), oscillator 2 (Interval semitones and Detune cents
 *     above it, at Osc 2; at -12 it is the sub octave), the same wave, through a DC blocker; Octave shifts both;
 *     Glide moves the pitch from note to note.
 *   - Gate: the input level (dBFS RMS) a note from silence needs; a note playing goes on down to 14 dB under it.
 *   - filter: a 4-pole ladder (zero-delay feedback, soft-clipped input) whose cutoff follows the note
 *     (Cutoff 0..100 % = 1..128 times the note's frequency) plus Env Amt (up to 6 octaves) of an envelope that
 *     jumps at every new note or attack and falls with Decay.
 *   - level: Follow, the guitar's own envelope (its dynamics, its decay); Hold, the note's peak until the note
 *     ends, then a release of Decay. Attack fades every note in (swells). Snap rounds the pitch to semitones
 *     (A = 440 Hz).
 *   - out: the synth (mono, on both sides) and the signal at the block, Mix 0 = the signal, 50 = both,
 *     100 = the synth; Level.
 * Bypassed it passes the signal (the synth fades out over a block); the tracker keeps listening, so it comes back
 * on the right note.
 *
 *   Wave  Octave  Osc 2 %  Interval st  Detune ct  Gate dB  Glide ms  Cutoff %  Resonance %  Env Amt %
 *   Attack ms  Decay ms  Mode (Follow / Follow Snap / Hold / Hold Snap)  Mix %  Level dB
 */
#include "synth/track.h"

enum {
    K_WAVE, K_OCTAVE, K_OSC2, K_INTERVAL, K_DETUNE, K_GATE, K_GLIDE, K_CUTOFF, K_RESONANCE, K_ENV, K_ATTACK,
    K_DECAY, K_MODE, K_MIX, K_LEVEL, NKNOBS
};

static const struct wvfx_param params[] = {
    { "Wave", 0.0f, 11.0f, 1.0f, 0.0f, K_WAVE, 0, 0, 0,
      "Saw\0Square\0Pulse\0Triangle\0Sine\0Thin Pulse\0PWM\0Saw+Square\0Supersaw\0Sync\0Organ\0Voice\0", 0 },
    { "Octave", -2.0f, 2.0f, 1.0f, 0.0f, K_OCTAVE, WVFX_FMT_SIGNED, 0, 0, 0, 0 },
    { "Osc 2", 0.0f, 100.0f, 1.0f, 0.0f, K_OSC2, 0, 0, 0, 0, "%.0f%%" },
    { "Interval", -24.0f, 24.0f, 1.0f, 7.0f, K_INTERVAL, WVFX_FMT_SIGNED, 0, 0, 0, 0 },
    { "Detune", 0.0f, 50.0f, 1.0f, 8.0f, K_DETUNE, 0, 0, 0, 0, "%.0f ct" },
    { "Gate", -90.0f, -40.0f, 1.0f, -70.0f, K_GATE, 0, 0, 0, 0, "%.0f dB" },
    { "Glide", 0.0f, 500.0f, 5.0f, 0.0f, K_GLIDE, 0, 0, 0, 0, "%.0f ms" },
    { "Cutoff", 0.0f, 100.0f, 1.0f, 40.0f, K_CUTOFF, 0, 0, 0, 0, "%.0f%%" },
    { "Resonance", 0.0f, 100.0f, 1.0f, 30.0f, K_RESONANCE, 0, 0, 0, 0, "%.0f%%" },
    { "Env Amt", 0.0f, 100.0f, 1.0f, 50.0f, K_ENV, 0, 0, 0, 0, "%.0f%%" },
    { "Attack", 0.0f, 1000.0f, 5.0f, 0.0f, K_ATTACK, 0, 0, 0, 0, "%.0f ms" },
    { "Decay", 10.0f, 2000.0f, 10.0f, 300.0f, K_DECAY, 0, 0, 0, 0, "%.0f ms" },
    { "Mode", 0.0f, 3.0f, 1.0f, 0.0f, K_MODE, 0, 0, 0, "Follow\0Follow Snap\0Hold\0Hold Snap\0", 0 },
    { "Mix", 0.0f, 100.0f, 1.0f, 100.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -20.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
};

#define SUB        16                           /* frames per control step (pitch, cutoff, envelopes' targets) */
#define GATE_DEF   -70.0f                       /* Gate's default, dBFS RMS: a note from silence needs this (a
                                                   decaying tail at -74 started random high notes); a note
                                                   playing goes on above half of the level HOLD_DB under it (the
                                                   pedal's input noise is at -90) */
#define HOLD_DB    8.0f
#define LEGATO_DB  -70.0f
#define CLEAR      0.7f                         /* clarity a note from silence needs */
#define KEEP_CLEAR 0.5f                         /* while a note plays: a pitch (it holds the note, and votes) */
#define END_BLOCKS 17                           /* ~25 ms without a pitch and quiet: the note ends */
#define LOST_BLOCKS 690                         /* 1 s without a pitch: the note ends however loud */
#define SAME_ST    0.6f                         /* within this the pitch follows (bends) */
#define AGREE_ST   0.5f
#define LEGATO_VOTES 8                         /* a note change without an attack: net votes (12 ms); four
                                                   times that for an octave (a string an octave down ringing
                                                   in sympathy) */
#define SURE_CLEAR 0.9f                         /* an estimate this clear is four votes */
#define PITCH_FOLLOW 0.2f                       /* the note's pitch moves this much of the way per estimate */
#define ATTACK_BLOCKS 103                      /* 150 ms after an attack a new note needs less: */
#define ATTACK_CLEAR 0.5f                       /* this clarity, and estimates of the old note still ringing do
                                                   not reset its count */
#define FRESH_AGREE 2                           /* agreeing estimates a change after an attack needs */
#define FRESH_OCT  2                            /* times that for a jump of an octave or more */
#define FALL_RATIO 0.7f                         /* no change with the level this much under its mean (-3 dB) */
#define LV_MEAN_C  0.93f                        /* that mean: one-pole per block, ~20 ms */
#define ATTACK_RISE 1.3f                        /* and the level this much over its 30 ms mean */
#define DIP_STEPS  8                            /* a re-attack's fall, in control steps (2.9 ms) */
#define DC_R       0.99929f                     /* the DC blocker's pole: 5 Hz */
#define SYNTH_GAIN 0.5f                         /* a saw at the guitar's peak level sounds this much louder */

#define FORMANTS   3
/* Voice: an "aah" (F1 800 Hz, F2 1150 Hz, F3 2900 Hz) by three band passes on a saw */
static const float formant_hz[FORMANTS] = { 800.0f, 1150.0f, 2900.0f };
static const float formant_q[FORMANTS] = { 8.0f, 9.0f, 12.0f };
static const float formant_gain[FORMANTS] = { 1.0f, 0.63f, 0.25f };

/* one oscillator: its phase, and what some waves keep (Supersaw's side saws, Sync's slave, Voice's formants) */
struct ostate {
    float ph;
    float sp[4];
    float slave, jump;
    float fz[FORMANTS][2];
};

struct state {
    struct trk trk;
    /* note */
    float note;                                 /* semitones (69 = A 440), 0 = none */
    float cand;                                 /* a candidate for the next note */
    int cand_n, quiet_n;
    int trig;                                   /* a new note or attack since the last control step */
    /* voice */
    float pitch;                                /* the glided pitch, semitones */
    struct ostate o1, o2;                       /* the two oscillators */
    float pwm_ph;                               /* PWM's LFO phase 0..1 */
    float dcx, dcy;                             /* the oscillators' DC blocker (Sync's restarts leave one) */
    float fa[FORMANTS][3];                      /* Voice: the formant band passes' a1, a2, a3 */
    float start_floor, floor;                   /* from Gate: a note from silence needs start_floor, a note
                                                   playing goes on down to half of floor (RMS) */
    float lv_mean;                              /* the tracker's level over ~20 ms */
    float legato_floor;                         /* a change without an attack needs this: the Gate or
                                                   LEGATO_DB, the lower (a high Gate is for new notes; it
                                                   blocked soft pull-offs at -65..-77) */
    float lad[4];                               /* ladder stages */
    float G, inv;                               /* the last control step's ladder coefficients */
    /* envelopes */
    float fast, slow;                           /* the guitar's peak follower and its 30 ms mean */
    float smooth;                               /* the follower smoothed: the level in Follow */
    float hprev, hfast, hslow;                  /* the attacks: the input's first difference (a pick's attack is
                                                   bright, the strings left ringing mostly are not), its peak
                                                   follower and that one's slower mean */
    int refractory;                             /* blocks before another attack may count */
    int since_attack;                           /* blocks since the last attack */
    float hold;                                 /* Hold: the note's peak */
    float gate;                                 /* 0..1: the note's gate, with Attack and the release */
    int dip;                                    /* control steps left of a re-attack's fall */
    float fenv;                                 /* the filter's envelope 0..1 */
    float wet, dry;                             /* the output gains where the last block ended */
};

static inline float semis_of(float hz)
{
    return 69.0f + 12.0f * wvfx_log2(hz * (1.0f / 440.0f));
}

static inline float hz_of(float st)
{
    return 440.0f * modfx_exp2((st - 69.0f) * (1.0f / 12.0f));
}

static inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

/* a jump of whole octaves (or an octave and a fifth): what a wrong period looks like */
static inline int octave_jump(float d)
{
    d = absf(d);
    return absf(d - 12.0f) < AGREE_ST || absf(d - 19.0f) < AGREE_ST || absf(d - 24.0f) < AGREE_ST;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    (void)knobs;
    for (unsigned i = 0; i < sizeof *s; i++)
        ((uint8_t *)s)[i] = 0;
    trk_init(&s->trk);
    s->dry = 1.0f;
    s->inv = 1.0f;
    s->since_attack = ATTACK_BLOCKS;
    static const float spread[8] = { 0.37f, 0.71f, 0.13f, 0.55f, 0.89f, 0.24f, 0.62f, 0.05f };
    for (int i = 0; i < 4; i++) {               /* Supersaw: the side saws start apart (no phasing at first) */
        s->o1.sp[i] = spread[i];
        s->o2.sp[i] = spread[4 + i];
    }
    for (int i = 0; i < FORMANTS; i++) {        /* Voice: TPT band pass coefficients */
        const float g = modfx_prewarp(formant_hz[i], MODFX_FS), kq = 1.0f / formant_q[i];
        s->fa[i][0] = 1.0f / (1.0f + g * (g + kq));
        s->fa[i][1] = g * s->fa[i][0];
        s->fa[i][2] = g * s->fa[i][1];
    }
    return 0;
}

/* the tracker's estimate into the note */
static void follow(struct state *s)
{
    const struct trk *t = &s->trk;
    const float e = t->hz > 0.0f ? semis_of(t->hz) : 0.0f;
    const int fresh = s->since_attack < ATTACK_BLOCKS;      /* just after an attack */
    const int playing = s->note > 0.0f;
    /* the note playing holds on less clarity than a new note needs (a ringing string's clarity drifts down);
       each such estimate is a vote against the candidate. Its pitch follows (bends, vibrato) only the clear
       estimates, over ~6 ms: other strings ringing pull an unclear one by tens of cents */
    if (playing && e > 0.0f && t->clarity >= KEEP_CLEAR && t->level >= 0.5f * s->floor && absf(e - s->note) < SAME_ST) {
        /* a clear estimate moves it fully, a middling one (a slide's, a decaying string's) less: (c - 0.5) / 0.2
           squared, so the warble of strings ringing together (0.5..0.6) barely moves it */
        float w = (t->clarity - KEEP_CLEAR) * (1.0f / (CLEAR - KEEP_CLEAR));
        w = w > 1.0f ? 1.0f : w * w;
        s->note += PITCH_FOLLOW * w * (e - s->note);
        s->quiet_n = 0;
        if (s->cand_n > 0)
            s->cand_n--;
        return;
    }
    const float need_clear = playing ? KEEP_CLEAR : fresh ? ATTACK_CLEAR : CLEAR;
    if (!(e > 0.0f && t->clarity >= need_clear && t->level >= (playing ? 0.5f * s->floor : s->start_floor))) {
        /* no clear pitch: the note goes on while the guitar sounds (two strings ringing, a pick's attack), on
           its last pitch; it ends when the guitar is quiet, or after LOST_BLOCKS without a pitch */
        if (playing) {
            s->quiet_n++;
            if ((t->level < 0.5f * s->floor && s->quiet_n >= END_BLOCKS) || s->quiet_n >= LOST_BLOCKS)
                s->note = 0.0f;
        } else {
            s->cand_n = 0;
        }
        return;
    }
    s->quiet_n = 0;
    /* under the legato floor (a tail, a touched string's rest, a string being stopped before the next pick) the
       note does not change at all, and such a pitch gathers no votes either: they would stand in the way of the
       next note picked */
    if (playing && t->level < s->legato_floor)
        return;
    /* nor while the level is falling fast (FALL_RATIO under its ~20 ms mean): a string being stopped before the
       next pick, whose harmonics read an octave or two up, very clear. Hammer-ons and pull-offs excite the
       string again: the level holds or rises */
    if (playing && t->level < FALL_RATIO * s->lv_mean)
        return;
    /* votes for a candidate: its own estimates add one, four when very clear (a clean legato note changes as
       fast as a picked one; strings ringing together are never that clear), none under CLEAR outside an
       attack's window (a pitch that holds a note, not one that starts another: the end of a slide gets there by
       following); while a note plays, another pitch takes one away */
    const int vote = t->clarity >= SURE_CLEAR ? 4
                     : (t->clarity >= CLEAR || (fresh && t->clarity >= ATTACK_CLEAR)) ? 1 : 0;
    if (s->cand_n > 0 && absf(e - s->cand) < AGREE_ST) {
        s->cand_n += vote;
        s->cand = e;
    } else if (s->cand_n > 0 && playing) {
        s->cand_n--;
        return;
    } else {
        s->cand = e;
        s->cand_n = vote;
    }
    if (!vote)
        return;
    /* from silence two in a row; just after an attack two (four for an octave: a wrong period's jump); else,
       a note change without an attack (legato, or two strings ringing) needs LEGATO_VOTES */
    /* a jump of an octave or more without an attack (a wrong period, a string ringing in sympathy, a harmonic
       from a finger brushing a string) needs four times the votes */
    const int oct = playing && (octave_jump(e - s->note) || absf(e - s->note) > 12.5f);
    const int need = !playing ? 2 * vote
                     : fresh  ? (oct ? FRESH_OCT : 1) * FRESH_AGREE * vote
                              : (oct ? 4 * LEGATO_VOTES : LEGATO_VOTES);
    if (s->cand_n >= need) {
        if (!playing)
            s->pitch = e;                       /* from silence: no glide */
        s->note = e;
        s->cand_n = 0;
        /* a note picked (or from silence) starts again: the filter's envelope and, in Hold, the level held. A
           change without an attack (a slide, legato) keeps both: the string has decayed by then, and taking its
           level again would drop the synth by 15-20 dB at the end of every slide */
        if (!playing || fresh)
            s->trig = 1;
        if (fresh)
            s->since_attack = ATTACK_BLOCKS;    /* one new note per attack */
    }
}
/* PolyBLEP: the step's correction near a discontinuity at phase 0 */
static inline float blep(float p, float dt)
{
    if (p < dt) {
        p /= dt;
        return p + p - p * p - 1.0f;
    }
    if (p > 1.0f - dt) {
        p = (p - 1.0f) / dt;
        return p * p + p + p + 1.0f;
    }
    return 0.0f;
}

static inline float wrap1(float p)
{
    return p >= 1.0f ? p - 1.0f : p;
}

/* the waves (the Wave knob's order; a new one goes at the end, so presets keep theirs) */
enum { W_SAW, W_SQUARE, W_PULSE, W_TRI, W_SINE, W_THIN, W_PWM, W_SAWSQ, W_SUPER, W_SYNC, W_ORGAN, W_VOICE, WAVES };

/* what a control step sets for both oscillators */
struct wctl {
    float pw;                                   /* PWM's width */
    float ratio;                                /* Sync: the slave's frequency over the master's */
};

static inline float saw_at(float p, float dt)
{
    return 2.0f * p - 1.0f - blep(p, dt);
}

/* a pulse of width w, without its DC (2 w - 1): a pulse's mean would thump under the envelopes */
static inline float pulse_at(float p, float dt, float w)
{
    float q = p - w;
    if (q < 0.0f)
        q += 1.0f;
    return (p < w ? 1.0f : -1.0f) + blep(p, dt) - blep(q, dt) - (2.0f * w - 1.0f);
}

/* Supersaw: four more saws around the main one, +-6 and +-16 cents (their phases spread at init) */
static const float super_dt[4] = { -0.00920f, -0.00346f, 0.00347f, 0.00929f };
#define SUPER_SIDE 0.6f                         /* the side saws' level */
#define SUPER_NORM 0.55f                        /* 1 / sqrt(1 + 4 x 0.6^2): the sum's RMS back to one saw's */

/* Organ: drawbars 8', 4', 2 2/3', 2', 1 1/3' (harmonics 1, 2, 3, 4, 6), each left out over 0.45 fs */
static const float organ_amp[5] = { 1.0f, 0.7f, 0.5f, 0.45f, 0.3f };
#define ORGAN_NORM 0.6f                         /* the drawbars' RMS (1.0) to a saw's (0.58) */

#define VOICE_NORM 0.8f

/* one sample of oscillator o (wave, phase step dt), its phase(s) moved on */
static inline float gen(struct ostate *o, const struct state *s, int wave, float dt, const struct wctl *c)
{
    const float p = o->ph;
    float v, sn, cs;
    switch (wave) {
    case W_SAW:
        v = saw_at(p, dt);
        break;
    case W_SQUARE:
        v = pulse_at(p, dt, 0.5f);
        break;
    case W_PULSE:
        v = pulse_at(p, dt, 0.25f);
        break;
    case W_TRI: {
        float t = 4.0f * p - 1.0f;
        v = t > 1.0f ? 2.0f - t : t;
        break;
    }
    case W_SINE:
        modfx_sincos(p, &v, &cs);
        break;
    case W_THIN:
        v = pulse_at(p, dt, 0.125f);
        break;
    case W_PWM:
        v = pulse_at(p, dt, c->pw);
        break;
    case W_SAWSQ:
        v = 0.5f * (saw_at(p, dt) - pulse_at(p, dt, 0.5f));   /* the square inverted: in phase with the saw */
        break;
    case W_SUPER: {
        v = saw_at(p, dt);
        for (int i = 0; i < 4; i++) {
            const float d = dt * (1.0f + super_dt[i]);
            v += SUPER_SIDE * saw_at(o->sp[i], d);
            o->sp[i] = wrap1(o->sp[i] + d);
        }
        v *= SUPER_NORM;
        break;
    }
    case W_SYNC: {
        /* a slave saw at ratio times the note, restarted with each period of the master (the note): the
           jump at the restart is smoothed after it like a saw's (o->jump, set when the master wrapped) */
        const float ds = dt * c->ratio;
        v = 2.0f * o->slave - 1.0f - blep(o->slave, ds < 0.45f ? ds : 0.45f) + 0.5f * o->jump * blep(p, dt);
        o->slave += ds;
        while (o->slave >= 1.0f)
            o->slave -= 1.0f;
        if (p + dt >= 1.0f) {                   /* the master wraps in this sample */
            const float frac = (p + dt - 1.0f) / dt, before = o->slave;
            o->slave = frac * ds;
            o->jump = 2.0f * (o->slave - before);
        } else if (p >= dt) {
            o->jump = 0.0f;
        }
        break;
    }
    case W_ORGAN: {
        modfx_sincos(p, &sn, &cs);
        const float s2 = 2.0f * sn * cs, c2 = 1.0f - 2.0f * sn * sn;
        const float s3 = s2 * cs + c2 * sn, c3 = c2 * cs - s2 * sn;
        const float s4 = 2.0f * s2 * c2, s6 = 2.0f * s3 * c3;
        v = organ_amp[0] * sn;
        if (dt < 0.225f)
            v += organ_amp[1] * s2;
        if (dt < 0.15f)
            v += organ_amp[2] * s3;
        if (dt < 0.1125f)
            v += organ_amp[3] * s4;
        if (dt < 0.075f)
            v += organ_amp[4] * s6;
        v *= ORGAN_NORM;
        break;
    }
    default: {                                  /* W_VOICE: TPT state-variable band passes */
        const float x = saw_at(p, dt);
        v = 0.0f;
        for (int i = 0; i < FORMANTS; i++) {
            const float *a = s->fa[i];
            float *z = o->fz[i];
            const float v3 = x - z[1], v1 = a[0] * z[0] + a[1] * v3, v2 = z[1] + a[1] * z[0] + a[2] * v3;
            z[0] = 2.0f * v1 - z[0];
            z[1] = 2.0f * v2 - z[1];
            v += formant_gain[i] * v1;
        }
        v *= VOICE_NORM;
        break;
    }
    }
    o->ph = wrap1(p + dt);
    return v;
}
/* tan (pi f / fs) for the ladder, f clamped below fs / 2 */
static inline float ladder_g(float hz)
{
    return modfx_prewarp(wvfx_clamp(hz, 20.0f, 18000.0f), MODFX_FS);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    const int n = io->frames;
    float *x = io->io;

    const float *in = io->aux ? io->aux : io->io;
    /* the Gate (a value above its range is a preset from before it, whose word held Sub's level: the default) */
    float gate_db = k[K_GATE] > -40.0f ? GATE_DEF : wvfx_clamp(k[K_GATE], -90.0f, -40.0f);
    s->start_floor = wvfx_db(gate_db);
    s->floor = wvfx_db(gate_db - HOLD_DB);
    s->legato_floor = wvfx_db(gate_db < LEGATO_DB ? gate_db : LEGATO_DB);
    trk_feed(&s->trk, in, n);
    trk_estimate(&s->trk);
    s->lv_mean = s->trk.level + (s->lv_mean - s->trk.level) * LV_MEAN_C;

    /* the guitar's envelope (per frame, from the same input): a peak follower (150 ms release), smoothed over
       4 ms so a low note's period does not ripple through. The attacks: the first difference's peak (30 ms
       release) three times its 50 ms mean, and the level rising (ATTACK_RISE times its 30 ms mean: a finger
       sliding on the frets is bright too, but the level does not rise) */
    const float rel = 0.99984884f, sm_c = 0.99434643f;
    const float hrel = 0.99924443f, hslow_c = 0.99954657f, slow_c = 0.99924443f;
    float env[WVFX_MAX_FRAMES];
    float fast = s->fast, slow = s->slow, sm = s->smooth, hprev = s->hprev, hfast = s->hfast, hslow = s->hslow;
    for (int f = 0; f < n; f++) {
        const float m = 0.5f * (in[2 * f] + in[2 * f + 1]);
        const float v = absf(m), h = absf(m - hprev);
        hprev = m;
        fast = v > fast ? v : fast * rel;
        sm = fast + (sm - fast) * sm_c;
        slow = fast + (slow - fast) * slow_c;
        hfast = h > hfast ? h : hfast * hrel;
        hslow = hfast + (hslow - hfast) * hslow_c;
        env[f] = sm;
    }
    s->fast = fast;
    s->slow = slow;
    s->smooth = sm;
    s->hprev = hprev;
    s->hfast = hfast;
    s->hslow = hslow;
    if (s->since_attack < ATTACK_BLOCKS)
        s->since_attack++;
    if (s->refractory)
        s->refractory--;
    else if (hfast > 3.0f * hslow && fast > ATTACK_RISE * slow && fast > 4.0f * s->floor) {
        s->since_attack = 0;                    /* a string picked: a new note comes easier for a while, and the */
        s->cand_n = 0;                          /* votes start again (an old candidate's would hold it back) */
        s->refractory = 35;                     /* 50 ms */
        if (s->note > 0.0f)
            s->trig = 1;                        /* the filter's envelope again (the same note picked again) */
    }
    follow(s);

    const int mode = modfx_option(k[K_MODE], 4), hold_mode = mode >= 2, snap = mode & 1;
    const int wave = modfx_option(k[K_WAVE], WAVES);
    const float oct = 12.0f * (float)(int)wvfx_clamp(k[K_OCTAVE] + (k[K_OCTAVE] >= 0.0f ? 0.5f : -0.5f), -2.0f, 2.0f);
    const float l2 = wvfx_clamp(k[K_OSC2], 0.0f, 100.0f) * 0.01f;
    const float iv = wvfx_clamp(k[K_INTERVAL], -24.0f, 24.0f) + wvfx_clamp(k[K_DETUNE], 0.0f, 50.0f) * 0.01f;
    const float norm = 1.0f / (1.0f + l2);
    const float glide_ms = wvfx_clamp(k[K_GLIDE], 0.0f, 500.0f);
    const float glide = glide_ms > 0.0f ? wvfx_coef(glide_ms, MODFX_FS / SUB) : 0.0f;
    const float cut = wvfx_clamp(k[K_CUTOFF], 0.0f, 100.0f) * 0.07f;          /* octaves above the note */
    const float env_oct = wvfx_clamp(k[K_ENV], 0.0f, 100.0f) * 0.06f;
    const float res = wvfx_clamp(k[K_RESONANCE], 0.0f, 100.0f) * 0.039f;      /* ladder feedback 0..3.9 */
    const float att_ms = wvfx_clamp(k[K_ATTACK], 0.0f, 1000.0f);
    const float att = att_ms > 0.0f ? wvfx_coef(att_ms, MODFX_FS / SUB) : 0.0f;
    const float dec = wvfx_coef(wvfx_clamp(k[K_DECAY], 10.0f, 2000.0f), MODFX_FS / SUB);
    const float stop = wvfx_coef(30.0f, MODFX_FS / SUB);                       /* Follow: the gate closes */
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f;
    const float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -20.0f, 20.0f));
    const float t_wet = io->enabled ? (mix < 0.5f ? 2.0f * mix : 1.0f) * level * SYNTH_GAIN : 0.0f;
    const float t_dry = io->enabled ? (mix > 0.5f ? 2.0f * (1.0f - mix) : 1.0f) : 1.0f;

    if (s->trig) {
        s->fenv = 1.0f;
        if (hold_mode)
            s->hold = fast;
        /* a note picked while one sounds is attacked again, like a new stroke of a bow: the gate falls in ~3 ms
           and rises again with Attack (none at Attack 0). Legato and slides (no pick) stay under one stroke */
        if (att_ms > 0.0f && s->gate > 0.05f)
            s->dip = DIP_STEPS;
        s->trig = 0;
    }
    if (hold_mode && s->note > 0.0f && fast > s->hold)
        s->hold = fast;

    if (!io->enabled && s->wet == 0.0f) {
        s->dry = 1.0f;                          /* bypassed: the signal as it is */
        s->gate = 0.0f;
        return;
    }

    const float dw = wvfx_ramp_step(s->wet, t_wet, n), dd = wvfx_ramp_step(s->dry, t_dry, n);
    float wet = s->wet, dry = s->dry;
    float y0 = s->lad[0], y1 = s->lad[1], y2 = s->lad[2], y3 = s->lad[3];
    for (int c0 = 0; c0 < n; c0 += SUB) {
        const int m = n - c0 < SUB ? n - c0 : SUB;
        /* control step: pitch, envelopes, cutoff */
        if (s->note > 0.0f) {
            float target = s->note;
            if (snap)
                target = (float)(int)(target + 0.5f);
            s->pitch = target + (s->pitch - target) * glide;
        }
        const int on = s->note > 0.0f;
        const float g_from = s->gate;
        if (s->dip) {
            s->gate *= 0.5f;                    /* 8 steps: -48 dB in 2.9 ms */
            s->dip--;
        } else if (on) {
            s->gate = 1.0f + (s->gate - 1.0f) * att;
        } else {
            s->gate *= hold_mode ? dec : stop;
        }
        s->fenv *= dec;
        /* the gate moves linearly over the step's frames (a re-attack's fall would click in steps) */
        float amp = g_from * norm;
        const float damp = (s->gate - g_from) * norm / (float)m, held = s->hold;
        const float p = s->pitch + oct;
        const float f1 = wvfx_clamp(hz_of(p), 1.0f, 18000.0f), dt1 = f1 * (1.0f / MODFX_FS);
        const float dt2 = wvfx_clamp(hz_of(p + iv), 1.0f, 18000.0f) * (1.0f / MODFX_FS);
        const float g1 = ladder_g(f1 * modfx_exp2(cut + env_oct * s->fenv)), G1 = g1 / (1.0f + g1);
        const float inv1 = 1.0f / (1.0f + res * G1 * G1 * G1 * G1);
        const float rm = 1.0f / (float)m, Gs = (G1 - s->G) * rm, is = (inv1 - s->inv) * rm;
        float G = s->G, inv = s->inv;
        /* PWM: the width swings 0.15..0.85 at 0.8 Hz; Sync: the slave 2 times the note, up to 2^(1 + Env Amt's
           octaves / 2) with the filter's envelope (the classic sync sweep) */
        struct wctl wc;
        s->pwm_ph = wrap1(s->pwm_ph + 0.8f * SUB / MODFX_FS);
        modfx_sincos(s->pwm_ph, &wc.pw, &wc.ratio);
        wc.pw = 0.5f + 0.35f * wc.pw;
        wc.ratio = modfx_exp2(1.0f + 0.5f * env_oct * s->fenv);
        for (int f = c0; f < c0 + m; f++) {
            float v = gen(&s->o1, s, wave, dt1, &wc);
            if (l2 > 0.0f)
                v += l2 * gen(&s->o2, s, wave, dt2, &wc);
            const float hp = v - s->dcx + DC_R * s->dcy;      /* 5 Hz one-pole high pass */
            s->dcx = v;
            s->dcy = hp;
            v = hp;
            amp += damp;
            v *= amp * (hold_mode ? held : env[f]);
            /* the ladder: y3 = G^4 u + S with u = x - k y3, solved for y3; then the stages */
            G += Gs;
            inv += is;
            const float G2 = G * G;
            const float S = (1.0f - G) * (G2 * G * y0 + G2 * y1 + G * y2 + y3);
            const float in4 = v * (1.0f + 0.5f * res);
            float u = (in4 - res * S) * inv;
            u = u / (1.0f + absf(u) * 0.3f);    /* soft clip: the ladder's input transistor pair, roughly */
            float a = G * (u - y0);
            const float o0 = a + y0;
            y0 = o0 + a;
            a = G * (o0 - y1);
            const float o1 = a + y1;
            y1 = o1 + a;
            a = G * (o1 - y2);
            const float o2 = a + y2;
            y2 = o2 + a;
            a = G * (o2 - y3);
            const float o3 = a + y3;
            y3 = o3 + a;
            wet += dw;
            dry += dd;
            x[2 * f] = x[2 * f] * dry + o3 * wet;
            x[2 * f + 1] = x[2 * f + 1] * dry + o3 * wet;
        }
        s->G = G1;
        s->inv = inv1;
    }
    s->lad[0] = y0;
    s->lad[1] = y1;
    s->lad[2] = y2;
    s->lad[3] = y3;
    s->wet = t_wet;
    s->dry = t_dry;
}

const struct wvfx_desc synth_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 1),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Synth",
    .category = "Synth",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
