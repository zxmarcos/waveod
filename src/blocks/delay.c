/*
 * Delay (DLY › Delay, waveod's): a light multi-type digital / bucket-brigade delay, docs/delay.md.
 *
 * Per 16 frames (the control rate; the shortest loop, 1 ms, is longer):
 *   input (faded in / out per frame: bypassed, the input leaves the line and the echoes ring out) -> the type's
 *   input shelf -> the modulation (LFO 1 = Mod Rate / Depth, LFO 2 on two types, x 2.5 or 7.5 ms) -> per channel
 *   the delay Time - 1/3000 s (the right one by the Mode: x Ratio, ping-pong, dual), smoothed per frame (~23 ms),
 *   on two heads: a jump of more than 10 ms starts the other head at the new time and crossfades to it (~23 ms),
 *   each head read with a 4-point cubic only while it is heard
 *   -> the soft clip (x Drive, a rational curve around 0.05, / Drive) -> the diffuser (4 Schroeder all-passes per
 *   channel, fixed or modulated) -> Low Cut / High Cut (first order, or 1..4 biquads by the type's slope) -> the
 *   compander's expander -> the feedback matrix by Mode (mono, stereo, ping-pong, dual, rotating; smoothed) + the
 *   input -> clip +-8 -> the compressor -> bit reduction -> the lines
 *   -> output shelf -> the sweep filter (a state-variable band-pass swept by an LFO) -> LFO 3 on the level or the
 *   pan -> the pan matrix -> polarity -> the ducker (the echoes duck under the playing) -> the mix law (the wet
 *   reaches full at 50 %, the dry falls from 50 %), level, ramped over one block.
 * In Mono the right line never gets a sample, so it is not run. Bypassed and rung out (no echo above -140 dB for
 * 0.1 s), the output is the input bit for bit and nothing runs but the LFOs.
 *
 *   Type (24)  Time 1..4000 ms (Sync)  Feedback %  Mix %  Level dB  Low Cut (20 = the type's)  High Cut (20000 = the
 *   type's)  Mod Rate Hz  Mod Depth %  Sync
 */
#include "modfx/modfx.h"

#define N      16
#define LINE   (4u * 44100u + 512u)     /* 4 s and the read's margin */
#define STRIDE (LINE + 4u)
#define TMAX   ((float)(LINE - 64u) / MODFX_FS)
#define LAT    23.0f                    /* frames added to every read (the line's block is written after it) */
#define DLEN   8192u                    /* diffuser lines */
#define DMASK  (DLEN - 1u)

enum { K_TYPE, K_TIME, K_FB, K_MIX, K_LEVEL, K_LOWCUT, K_HICUT, K_MODRATE, K_MODDEPTH, K_SYNC, NKNOBS };
enum { M_MONO, M_STEREO, M_PINGPONG, M_DUAL, M_SWEEP = 5, M_ROTATE = 7 };
enum { T_NONE, T_LEVEL, T_PAN, T_SPREAD };

#define TYPES \
    "Digital Mono\0" "Digital Stereo\0" "Analog Mono\0" "Analog Stereo\0" "Tape Wide\0" "Ping-Pong\0" "Dual\0" \
    "Sweep\0" "Ducking\0" "Early Digital\0" "Rack Digital\0" "Ambient\0" "Bucket Deluxe\0" "BBD Mono\0" \
    "BBD Stereo\0" "Bucket Stereo\0" "Dark Copy\0" "Compact BBD\0" "Diffused\0" "Breeze\0" "Panning\0" \
    "Trem Echo\0" "Wander\0" "Rotating\0"

static const struct wvfx_param params[] = {
    { "Type", 0.0f, 23.0f, 1.0f, 0.0f, K_TYPE, 0, 0, 0, TYPES, 0 },
    { "Time", 1.0f, 4000.0f, 1.0f, 400.0f, K_TIME, 0, 0x10 | K_SYNC, 0, 0, "%.0f ms" },
    { "Feedback", -100.0f, 100.0f, 1.0f, 30.0f, K_FB, 0, 0, 0, 0, "%.0f%%" },
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Low Cut", 20.0f, 2000.0f, 1.0f, 20.0f, K_LOWCUT, 0, 0, 0, 0, "%.0f Hz" },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 20000.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Mod Rate", 0.1f, 10.0f, 0.01f, 0.5f, K_MODRATE, 0, 0, 0, 0, "%.2f Hz" },
    { "Mod Depth", 0.0f, 100.0f, 1.0f, 0.0f, K_MODDEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
};

/* an LFO of a type: form 0 sine, 1 triangle, 6 "log"; rate Hz, amplitude, the right channel's phase (rad) */
struct lfo_def { uint8_t form; float rate, amp, ofs; };
/* a first-order section at 44.1 kHz: y = b0 x + b1 x[n-1] + a1 y[n-1] */
struct shelf { float b0, b1, a1; };

/*
 * The types. mode, ratio (the right time), fbr (the right feedback / the left), fbsum (0: the input summed into
 * the left line, mono style), pan (Dual / Rotate: L, R), rot (Rotate: x pi/2); drive; lowcut / hicut (Hz) and
 * their slopes (0 first order, n biquads) with the biquads' Q; depth (mod x 2.5 ms, 1: x 7.5 ms); lfo2; lfo3 and
 * its target; bits (0 off); comp (the compander's time, 0 off); duck (dB, threshold, release); diffuser (mix,
 * modulation, rate, delays at 44.1 kHz); flip (the right inverted); shelves in / out; echo pan.
 */
