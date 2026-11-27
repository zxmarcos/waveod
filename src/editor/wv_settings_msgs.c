/*
 * Protocol 9 of group 0xF: the GLOBAL screen's settings for waveod Studio, both kinds in one list (the original's
 * record 0, fw/globals_format.h, and waveod's own, model/settings.h), plus the global EQ's 31 parameters.
 *
 *   0x1F000019 settings     {}                                  -> the list
 *   0x1F00001A settings put {u32 n, n x {u32 id, f32 value}}    -> {i32 error, u32 the id that failed (0)}, the list
 *
 * The list: {u8 count, u8 0, u16 0}, then per setting {u8 id, u8 flags (1 read only), u16 0, f32 value, min, max,
 * step}. Values travel as floats (the integers exactly). A put changes the settings in order, each the way the
 * GLOBAL screen does (scr_settings.c: the same ranges and the same effects: applied at once, the chain rebuilt
 * for the Cab / IR engine...), and stops at the first it refuses (-10 an unknown setting, -11 a read-only one,
 * -12 a value it does not take); what was changed stays. The system's own values come read only (model/sysmon.h:
 * the CPU's temperature now, its highest and lowest since start-up, in degrees C; NaN when unknown). Both records are written after 2 s without changes
 * (globals_touch / settings_touch), and the pedal's screens redraw (globals_generation / settings_generation).
 */
#include "editor/server.h"
#include "fw/bytes.h"
#include "model/assign.h"
#include "model/globals.h"
#include "model/settings.h"
#include "model/sysmon.h"
#include "ui/i18n_msgs.h"
#include "platform/platform.h"
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

enum wv_setting {
    /* the original's record 0 */
    WS_INPUT_MODE = 1, WS_INPUT_LEVEL, WS_AUX, WS_CABS, WS_USB_MODE, WS_USB_REC, WS_USB_MON, WS_USB_WET, WS_USB_AUX,
    WS_MIDI_SOURCE, WS_MIDI_TRS, WS_MIDI_USB, WS_CLOCK_SOURCE, WS_CLOCK_OUT, WS_EQ_ON, WS_TEMPO, WS_EXP2_JACK,
    WS_EXT1, WS_EXT2, WS_EXP1_MIN, WS_EXP1_MAX, WS_EXP2_MIN, WS_EXP2_MAX,
    /* waveod's */
    WS_A4 = 32, WS_TUNER_MUTE, WS_TUNER_STROBE, WS_BRIGHTNESS, WS_CURSOR, WS_CAB_ORIGINAL, WS_SPILL, WS_SCENE_MANUAL,
    WS_LANGUAGE,
    /* the system's, read only (GLOBAL > SISTEMA) */
    WS_CPU_TEMP = 48, WS_CPU_TEMP_MAX, WS_CPU_TEMP_MIN,
    /* the global EQ: WS_EQ + enum eq_param */
    WS_EQ = 64,
};

#define RO 1                                /* flags: read only (set on the pedal: the calibration) */

