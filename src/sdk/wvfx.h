/*
 * waveod effect blocks (docs/blocks.md): what a block of waveod's own is, for the firmware and for the block.
 *
 * A block is built either into the firmware (built-in) or as a module (an ELF in waveod's library, loaded into
 * the pools like the original's modules); the source is the same. It depends on nothing but this header:
 * no libc beyond what the compiler inlines, no mutable statics (the same code runs in several slots, and in
 * the spillover's tail while the next preset loads), no floating-point library calls.
 *
 * Audio: 44.1 kHz, blocks of up to WVFX_MAX_FRAMES frames (64 today; 16 and 32 are possible), interleaved
 * stereo float, full scale 1.0, processed in place.
 */
#pragma once

#include <stdint.h>

#define WVFX_MAGIC      0x58465657u             /* "WVFX" */
#define WVFX_ABI        1
#define WVFX_MAX_FRAMES 64
#define WVFX_KNOBS      15                      /* knob words of a slot */
#define WVFX_MAX_PARAMS 15

/* an id: 'W', the menu, a number */
#define WVFX_ID(menu, n) (0x57000000u | ((uint32_t)(menu) << 16) | ((uint32_t)(n) & 0xffffu))
#define WVFX_IS_ID(id)   (((id) >> 24) == 0x57u)

/* the menus of the pedal (fw/library.h enum lib_menu) */
enum wvfx_menu {
    WVFX_DYN, WVFX_FX, WVFX_WAH, WVFX_DRV, WVFX_AMP, WVFX_CAB, WVFX_IR, WVFX_EQ,
    WVFX_MOD, WVFX_DLY, WVFX_RVB, WVFX_CLONE, WVFX_LOOP, WVFX_SND, WVFX_RTN, WVFX_VOL
};

/* display formats: 0 = the value without decimals, 0x01..0xC3 = the original's codes (model/param_format_table.c,
   e.g. 0x75 = "Center" / "R30" / "L30"), and waveod's: */
#define WVFX_FMT_NUMBER  0x00                   /* the value, no decimals */
#define WVFX_FMT_SIGNED  0xF0                   /* "+40", "0", "-40" */
#define WVFX_FMT_DEGREES 0xF1                   /* "30°" */

struct wvfx_param {
    const char *name;
    float min, max, step, def;
    uint8_t knob;                               /* knob word 0..WVFX_KNOBS-1 */
    uint8_t format;                             /* a display format (above), unless options / unit say more */
    uint8_t sync;                               /* the original's f2: 0x1N = tempo-synced, N its switch; 0x2N = switch */
    uint8_t pad;
    const char *options;                        /* named values, "Off\0On\0" (value 0, 1, ...), or 0 */
    const char *unit;                           /* a template with one %.Nf, "%.1f dB", or 0 */
};

/* What init gets. */
struct wvfx_env {
    uint32_t abi;
    float rate;                                 /* 44100 */
    uint16_t max_frames;                        /* largest block process will get */
    uint8_t slot;                               /* 0..11 */
    uint8_t pad;
    const void *data;                           /* the item's data: a Cab / User IR item's IR (1024 int32), a Clone
                                                   item's capture (0x2000 bytes), else 0 */
    uint32_t data_size;
};

/* What process gets, every audio block. */
struct wvfx_io {
    float *io;                                  /* interleaved stereo, in place */
    const float *aux;                           /* the chain's input (guitar + FX return), for side chains */
    float *send;                                /* FX send, the FX loop blocks write it */
    const float *ret;                           /* FX return */
    const float *knobs;                         /* WVFX_KNOBS words, they change between blocks */
    uint32_t beat_ms;                           /* ms per beat (the tempo) */
    uint16_t frames;
    uint8_t enabled;                            /* 0 = bypassed: the block passes the signal (it may let tails ring) */
    uint8_t pad;
};

/* flags */
#define WVFX_ENGINE  0x01                       /* runs the items of its menu instead of the library's engine entry */
#define WVFX_INHERIT 0x02                       /* its parameters come after the library entry's */
#define WVFX_REPLACE 0x04                       /* built-ins only: runs its menu's library entry in place of the
                                                   original's module (the entry's id, name and knobs) */