struct type {
    uint8_t mode, fbsum, lc_slope, hc_slope, depth, lfo3_tgt, bits, flip, shelf_in, shelf_out;
    float ratio, fbr, pan_l, pan_r, rot, drive, lowcut, hicut, lc_q[4], hc_q[4];
    struct lfo_def lfo2, lfo3;
    float comp, duck, duck_thr, duck_rel, dif_mix, dif_mod, dif_rate;
    uint16_t dif_d[4];
    float echo_pan;
    uint8_t post;                       /* a de-emphasis after the cuts (posts[]) */
    uint8_t mod_quad;                   /* the Mod LFO's right channel a quarter turn ahead */
};

#define Q1  { 0.707f, 1.0f, 1.0f, 1.0f }
#define QB4 { 0.707f, 0.5176f, 0.7071f, 1.932f }
#define QB2 { 0.707f, 0.7071f, 1.0f, 1.0f }
#define NOL { 0, 0.0f, 0.0f, 0.0f }
#define L3P { 0, 1.0f, 1.0f, 3.14159274f }      /* LFO 3 when no target uses it */
#define DIF0 0.0f, 0.0f, 0.0f, { 0, 0, 0, 0 }
#define DIFA 0.200003f, 0.0f, 0.1f, { 1091, 1791, 2882, 4674 }

static const struct type types[] = {
    /* mode fbsum lc hc dep tgt bits flip in out | ratio fbr panL panR rot drive lowcut hicut lcq hcq | lfo2 lfo3 | comp duck thr rel | diffuser | echo pan, post, mod quad */
    { M_MONO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 20.0f, 20000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0 },       /* Digital Mono */
    { M_STEREO, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 20.0f, 20000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0, 0, 1 },     /* Digital Stereo */
    { M_MONO, 0, 0, 4, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.08f, 100.0f, 4000.0f, Q1, QB4, NOL, L3P, 0.01f, 0, 0, 0, DIF0, 0 },  /* Analog Mono */
    { M_STEREO, 1, 0, 4, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.08f, 100.0f, 4000.0f, Q1, QB4, NOL, L3P, 0.01f, 0, 0, 0, DIF0, 0, 0, 1 },/* Analog Stereo */
    { M_STEREO, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0.95f, 1.0f, 0, 0, 0, 1.5f, 50.0f, 5000.0f, Q1, Q1, { 6, 0.99998951f, 0.20000516f, 1.57079637f },
      L3P, 0, 0, 0, 0, DIF0, 0, 0, 1 },                                                                                              /* Tape Wide */
    { M_PINGPONG, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 50.0f, 10000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0 },   /* Ping-Pong */
    { M_DUAL, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, -1.0f, 1.0f, 0, 0.05f, 50.0f, 10000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0 },/* Dual */
    { M_SWEEP, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 20.0f, 20000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0,
      0.0500046f, 0.0f, 0.1f, { 1091, 1791, 2882, 4674 }, 0 },                                                                       /* Sweep */
    { M_STEREO, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 50.0f, 10000.0f, Q1, Q1, NOL, L3P, 0, 11.9999f, -39.9994f,
      0.0100004f, DIF0, 0 },                                                                                                         /* Ducking */
    { M_MONO, 0, 0, 0, 0, 0, 16, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 50.0f, 8000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0 },       /* Early Digital */
    { M_MONO, 0, 0, 0, 0, 0, 8, 1, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 20.0f, 16000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0, DIF0, 0 },       /* Rack Digital */
    { M_STEREO, 1, 0, 3, 0, 0, 0, 1, 0, 0, 0.99f, 0.25003f, 0, 0, 0, 0.075f, 120.0f, 4000.0f, Q1, { 0.707f, 0.5412f, 1.3065f, 1.0f },
      NOL, L3P, 0, 0, 0, 0, DIFA, 0, 0, 1 },                                                                                         /* Ambient */
    { M_MONO, 0, 1, 4, 1, 0, 0, 0, 1, 0, 1.0f, 1.0f, 0, 0, 0, 0.65f, 21.0f, 3600.0f, Q1, QB4, NOL, L3P, 0.01f, 0, 0, 0, DIF0, 0, 1 },/* Bucket Deluxe */
    { M_MONO, 0, 0, 2, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.65f, 165.0f, 2900.0f, Q1, QB2, NOL, L3P, 0.01f, 0, 0, 0, DIF0, 0 },  /* BBD Mono */
    { M_STEREO, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0.945f, 1.0333333f, 0, 0, 0, 0.25f, 100.0f, 2900.0f, Q1, QB2,
      { 0, 4.99989557f, 0.01879979f, 0.0f }, L3P, 0.01f, 0, 0, 0, DIF0, 0, 0, 1 },                                                         /* BBD Stereo */
    { M_STEREO, 1, 0, 4, 1, 0, 0, 0, 1, 0, 1.0f, 1.0f, 0, 0, 0, 0.65f, 21.0f, 3600.0f, Q1, QB4, NOL, L3P, 0.01f, 0, 0, 0, DIF0, 0, 1 },/* Bucket Stereo */
    { M_MONO, 0, 1, 4, 1, 0, 0, 0, 2, 0, 1.0f, 1.0f, 0, 0, 0, 0.65f, 20.0f, 2140.0f, { 1.2f, 1.0f, 1.0f, 1.0f },
      { 1.2f, 0.5176f, 0.7071f, 1.932f }, NOL, L3P, 0.0047001f, 0, 0, 0, DIF0, 0, 2 },                                                  /* Dark Copy */
    { M_MONO, 0, 1, 2, 1, 0, 0, 0, 3, 4, 1.0f, 1.0f, 0, 0, 0, 0.65f, 64.0f, 3470.0f, Q1, QB2, NOL, L3P, 0.0022f, 0, 0, 0, DIF0, 0 }, /* Compact BBD */
    { M_MONO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.05f, 20.0f, 20000.0f, Q1, Q1, NOL, L3P, 0, 0, 0, 0,
      0.243004f, 0.144658f, 0.43000418f, { 833, 1351, 2188, 3540 }, 0 },                                                             /* Diffused */
    { M_STEREO, 1, 0, 4, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.08f, 100.0f, 4000.0f, Q1, QB4, NOL, L3P, 0, 0, 0, 0,
      0.299005f, 0.187812f, 0.49999475f, { 787, 1285, 2055, 3323 }, 0, 0, 1 },                                                             /* Breeze */
    { M_MONO, 0, 0, 2, 1, T_PAN, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 0, 0.65f, 165.0f, 2900.0f, Q1, QB2, NOL, { 1, 1.0f, 1.0f, 3.14159274f },
      0.01f, 0, 0, 0, DIF0, 1.0f },                                                                                                  /* Panning */
    { M_STEREO, 1, 0, 4, 0, T_LEVEL, 0, 0, 0, 0, 0.975f, 1.0f, 0, 0, 0, 0.08f, 100.0f, 4000.0f, Q1, QB4, NOL,
      { 0, 5.49982214f, 0.61999875f, 0.0f }, 0.01f, 0, 0, 0, DIF0, 0, 0, 1 },                                                              /* Trem Echo */
    { M_DUAL, 1, 0, 0, 0, T_PAN, 0, 0, 0, 0, 1.0f, 1.0f, -1.0f, 1.0f, 0, 0.06f, 50.0f, 10000.0f, Q1, Q1, NOL,
      { 1, 0.25999212f, 1.0f, 1.57079637f }, 0, 0, 0, 0, DIF0, 0 },                                                                  /* Wander */
    { M_ROTATE, 1, 0, 0, 0, T_PAN, 0, 0, 0, 0, 1.0f, 1.0f, -1.0f, 1.0f, 0.329997f, 0.06f, 50.0f, 10000.0f, Q1, Q1, NOL,
      { 1, 0.25999212f, 0.0f, 1.57079637f }, 0, 0, 0, 0, DIF0, 0 },                                                                  /* Rotating */
};
#define NTYPES ((int)(sizeof types / sizeof types[0]))