/* the integer settings: id, flags, min, max, step (the EQ's ranges are eq_param_ranges) */
static const struct range {
    uint8_t id, flags;
    int16_t min, max, step;
} ranges[] = {
    { WS_INPUT_MODE, 0, 0, 2, 1 },          /* enum input_mode: acoustic, electric, line */
    { WS_INPUT_LEVEL, 0, -20, 20, 1 },      /* dB */
    { WS_AUX, 0, 0, 100, 1 },
    { WS_CABS, 0, 0, 3, 1 },                /* no cabinet on L | on R << 1 */
    { WS_USB_MODE, 0, 0, 1, 1 },            /* 0 6-in 4-out, 1 2-in 2-out */
    { WS_USB_REC, 0, -20, 20, 1 },
    { WS_USB_MON, 0, -20, 20, 1 },
    { WS_USB_WET, 0, 0, 3, 1 },             /* processed channel 1 | channel 2 << 1 */
    { WS_USB_AUX, 0, 0, 1, 1 },
    { WS_MIDI_SOURCE, 0, 0, 2, 1 },         /* TRS, USB, both */
    { WS_MIDI_TRS, 0, 0, 16, 1 },           /* 0 omni */
    { WS_MIDI_USB, 0, 0, 16, 1 },
    { WS_CLOCK_SOURCE, 0, 0, 4, 1 },
    { WS_CLOCK_OUT, 0, 0, 3, 1 },           /* bit 0 TRS, bit 1 USB */
    { WS_EQ_ON, 0, 0, 1, 1 },
    { WS_TEMPO, 0, 0, 300, 1 },             /* 0 off, else 40..300 BPM */
    { WS_EXP2_JACK, 0, 0, 2, 1 },           /* enum exp2_jack */
    { WS_EXT1, 0, -128, 127, 1 },           /* enum fs_function, one of assign_fn_step's list */
    { WS_EXT2, 0, -128, 127, 1 },
    { WS_EXP1_MIN, RO, 0, 255, 1 },
    { WS_EXP1_MAX, RO, 0, 255, 1 },
    { WS_EXP2_MIN, RO, 0, 255, 1 },
    { WS_EXP2_MAX, RO, 0, 255, 1 },
    { WS_A4, 0, 430, 450, 1 },              /* Hz */
    { WS_TUNER_MUTE, 0, 0, 1, 1 },
    { WS_TUNER_STROBE, 0, 0, 1, 1 },
    { WS_BRIGHTNESS, 0, 10, 100, 10 },      /* % */
    { WS_CURSOR, 0, 0, 1, 1 },
    { WS_CAB_ORIGINAL, 0, 0, 1, 1 },        /* the original's Cab / IR engine instead of waveod's */
    { WS_SPILL, 0, 0, SETTINGS_SPILL_CHOICES - 1, 1 },     /* settings_spill_ms */
    { WS_SCENE_MANUAL, 0, 0, 1, 1 },        /* KNOBS NA CENA: 1 MANUAL, 0 AUTO */
    { WS_LANGUAGE, 0, 0, LANG_COUNT - 1, 1 },   /* ui/i18n_msgs.h enum i18n_lang (0 English) */
    { WS_CPU_TEMP, RO, -40, 125, 0 },       /* degrees C, tenths (NaN: no sensor reading) */
    { WS_CPU_TEMP_MAX, RO, -40, 125, 0 },
    { WS_CPU_TEMP_MIN, RO, -40, 125, 0 },
};
#define RANGES ((int)(sizeof ranges / sizeof ranges[0]))
#define ENTRY 20

static uint8_t out[8 + (RANGES + EQ_PARAMS) * ENTRY] SDRAM_BSS;