/* what init may return besides 0 */
#define WVFX_ERR_NO_MEMORY (-2)                 /* a built-in's own allocation failed: like a full pool */

struct wvfx_desc {
    uint32_t magic;                             /* WVFX_MAGIC */
    uint32_t abi;                               /* WVFX_ABI */
    uint32_t id;                                /* WVFX_ID(menu, n) */
    uint8_t menu;                               /* enum wvfx_menu */
    uint8_t nparams;
    uint8_t flags;
    uint8_t pad;
    const char *name;                           /* up to 23 characters */
    const char *category;                       /* the tab in the models screen */
    const struct wvfx_param *params;
    uint32_t state_size;                        /* bytes per instance in the state pool (OCRAM, fast) */
    uint32_t buffer_size;                       /* bytes per instance in the buffer pool (SDRAM), may be 0 */
    /* set up; state and buffer are not cleared. 0 = ready, WVFX_ERR_NO_MEMORY = the pools are full, other = the
       slot does not run (the signal passes) */
    int (*init)(void *state, void *buffer, const struct wvfx_env *env, const float *knobs);
    /* one block, in the audio interrupt */
    void (*process)(void *state, const struct wvfx_io *io);
    /* main loop, after the knobs changed or on its own; may be 0. 1 = did something */
    int (*poll)(void *state, const float *knobs);
};

/*
 * A block built as a module (tools/wvfx.py build) starts its code region with this header (sdk/wvfx_module.c): the
 * original's module header (the loader writes word 0, checks the magic at 0x24), with "WVFX" and the descriptor
 * in words the original leaves free. Init / Process stay 0: the original's app cannot run a block.
 */
struct wvfx_module_header {
    uint32_t host_word;                         /* written by the loader */
    uint32_t magic;                             /* WVFX_MAGIC */
    uint32_t abi;                               /* WVFX_ABI */
    const struct wvfx_desc *desc;
    uint32_t reserved;
    uint32_t id;                                /* 0 (the descriptor has it) */
    uint32_t srom;
    uint32_t init, process;                     /* 0 */
    uint32_t module_magic;                      /* 0xFFAA55AA */
};

/* ---------------------------------------------------------------- helpers for blocks */

static inline float wvfx_clamp(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* 2^x without libm (relative error up to 8.5e-5: the Taylor terms of the fraction; blocks that need more have their
   own, e.g. blocks/modfx/modfx.h) */
static inline float wvfx_exp2(float x)
{
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    int i = (int)x;
    if ((float)i > x)
        i--;
    float f = x - (float)i;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return p * v.f;
}

static inline float wvfx_db(float db)
{
    return wvfx_exp2(db * 0.16609640474f);
}

/* log2 x for x > 0 without libm (error ~1e-5); x <= 0 gives -127 */
static inline float wvfx_log2(float x)
{
    if (!(x > 0.0f))
        return -127.0f;
    union { float f; uint32_t u; } v = { x };
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;        /* mantissa in [1, 2) */
    float m = v.f, t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    /* ln m = 2 (t + t^3/3 + t^5/5 + t^7/7 + t^9/9), m in [1, 2): t <= 1/3 */
    float ln = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));
    return (float)e + ln * 1.44269504f;
}

/* a level 0..1 in dB of full scale (silence: -762) */
static inline float wvfx_to_db(float x)
{
    return 6.02059991f * wvfx_log2(x);
}

/* one-pole smoothing coefficient for time constant `ms` at `rate`: y += (1 - c) (x - y) */
static inline float wvfx_coef(float ms, float rate)
{
    return ms <= 0.0f ? 0.0f : wvfx_exp2(-1442.69504f / (ms * rate));
}

/* a gain moving towards `target` over one block: g at frame f = from + (f + 1) * step */
static inline float wvfx_ramp_step(float from, float target, int frames)
{
    return (target - from) / (float)frames;
}