/* the bucket-brigade types' first-order high shelves at 44.1 kHz (unity at DC): 1 +6 dB at 1.49 kHz,
   2 +10.5 dB at 1.32 kHz, 3 +15.1 dB at 980 Hz, 4 its inverse */
static const struct shelf shelves[5] = {
    { 1.0f, 0.0f, 0.0f },
    { 1.87472057f, -1.61286092f, 0.73814040f },
    { 3.02768779f, -2.73152494f, 0.70383710f },
    { 5.05803633f, -4.77026939f, 0.71223372f },
    { 0.19770525f, -0.14081234f, 0.94310713f },
};

/* the bucket-brigade types' de-emphasis after the cuts (one-poles at 44.1 kHz, unity at DC, -26 dB at the top) */
static const struct op posts[3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.189648286f, 0.109493524f, 0.700858295f },
    { 0.179046392f, 0.103372529f, 0.717581093f },
};


struct lfo { float ph, sm[2]; };

struct design {
    const struct type *t;
    int lowcut_on, hicut_on, nlc, nhc, comp, skip_r;
    float time_l, time_r;               /* s, before the heads' smoothing */
    float ll, rr, lr, rl;               /* the feedback matrix's targets */
    float mod_amp, lfo1_rate, lfo1_amp;
    struct bq lc[4], hc[4];
    struct op lc1, hc1;
    float comp_c, exp_c, duck_c;
    float wet, dry;                     /* the mix law with the level */
};

struct state {
    float *line[2], *dif[2][4];
    uint32_t w, dw;
    float knobs[NKNOBS];
    uint32_t beat;
    int designed, type, idle, quiet;
    struct design d;
    float in_g;                         /* the input's fade */
    float sh_in[2][2], sh_out[2][2];    /* the shelves' x[n-1], y[n-1] */
    struct lfo lfo1, lfo2, lfo3, lfo_sw, lfo_dif;
    int head;                           /* the head heard */
    float ht[2][2], hs[2][2];           /* per head, per channel: the time it goes to, the smoothed time (s) */
    float xf;                           /* the crossfade, 0 = head 0, 1 = head 1 */
    float fb[4];                        /* the smoothed matrix LL RR LR RL */
    float s_lc[4][2][2], s_hc[4][2][2], s_lc1[2], s_hc1[2], s_post[2];
    float ce[2], ee[2];                 /* the compander's envelopes: compressor, expander */
    float dsm[2][4];                    /* the modulated diffuser's smoothed delays */
    float duck_p, duck_e;
    float sw_pos[2], sw_s[2][2];        /* the sweep: smoothed position, the filter states */
    float gw, gd, gt;                   /* where the last block's gains ended: wet, dry, through */
};