static int round_int(float v)
{
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

static const struct range *range_of(int id)
{
    for (int i = 0; i < RANGES; i++)
        if (ranges[i].id == id)
            return &ranges[i];
    return 0;
}

/* tenths of a degree -> degrees, NaN when unknown */
static float degrees(int tenths)
{
    return tenths == PLAT_TEMP_NONE ? __builtin_nanf("") : (float)tenths / 10.0f;
}

static float value_of(int id)
{
    const struct globals *g = srv.globals;
    const struct settings *st = srv.settings;
    switch (id) {
    case WS_INPUT_MODE: return g->input_mode;
    case WS_INPUT_LEVEL: return g->input_level;
    case WS_AUX: return g->aux_level;
    case WS_CABS: return (float)(g->no_cab[0] | g->no_cab[1] << 1);
    case WS_USB_MODE: return g->usb_mode;
    case WS_USB_REC: return g->usb_rec_level;
    case WS_USB_MON: return g->usb_mon_level;
    case WS_USB_WET: return (float)(g->usb_rec_wet[0] | g->usb_rec_wet[1] << 1);
    case WS_USB_AUX: return g->usb_rec_aux;
    case WS_MIDI_SOURCE: return g->midi_source < 3 ? g->midi_source : 2;
    case WS_MIDI_TRS: return g->midi_channel[0] >= 0 && g->midi_channel[0] <= 16 ? g->midi_channel[0] : 0;
    case WS_MIDI_USB: return g->midi_channel[1] >= 0 && g->midi_channel[1] <= 16 ? g->midi_channel[1] : 0;
    case WS_CLOCK_SOURCE: return g->clock_source < 5 ? g->clock_source : 4;
    case WS_CLOCK_OUT: return g->clock_out & 3;
    case WS_EQ_ON: return g->eq_on;
    case WS_TEMPO: return g->tempo_mode && g->bpm ? g->bpm : 0;
    case WS_EXP2_JACK: return g->exp2_jack % 3;
    case WS_EXT1: return g->ext_fn[0];
    case WS_EXT2: return g->ext_fn[1];
    case WS_EXP1_MIN: return g->exp_min[0];
    case WS_EXP1_MAX: return g->exp_max[0];
    case WS_EXP2_MIN: return g->exp_min[1];
    case WS_EXP2_MAX: return g->exp_max[1];
    case WS_A4: return st->a4;
    case WS_TUNER_MUTE: return st->tuner_mute;
    case WS_TUNER_STROBE: return st->tuner_strobe;
    case WS_BRIGHTNESS: return st->brightness;
    case WS_CURSOR: return st->touch_cursor;
    case WS_CAB_ORIGINAL: return st->cab_original;
    case WS_SPILL: return st->spill;
    case WS_SCENE_MANUAL: return st->scene_manual;
    case WS_LANGUAGE: return st->language;
    case WS_CPU_TEMP: return degrees(sysmon_get()->temp);
    case WS_CPU_TEMP_MAX: return degrees(sysmon_get()->temp_max);
    case WS_CPU_TEMP_MIN: return degrees(sysmon_get()->temp_min);
    }
    if (id >= WS_EQ && id < WS_EQ + EQ_PARAMS)
        return g->eq[id - WS_EQ];
    return 0;
}

/* one setting changed the way the GLOBAL screen changes it; 0 or the error */
static int set(int id, float value)
{
    struct globals *g = srv.globals;
    struct settings *st = srv.settings;
    if (id >= WS_EQ && id < WS_EQ + EQ_PARAMS) {
        const struct eq_param_range *r = &eq_param_ranges[id - WS_EQ];
        if (!(value == value))              /* NaN */
            return -12;
        g->eq[id - WS_EQ] = value < r->min ? r->min : value > r->max ? r->max : value;
        globals_apply(g);
        globals_touch();
        return 0;
    }
    const struct range *r = range_of(id);
    if (!r)
        return -10;
    if (r->flags & RO)
        return -11;
    if (!(value == value) || value < r->min - 0.5f || value > r->max + 0.5f)
        return -12;
    int v = round_int(value);
    if (id >= WS_A4) {
        switch (id) {
        case WS_A4: st->a4 = (uint16_t)v; break;
        case WS_TUNER_MUTE: st->tuner_mute = v != 0; break;
        case WS_TUNER_STROBE: st->tuner_strobe = v != 0; break;
        case WS_BRIGHTNESS: st->brightness = (uint8_t)v; break;
        case WS_CURSOR: st->touch_cursor = v != 0; break;     /* the UI sets the marker (settings_generation) */
        case WS_SPILL: st->spill = (uint8_t)v; break;
        case WS_LANGUAGE: st->language = (uint8_t)v; break;     /* the UI builds its screens again (ui_tick) */
        case WS_SCENE_MANUAL:
            st->scene_manual = v != 0;
            srv.rig->scene_auto = !st->scene_manual;
            break;
        case WS_CAB_ORIGINAL:
            if (st->cab_original != (v != 0)) {
                st->cab_original = v != 0;
                settings_apply(st);
                rig_rebuild(srv.rig);       /* the preset again on the other engine */
            }
            break;
        }
        settings_apply(st);
        settings_touch();
        return 0;
    }
    switch (id) {
    case WS_INPUT_MODE: g->input_mode = (uint8_t)v; break;
    case WS_INPUT_LEVEL: g->input_level = (int8_t)v; break;
    case WS_AUX: g->aux_level = (uint8_t)v; break;
    case WS_CABS: g->no_cab[0] = (uint8_t)(v & 1); g->no_cab[1] = (uint8_t)(v >> 1); break;
    case WS_USB_MODE: g->usb_mode = (uint8_t)v; break;
    case WS_USB_REC: g->usb_rec_level = (int8_t)v; break;
    case WS_USB_MON: g->usb_mon_level = (int8_t)v; break;
    case WS_USB_WET: g->usb_rec_wet[0] = (uint8_t)(v & 1); g->usb_rec_wet[1] = (uint8_t)(v >> 1); break;
    case WS_USB_AUX: g->usb_rec_aux = v != 0; break;
    case WS_MIDI_SOURCE: g->midi_source = (uint8_t)v; break;
    case WS_MIDI_TRS: g->midi_channel[0] = (int16_t)v; break;
    case WS_MIDI_USB: g->midi_channel[1] = (int16_t)v; break;
    case WS_CLOCK_SOURCE: g->clock_source = (uint8_t)v; break;
    case WS_CLOCK_OUT: g->clock_out = (uint8_t)v; break;
    case WS_EQ_ON: g->eq_on = v != 0; break;
    case WS_TEMPO:                          /* off below 40 BPM; the effects follow it while it is on */
        if (v && v < 40)
            return -12;
        g->tempo_mode = v >= 40;
        if (v >= 40)
            g->bpm = (uint16_t)v;
        rig_set_global_tempo(srv.rig, g->tempo_mode ? g->bpm : 0);
        break;
    case WS_EXP2_JACK: g->exp2_jack = (uint8_t)v; break;
    case WS_EXT1:
    case WS_EXT2:
        if (assign_fn_step(v, 0) != v)      /* not a function the pedal offers */
            return -12;
        g->ext_fn[id == WS_EXT2] = (int8_t)v;
        break;
    }
    globals_apply(g);
    globals_touch();
    return 0;
}

static void putf(uint8_t *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put32le(p, u);
}

static unsigned entry(unsigned at, int id, int flags, float value, float min, float max, float step)
{
    out[at] = (uint8_t)id;
    out[at + 1] = (uint8_t)flags;
    out[at + 2] = out[at + 3] = 0;
    putf(out + at + 4, value);
    putf(out + at + 8, min);
    putf(out + at + 12, max);
    putf(out + at + 16, step);
    return at + ENTRY;
}

/* the list at out + at; its length */
static unsigned list(unsigned at)
{
    unsigned start = at;
    out[at] = (uint8_t)(RANGES + EQ_PARAMS);
    out[at + 1] = out[at + 2] = out[at + 3] = 0;
    at += 4;
    for (int i = 0; i < RANGES; i++)
        at = entry(at, ranges[i].id, ranges[i].flags, value_of(ranges[i].id), ranges[i].min, ranges[i].max,
                   ranges[i].step);
    for (int k = 0; k < EQ_PARAMS; k++) {
        const struct eq_param_range *r = &eq_param_ranges[k];
        at = entry(at, WS_EQ + k, 0, srv.globals->eq[k], r->min, r->max, r->step < 0 ? -r->step : r->step);
    }
    return at - start;
}

void msg_waveod_settings(const struct msg *m)
{
    if (m->cmd == CMD_WV_SETTINGS) {
        reply(m->cmd, out, list(0));
        return;
    }
    uint32_t n = msg_word(m, 2), bad = 0;
    int err = 0;
    if (12 + 8 * n > m->len)
        err = -10;
    for (uint32_t i = 0; i < n && !err; i++) {
        uint32_t id = msg_word(m, 3 + 2 * (int)i), w = msg_word(m, 4 + 2 * (int)i);
        float v;
        memcpy(&v, &w, 4);
        err = id < 256 ? set((int)id, v) : -10;
        if (err)
            bad = id;
    }
    put32le(out, (uint32_t)err);
    put32le(out + 4, bad);
    reply(m->cmd, out, 8 + list(8));
}