/* ---------------------------------------------------------------- designs */

/* the low cut's biquads: the cookbook's high-pass with alpha = w / 2Q (not sin w / 2Q) */
static struct bq hp_w(float f, float q, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    (void)sn;
    const float w = MODFX_TWO_PI * f / fs, al = w / (2.0f * q), n = 1.0f / (1.0f + al);
    return (struct bq){ 0.5f * (1.0f + cs) * n, -(1.0f + cs) * n, 0.5f * (1.0f + cs) * n, 2.0f * cs * n, -(1.0f - al) * n };
}

static void design(struct state *s, const float *k, uint32_t beat_ms, float fs)
{
    struct design *d = &s->d;
    const struct type *t = &types[modfx_option(k[K_TYPE], NTYPES)];
    d->t = t;
    float ms = wvfx_clamp(k[K_TIME], 1.0f, 4000.0f);
    if (k[K_SYNC] >= 0.5f) {
        static const float beats[11] = { 4.0f, 2.0f, 3.0f, 4.0f / 3.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f };
        ms = (float)(beat_ms & 0xffffu) * beats[modfx_option(k[K_TIME], 11)];
    }
    const float tm = wvfx_clamp(ms * 0.001f, 0.001f, TMAX), tl = tm - 3.33333335e-4f;
    float tr = tl;
    if (t->mode == M_STEREO || t->mode == M_SWEEP)
        tr = tl * t->ratio;
    else if (t->mode == M_PINGPONG || t->mode == M_DUAL)
        tr = (t->ratio < 0.1f ? 0.1f : t->ratio) * tl;
    d->time_l = tl;
    d->time_r = tr;
    const float fbl = wvfx_clamp(k[K_FB], -100.0f, 100.0f) * 0.01f, fbr = fbl * t->fbr;
    d->ll = d->rr = d->lr = d->rl = 0.0f;
    switch (t->mode) {
    case M_MONO:
        d->ll = d->rr = fbl;
        break;
    case M_PINGPONG:
        d->lr = d->rl = fbl;
        break;
    case M_ROTATE: {
        float sn, cs;
        modfx_sincos(0.25f * t->rot, &sn, &cs);
        d->lr = fbl * sn;
        d->rl = -d->lr;
        d->ll = d->rr = fbl * cs;
        break;
    }
    default:
        d->ll = fbl;
        d->rr = fbr;
        break;
    }
    d->skip_r = t->mode == M_MONO;
    d->mod_amp = 0.0025f + 0.005f * (float)t->depth;
    d->lfo1_rate = wvfx_clamp(k[K_MODRATE], 0.1f, 10.0f);
    d->lfo1_amp = wvfx_clamp(k[K_MODDEPTH], 0.0f, 100.0f) * 0.01f / d->lfo1_rate;
    const float lc = k[K_LOWCUT] <= 20.5f ? t->lowcut : wvfx_clamp(k[K_LOWCUT], 20.0f, 2000.0f);
    const float hc = k[K_HICUT] >= 19999.5f ? t->hicut : wvfx_clamp(k[K_HICUT], 200.0f, 20000.0f);
    d->lowcut_on = lc > 20.1f && t->mode != M_SWEEP;
    d->hicut_on = hc < 19999.0f && t->mode != M_SWEEP;
    d->nlc = t->lc_slope;
    d->nhc = t->hc_slope;
    d->lc1 = op_hp(lc, fs);
    d->hc1 = op_lp(hc, fs);
    for (int i = 0; i < 4; i++) {
        d->lc[i] = hp_w(lc, t->lc_q[i], fs);
        d->hc[i] = bq_rbj(0, hc, t->hc_q[i], fs);
    }
    d->comp = t->comp > 0.0f;
    if (d->comp) {
        d->exp_c = modfx_exp2(-1.44269504f / (t->comp * fs));
        d->comp_c = modfx_exp2(-1.44269504f / (0.475f * t->comp * fs));
    }
    d->duck_c = t->duck_rel > 0.0f ? modfx_exp2(-1.44269504f / (t->duck_rel * fs)) : 0.0f;
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->wet = (mix >= 0.5f ? 1.0f : modfx_wet_curve(2.0f * mix)) * level;
    d->dry = (mix <= 0.5f ? 1.0f : 2.0f - 2.0f * mix) * level;
}

/* ---------------------------------------------------------------- LFOs, once per 16 frames */

static float lfo_shape(int form, float p)
{
    if (form == 0) {
        float sn, cs;
        modfx_sincos(p, &sn, &cs);
        return -cs;
    }
    const float tri = modfx_tri(p);
    if (form == 6) {
        float sn, cs;
        modfx_sincos(0.125f + 0.125f * tri, &sn, &cs);
        return (sn + sn) - 1.0f;
    }
    return tri;
}

static void lfo_run(struct lfo *l, int form, float rate, float amp, float ofs, float fs, float out[2])
{
    l->ph = modfx_advance(l->ph, (float)N * rate * MODFX_TWO_PI / fs);
    float q = ofs + l->ph;
    if (q >= MODFX_TWO_PI)
        q -= MODFX_TWO_PI;
    out[0] = amp * lfo_shape(form, l->ph * (1.0f / MODFX_TWO_PI));
    out[1] = ofs > 0.0f ? amp * lfo_shape(form, q * (1.0f / MODFX_TWO_PI)) : out[0];
}

/* ---------------------------------------------------------------- stages */

static inline float clip_rational(float x)            /* sign(v) (v^2 + a |v|) / (v^2 + 2 a |v| + 1), v = pi/2 x + 0.05 */
{
    const float v = 0.05f + 0x1.921fbp+0f * x, av = __builtin_fabsf(v), p = 0x1.3144aap-1f * av, q = av * av;
    const float y = (q + p) / (q + (1.0f + (p + p)));
    return __builtin_copysignf(y, v);
}

static void shelf_run(const struct shelf *f, float *st, float *x, int n)
{
    float x1 = st[0], y1 = st[1];
    for (int i = 0; i < n; i++) {
        const float y = f->b0 * x[i] + f->b1 * x1 + f->a1 * y1;
        x1 = x[i];
        y1 = y;
        x[i] = y;
    }
    st[0] = x1;
    st[1] = y1;
}

/* the diffuser of one channel over n frames: four Schroeder all-passes of delay d + 16 (fixed) or the smoothed
   modulated delay (linear read) */
static void diffuse(struct state *s, int ch, float *x, int n, const float m[4])
{
    const struct type *t = s->d.t;
    const float mix = t->dif_mix, k = 1.0f - mix;
    float cur[N];
    for (int i = 0; i < n; i++)
        cur[i] = x[i];
    for (int st = 0; st < 4; st++) {
        float *line = s->dif[ch][st];
        const uint32_t w = s->dw;
        float dl[N];
        if (t->dif_mod > 0.0f) {
            const float a = 0.01f * ((float)t->dif_d[st] * m[st]);
            float v = s->dsm[ch][st];
            for (int i = 0; i < n; i++) {
                v = a + 0.99f * v;
                const float pos = v + 16.0f;
                const int ip = (int)pos;
                const float f = pos - (float)ip;
                const uint32_t j = (w + (uint32_t)i - (uint32_t)ip) & DMASK;
                dl[i] = (1.0f - f) * line[j] + f * line[(j - 1u) & DMASK];
            }
            s->dsm[ch][st] = v;
        } else {
            const uint32_t dd = (uint32_t)t->dif_d[st] + 16u;
            for (int i = 0; i < n; i++)
                dl[i] = line[(w + (uint32_t)i - dd) & DMASK];
        }
        for (int i = 0; i < n; i++) {
            const float v = dl[i] * -0.707000017f + cur[i];
            line[(w + (uint32_t)i) & DMASK] = v;
            cur[i] = v * 0.707000017f + dl[i];
        }
    }
    for (int i = 0; i < n; i++)
        x[i] = cur[i] * mix + x[i] * k;
}

/* the sweep's state-variable filter design (band-pass) at f, Q q */
struct svf { float c0, c1, c2, c3; };
static struct svf svf_bp(float f, float q, float fs)
{
    const float g = 2.0f * modfx_prewarp(f, fs), d = g / q, g2 = g * g;
    const float c0 = (g2 + d) / (g2 * 0.25f + (1.0f + d * 0.5f)), c1 = g2 / (g2 + d), h = 1.0f - c1;
    return (struct svf){ c0, c1, q * (h * (0.5f * c0)), q * h };
}

/* ---------------------------------------------------------------- the block */

static int changed(struct state *s, const float *k, uint32_t beat)
{
    int c = !s->designed || (k[K_SYNC] >= 0.5f && beat != s->beat);
    for (int i = 0; i < NKNOBS; i++)
        if (k[i] != s->knobs[i]) {
            c = 1;
            s->knobs[i] = k[i];
        }
    s->beat = beat;
    s->designed = 1;
    return c;
}

static void clear(struct state *s)
{
    for (unsigned i = 0; i < 2 * STRIDE; i++)
        s->line[0][i] = 0.0f;
    for (unsigned i = 0; i < 8 * DLEN; i++)
        s->dif[0][0][i] = 0.0f;
}

/* a new type: the lines cleared, the heads at the time, the matrix at its targets */
static void set_type(struct state *s, int t)
{
    const struct design *d = &s->d;
    s->type = t;
    clear(s);
    s->head = 0;
    s->xf = 0.0f;
    for (int h = 0; h < 2; h++) {
        s->ht[h][0] = s->hs[h][0] = d->time_l;
        s->ht[h][1] = s->hs[h][1] = d->time_r;
    }
    s->fb[0] = d->ll;
    s->fb[1] = d->rr;
    s->fb[2] = d->lr;
    s->fb[3] = d->rl;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    float *b = buffer;
    s->line[0] = b;
    s->line[1] = b + STRIDE;
    for (int ch = 0; ch < 2; ch++)
        for (int st = 0; st < 4; st++)
            s->dif[ch][st] = b + 2 * STRIDE + (uint32_t)(4 * ch + st) * DLEN;
    changed(s, knobs, 500);
    design(s, knobs, 500, MODFX_FS);
    set_type(s, modfx_option(knobs[K_TYPE], NTYPES));
    s->gt = 1.0f;
    s->lfo3.ph = 0.0f;
    return 0;
}

/* the heads of one channel over n frames into y: both move, the heard ones are read */
static void heads(struct state *s, int ch, const float *g1, int n, float mod, int xfading, float *y)
{
    const float kr = 0x1.ff7ceep-1f;                            /* per frame ~23 ms */
    const float *line = s->line[ch];
    float out0[N], out1[N];
    for (int h = 0; h < 2; h++) {
        /* the smoothing keeps the distance to the target and lets it decay: it ends on the target exactly (a plain
           s = k t + r s stalls up to a frame away from it in float, wherever its start leaves it) */
        const float tgt = mod + s->ht[h][ch];
        float off = s->hs[h][ch] - tgt;
        const int heard = s->head == h || xfading;
        float *o = h ? out1 : out0;
#ifdef DELAY_PLAIN_SMOOTHER
        float sm = s->hs[h][ch];                        /* the plain form, for comparisons: s = (1 - r) t + r s */
        const float b = 0x1.0624p-10f * tgt;
#endif
        for (int i = 0; i < n; i++) {
            off = off * kr;
#ifdef DELAY_PLAIN_SMOOTHER
            sm = b + sm * kr;
            off = sm - tgt;
#endif
            if (heard) {
                const float v = wvfx_clamp(tgt + off, 0.001f, TMAX);
                uint32_t wi = s->w + (uint32_t)i;
                o[i] = modfx_tap_wrap(line, LINE, wi, MODFX_FS * v + LAT);
            }
        }
        s->hs[h][ch] = tgt + off;
        if (!heard)
            for (int i = 0; i < n; i++)
                o[i] = 0.0f;
    }
    for (int i = 0; i < n; i++)
        y[i] = out0[i] * (1.0f - g1[i]) + out1[i] * g1[i];
}

static void step(struct state *s, float *x, int n, int on)
{
    const struct design *d = &s->d;
    const struct type *t = d->t;
    const float fs = MODFX_FS;
    float in[2][N], wet[2][N];
    /* the input fade (bypassed: the input leaves the line, the echoes ring out) */
    {
        const float c = 0x1.ff7ceep-1f, kk = on ? 1.0f - c : 0.0f;
        float g = s->in_g;
        for (int i = 0; i < n; i++) {
            g = kk + c * g;
            in[0][i] = x[2 * i] * g;
            in[1][i] = x[2 * i + 1] * g;
        }
        s->in_g = g;
    }
    float env[N];
    if (t->duck > 0.1f) {                               /* the ducker: the echoes duck under the playing */
        float p = 0.0f;
        for (int i = 0; i < n; i++)
            p += in[0][i] * in[0][i] + in[1][i] * in[1][i];
        p = 0.1f * (p * (1.0f / 32.0f)) + 0.9f * s->duck_p;
        p = p > 1e-14f ? p : 1e-14f;
        s->duck_p = p;
        const float lvl = 3.01029996f * wvfx_log2(p);   /* 10 log10 */
        const float thc = t->duck_thr < -1.0f ? t->duck_thr : -1.0f, q = t->duck / thc;
        const float g = lvl < t->duck_thr ? 1.0f : modfx_exp2(0.16609640f * (-q * (thc - lvl)));
        const float c = g < s->duck_e ? 0.99f : d->duck_c, a = g * (1.0f - c);
        float e = s->duck_e;
        for (int i = 0; i < n; i++) {
            e = a + e * c;
            env[i] = e;
        }
        s->duck_e = e;
    }
    if (t->shelf_in)
        for (int ch = 0; ch < 2; ch++)
            shelf_run(&shelves[t->shelf_in], s->sh_in[ch], in[ch], n);
    /* modulation and the heads */
    float l1[2], l2[2], l3[2];
    lfo_run(&s->lfo1, 0, d->lfo1_rate, d->lfo1_amp, t->mod_quad ? 1.57079637f : 0.0f, fs, l1);
    lfo_run(&s->lfo2, t->lfo2.form, t->lfo2.rate, t->lfo2.amp, t->lfo2.ofs, fs, l2);
    lfo_run(&s->lfo3, t->lfo3.form, t->lfo3.rate, t->lfo3.amp, t->lfo3.ofs, fs, l3);
    {                                                   /* LFO 3's 640 Hz one-pole at the control rate */
        static const struct op sm3 = { 0.043633174f, 0.043633174f, 0.912733674f };
        l3[0] = op_run(&sm3, &s->lfo3.sm[0], l3[0]);
        l3[1] = op_run(&sm3, &s->lfo3.sm[1], l3[1]);
    }
    const float mod[2] = { d->mod_amp * l2[0] + d->mod_amp * l1[0], d->mod_amp * l1[1] + d->mod_amp * l2[1] };
    int k = s->head;
    if (__builtin_fabsf(d->time_l - s->ht[k][0]) > 0.01f || __builtin_fabsf(d->time_r - s->ht[k][1]) > 0.01f) {
        k ^= 1;                                         /* a jump: the other head starts at the new time */
        s->hs[k][0] = d->time_l;
        s->hs[k][1] = d->time_r;
    }
    s->ht[k][0] = d->time_l;
    s->ht[k][1] = d->time_r;
    s->head = k;
    float g1[N];
    int xfading = 0;
    {
        const float st = k ? 0.001f : -0.001f;
        float v = s->xf;
        for (int i = 0; i < n; i++) {
            v = wvfx_clamp(st + v, 0.0f, 1.0f);
            g1[i] = v;
            xfading |= v > 0.0f && v < 1.0f;
        }
        s->xf = v;
    }
    const int nch = d->skip_r ? 1 : 2;
    for (int ch = 0; ch < nch; ch++)
        heads(s, ch, g1, n, mod[ch], xfading, wet[ch]);
    if (d->skip_r)
        for (int i = 0; i < n; i++)
            wet[1][i] = 0.0f;
    /* the wet path */
    {
        const float g = t->drive, rg = 1.0f / g, c = 0x1.f26cf6p-6f;   /* the curve at 0 */
        for (int ch = 0; ch < nch; ch++)
            for (int i = 0; i < n; i++)
                wet[ch][i] = (clip_rational(wet[ch][i] * g) - c) * rg;
    }
    if (t->dif_mix > 0.001f) {
        float m[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, m2[4];
        if (t->dif_mod > 0.0f) {
            float u[2];
            lfo_run(&s->lfo_dif, 0, t->dif_rate, t->dif_mod, 1.57079637f, fs, u);
            const float h = 0.5f * t->dif_mod, k0 = 1.0f - h;
            m[0] = u[0] * h + k0;
            m[1] = u[1] * h + k0;
            m[2] = -u[0] * h + k0;
            m[3] = -u[1] * h + k0;
        }
        diffuse(s, 0, wet[0], n, m);
        m2[0] = m[1];
        m2[1] = m[2];
        m2[2] = m[3];
        m2[3] = m[0];
        if (nch > 1)
            diffuse(s, 1, wet[1], n, m2);
        s->dw = (s->dw + (uint32_t)n) & DMASK;
    }
    for (int ch = 0; ch < nch; ch++) {
        if (d->lowcut_on) {
            if (d->nlc == 0)
                op_block(&d->lc1, &s->s_lc1[ch], wet[ch], n);
            for (int j = 0; j < d->nlc; j++)
                bq_block(&d->lc[j], s->s_lc[j][ch], wet[ch], n);
        }
        if (d->hicut_on) {
            if (d->nhc == 0)
                op_block(&d->hc1, &s->s_hc1[ch], wet[ch], n);
            for (int j = 0; j < d->nhc; j++)
                bq_block(&d->hc[j], s->s_hc[j][ch], wet[ch], n);
        }
        if (t->post)
            op_block(&posts[t->post], &s->s_post[ch], wet[ch], n);
        if (d->comp) {                                  /* the expander */
            const float c = d->exp_c, kk = 1.0f - c;
            float e = s->ee[ch];
            for (int i = 0; i < n; i++) {
                e = kk * (0.001f + __builtin_fabsf(wet[ch][i])) + c * e;
                wet[ch][i] = wet[ch][i] * (10.0f * e);
            }
            s->ee[ch] = e;
        }
    }
    /* the feedback matrix, smoothed per 16 frames */
    s->fb[0] = 0.01f * d->ll + 0.99f * s->fb[0];
    s->fb[1] = 0.99f * s->fb[1] + d->rr * 0.01f;
    s->fb[2] = 0.01f * d->lr + 0.99f * s->fb[2];
    s->fb[3] = 0.99f * s->fb[3] + 0.01f * d->rl;
    float lin[2][N];
    for (int i = 0; i < n; i++) {
        const float cl = wet[0][i], cr = wet[1][i];
        if (!t->fbsum) {
            lin[0][i] = (in[0][i] + (cl * s->fb[0] + cr * s->fb[3])) + in[1][i];
            lin[1][i] = cr * s->fb[1] + cl * s->fb[2];
        } else {
            lin[0][i] = (in[0][i] + cl * s->fb[0]) + cr * s->fb[3];
            lin[1][i] = (in[1][i] + cr * s->fb[1]) + cl * s->fb[2];
        }
    }
    for (int ch = 0; ch < nch; ch++) {
        float *v = lin[ch];
        for (int i = 0; i < n; i++)
            v[i] = wvfx_clamp(v[i], -8.0f, 8.0f);
        if (d->comp) {                                  /* the compressor */
            const float c = d->comp_c, kk = 1.0f - c;
            float e = s->ce[ch];
            for (int i = 0; i < n; i++) {
                e = kk * (0.001f + __builtin_fabsf(v[i])) + c * e;
                const float z = 10.0f * e;              /* x / sqrt(z), by Newton from the bit trick */
                union { float f; uint32_t u; } q = { z };
                q.u = 0x5f3759dfu - (q.u >> 1);
                float y = q.f;
                y = y * (1.5f - 0.5f * z * y * y);
                y = y * (1.5f - 0.5f * z * y * y);
                y = y * (1.5f - 0.5f * z * y * y);
                v[i] = v[i] * y;
            }
            s->ce[ch] = e;
        }
        if (t->bits) {
            const float sc = (float)(1u << (24 - t->bits)), r = 1.0f / sc;
            for (int i = 0; i < n; i++)
                v[i] = (float)(int)(0.5f + v[i] * sc) * r;
        }
        float *line = s->line[ch];
        uint32_t w = s->w;
        for (int i = 0; i < n; i++) {
            line[w] = v[i];
            if (w < MODFX_MIRROR)
                line[LINE + w] = v[i];
            if (++w >= LINE)
                w = 0;
        }
    }
    s->w = s->w + (uint32_t)n >= LINE ? s->w + (uint32_t)n - LINE : s->w + (uint32_t)n;
    /* the tail */
    if (t->shelf_out)
        for (int ch = 0; ch < 2; ch++)
            shelf_run(&shelves[t->shelf_out], s->sh_out[ch], wet[ch], n);
    if (t->mode == M_SWEEP) {
        float a[2];
        lfo_run(&s->lfo_sw, 0, 0.24999471f, 0.5f, 0.0f, fs, a);
        for (int ch = 0; ch < 2; ch++) {
            s->sw_pos[ch] = 0.9f * s->sw_pos[ch] + 0.1f * (0.5f + a[ch]);
            const struct svf f = svf_bp(200.002f * modfx_pow(8.00006f, s->sw_pos[ch]), 0.499989f, fs);
            float s1 = s->sw_s[ch][0], s2 = s->sw_s[ch][1];
            for (int i = 0; i < n; i++) {
                const float hp = (wet[ch][i] - s1) - s2;
                wet[ch][i] = 2.0f * (s1 * f.c3 + f.c2 * hp);   /* x 1 / resonance */
                s2 = s2 + f.c1 * s1;
                s1 = hp * f.c0 + s1;
            }
            s->sw_s[ch][0] = s1;
            s->sw_s[ch][1] = s2;
        }
    }
    if (t->lfo3_tgt == T_LEVEL) {
        const float kk = 1.0f - t->lfo3.amp * 0.5f, gl = kk + 0.5f * l3[0], gr = kk + 0.5f * l3[1];
        for (int i = 0; i < n; i++) {
            wet[0][i] *= gl;
            wet[1][i] *= gr;
        }
    }
    float bl, br;
    if (t->mode == M_MONO) {
        bl = t->lfo3_tgt == T_PAN ? l3[0] * t->echo_pan : t->echo_pan;
        br = 0.0f;
    } else if (t->mode == M_DUAL) {
        bl = t->pan_l;
        br = t->pan_r;
        if (t->lfo3_tgt == T_PAN) {
            bl *= l3[0];
            br *= l3[1];
        }
    } else {
        bl = -1.0f;
        br = 1.0f;
    }
    const float pl = 0.5f + 0.5f * bl, pr = 0.5f + 0.5f * br, fr = t->flip ? -1.0f : 1.0f;
    for (int i = 0; i < n; i++) {
        float ol = wet[0][i] * (1.0f - pl) + wet[1][i] * (1.0f - pr), orr = wet[0][i] * pl + wet[1][i] * pr;
        orr *= fr;
        if (t->duck > 0.1f) {
            ol *= env[i];
            orr *= env[i];
        }
        wet[0][i] = ol;
        wet[1][i] = orr;
    }
    /* the mix */
    for (int i = 0; i < n; i++) {
        x[2 * i] = wet[0][i];
        x[2 * i + 1] = wet[1][i];
    }
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    if (changed(s, io->knobs, io->beat_ms)) {
        design(s, io->knobs, io->beat_ms, MODFX_FS);
        if (modfx_option(io->knobs[K_TYPE], NTYPES) != s->type)
            set_type(s, modfx_option(io->knobs[K_TYPE], NTYPES));
    }
    const struct design *d = &s->d;
    float *x = io->io;
    const float tw = d->wet, td = io->enabled ? d->dry : 0.0f, tt = io->enabled ? 0.0f : 1.0f;
    if (!io->enabled && s->idle && s->gd == 0.0f && s->gt == 1.0f)
        return;                                         /* rung out: the input, bit for bit */
    float wet[2 * WVFX_MAX_FRAMES];
    for (int i = 0; i < 2 * n; i++)
        wet[i] = x[i];
    for (int c0 = 0; c0 < n; c0 += N)
        step(s, &wet[2 * c0], n - c0 < N ? n - c0 : N, io->enabled);
    float peak = 0.0f;
    for (int i = 0; i < 2 * n; i++)
        peak = __builtin_fmaxf(peak, __builtin_fabsf(wet[i]));
    if (io->enabled || peak > 1e-7f || s->in_g > 1e-6f) {
        s->quiet = 0;
        s->idle = 0;
    } else if (++s->quiet > 70) {                       /* 0.1 s of silence: rung out */
        s->idle = 1;
    }
    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n), dt = wvfx_ramp_step(s->gt, tt, n);
    float gw = s->gw, gd = s->gd, gt = s->gt;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        gt += dt;
        x[2 * i] = gw * wet[2 * i] + (gd + gt) * x[2 * i];
        x[2 * i + 1] = gw * wet[2 * i + 1] + (gd + gt) * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
    s->gt = tt;
}

const struct wvfx_desc delay_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 1),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Delay",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = (2 * STRIDE + 8 * DLEN) * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
