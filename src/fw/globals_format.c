#include "fw/globals_format.h"
#include "fw/bytes.h"
#include <string.h>

#define OFF_AUTO_CAB    0x011
#define OFF_BACKLIGHT   0x016
#define OFF_INPUT_LEVEL 0x01A
#define OFF_INPUT_MODE  0x01B
#define OFF_NO_CAB      0x01C
#define OFF_USB_REC     0x01E           /* record level, monitor level, AUX IN, processed L / R */
#define OFF_EXP2_JACK   0x024
#define OFF_EXT_FN      0x025           /* +2 per switch */
#define OFF_FS_GROUP    0x02F
#define OFF_EXP_MAX     0x064
#define OFF_EXP_MIN     0x068
#define OFF_EXP_TOE     0x06C
#define OFF_MIDI_CH     0x070           /* i16 x 4 */
#define OFF_MIDI        0x078           /* source, clock source, clock out */
#define OFF_EQ_ON       0x07B
#define OFF_EQ          0x07C
#define OFF_LOOP        0x0FC           /* record, play, place, undo */
#define OFF_DRUM        0x106           /* u16 genre, u16 pattern, u16 BPM, sync, volume */
#define OFF_USB_MODE    0x230
#define OFF_AUX_LEVEL   0x232
#define OFF_TEMPO_MODE  0x233
#define OFF_BPM         0x234

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void putf32(uint8_t *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(u >> (8 * i));
}

static float eq_clamp(int k, float v)
{
    const struct eq_param_range *r = &eq_param_ranges[k];
    if (!(v >= r->min))                     /* also NaN */
        return v < r->min ? r->min : r->def;
    return v > r->max ? r->max : v;
}

int globals_decode(const uint8_t *rec, struct globals *out)
{
    int ok = rec && rd16(rec) == GLOBALS_MAGIC;
    if (!ok)
        rec = globals_factory;
    memset(out, 0, sizeof *out);
    memcpy(out->raw, rec, GLOBALS_RECORD_SIZE);
    out->auto_cab = rec[OFF_AUTO_CAB] != 0;
    out->backlight = rec[OFF_BACKLIGHT] > 100 ? 100 : rec[OFF_BACKLIGHT];
    int level = (int8_t)rec[OFF_INPUT_LEVEL];
    out->input_level = (int8_t)(level < -20 ? -20 : level > 20 ? 20 : level);   /* FUN_00026C0C */
    out->input_mode = rec[OFF_INPUT_MODE] > 3 ? 3 : rec[OFF_INPUT_MODE];        /* FUN_0002DB68 */
    out->no_cab[0] = rec[OFF_NO_CAB] != 0;
    out->no_cab[1] = rec[OFF_NO_CAB + 1] != 0;
    out->eq_on = rec[OFF_EQ_ON] != 0;
    for (int k = 0; k < EQ_PARAMS; k++)
        out->eq[k] = eq_clamp(k, rdf32(rec + OFF_EQ + 4 * k));
    out->aux_level = rec[OFF_AUX_LEVEL] > 100 ? 50 : rec[OFF_AUX_LEVEL];      /* FUN_0001EC9C */
    out->fs_group = rec[OFF_FS_GROUP] != 0;
    out->exp2_jack = rec[OFF_EXP2_JACK] > 2 ? 0 : rec[OFF_EXP2_JACK];
    out->loop_rec = rec[OFF_LOOP] > 100 ? 100 : rec[OFF_LOOP];
    out->loop_play = rec[OFF_LOOP + 1] > 100 ? 100 : rec[OFF_LOOP + 1];
    out->loop_post = rec[OFF_LOOP + 2] != 0;
    out->loop_undo = rec[OFF_LOOP + 3] != 0;
    out->drum_genre = rd16(rec + OFF_DRUM) < 8 ? rd16(rec + OFF_DRUM) : 0;
    out->drum_pattern = rd16(rec + OFF_DRUM + 2);
    uint16_t dbpm = rd16(rec + OFF_DRUM + 4);
    out->drum_bpm = dbpm >= 40 && dbpm <= 300 ? dbpm : 120;
    out->drum_sync = rec[OFF_DRUM + 6] != 0;
    out->drum_volume = rec[OFF_DRUM + 7] > 100 ? 100 : rec[OFF_DRUM + 7];
    out->ext_fn[0] = (int8_t)rec[OFF_EXT_FN];
    out->ext_fn[1] = (int8_t)rec[OFF_EXT_FN + 2];
    for (int i = 0; i < 2; i++) {
        out->exp_max[i] = rds16(rec + OFF_EXP_MAX + 2 * i);
        out->exp_min[i] = rds16(rec + OFF_EXP_MIN + 2 * i);
        out->exp_toe[i] = rds16(rec + OFF_EXP_TOE + 2 * i);
    }
    out->tempo_mode = rec[OFF_TEMPO_MODE];
    int rl = (int8_t)rec[OFF_USB_REC], ml = (int8_t)rec[OFF_USB_REC + 1];     /* FUN_00026C0C clamps them */
    out->usb_rec_level = (int8_t)(rl < -20 ? -20 : rl > 20 ? 20 : rl);
    out->usb_mon_level = (int8_t)(ml < -20 ? -20 : ml > 20 ? 20 : ml);
    out->usb_rec_aux = rec[OFF_USB_REC + 2] != 0;
    out->usb_rec_wet[0] = rec[OFF_USB_REC + 3] != 0;
    out->usb_rec_wet[1] = rec[OFF_USB_REC + 4] != 0;
    out->usb_mode = rec[OFF_USB_MODE] != 0;                                    /* FUN_000361AC */
    uint16_t bpm = rd16(rec + OFF_BPM);
    out->bpm = bpm >= 40 && bpm <= 300 ? bpm : 0;
    for (int i = 0; i < 4; i++)             /* as stored: the original's readers do not clamp them */
        out->midi_channel[i] = rds16(rec + OFF_MIDI_CH + 2 * i);
    out->midi_source = rec[OFF_MIDI];
    out->clock_source = rec[OFF_MIDI + 1];
    out->clock_out = rec[OFF_MIDI + 2];
    return ok ? 0 : -1;
}

/* only the fields that differ from what `base` decodes to are written, so an unchanged record stays the same
   byte for byte (out-of-range values the original tolerates included) */
void globals_encode(const struct globals *g, const uint8_t *base, uint8_t out[GLOBALS_RECORD_SIZE])
{
    static struct globals b;
    if (globals_decode(base, &b) != 0)
        base = globals_factory;
    memmove(out, base, GLOBALS_RECORD_SIZE);
    if (g->auto_cab != b.auto_cab)
        out[OFF_AUTO_CAB] = g->auto_cab;
    if (g->backlight != b.backlight)
        out[OFF_BACKLIGHT] = g->backlight;
    if (g->input_level != b.input_level)
        out[OFF_INPUT_LEVEL] = (uint8_t)g->input_level;
    if (g->input_mode != b.input_mode)
        out[OFF_INPUT_MODE] = g->input_mode;
    for (int i = 0; i < 2; i++)
        if (g->no_cab[i] != b.no_cab[i])
            out[OFF_NO_CAB + i] = g->no_cab[i];
    if (g->eq_on != b.eq_on)
        out[OFF_EQ_ON] = g->eq_on;
    for (int k = 0; k < EQ_PARAMS; k++)
        if (g->eq[k] != b.eq[k])
            putf32(out + OFF_EQ + 4 * k, g->eq[k]);
    if (g->aux_level != b.aux_level)
        out[OFF_AUX_LEVEL] = g->aux_level;
    if (g->fs_group != b.fs_group)
        out[OFF_FS_GROUP] = g->fs_group;
    if (g->exp2_jack != b.exp2_jack)
        out[OFF_EXP2_JACK] = g->exp2_jack;
    const uint8_t loop[4] = { g->loop_rec, g->loop_play, g->loop_post, g->loop_undo };
    const uint8_t was[4] = { b.loop_rec, b.loop_play, b.loop_post, b.loop_undo };
    for (int i = 0; i < 4; i++)
        if (loop[i] != was[i])
            out[OFF_LOOP + i] = loop[i];
    if (g->drum_genre != b.drum_genre)
        put16(out + OFF_DRUM, g->drum_genre);
    if (g->drum_pattern != b.drum_pattern)
        put16(out + OFF_DRUM + 2, g->drum_pattern);
    if (g->drum_bpm != b.drum_bpm)
        put16(out + OFF_DRUM + 4, g->drum_bpm);
    if (g->drum_sync != b.drum_sync)
        out[OFF_DRUM + 6] = g->drum_sync;
    if (g->drum_volume != b.drum_volume)
        out[OFF_DRUM + 7] = g->drum_volume;
    for (int i = 0; i < 2; i++)
        if (g->ext_fn[i] != b.ext_fn[i])
            out[OFF_EXT_FN + 2 * i] = (uint8_t)g->ext_fn[i];
    for (int i = 0; i < 2; i++) {
        if (g->exp_max[i] != b.exp_max[i])
            put16(out + OFF_EXP_MAX + 2 * i, (uint16_t)g->exp_max[i]);
        if (g->exp_min[i] != b.exp_min[i])
            put16(out + OFF_EXP_MIN + 2 * i, (uint16_t)g->exp_min[i]);
        if (g->exp_toe[i] != b.exp_toe[i])
            put16(out + OFF_EXP_TOE + 2 * i, (uint16_t)g->exp_toe[i]);
    }
    const uint8_t usb[5] = { (uint8_t)g->usb_rec_level, (uint8_t)g->usb_mon_level, g->usb_rec_aux, g->usb_rec_wet[0],
                             g->usb_rec_wet[1] };
    const uint8_t usb_was[5] = { (uint8_t)b.usb_rec_level, (uint8_t)b.usb_mon_level, b.usb_rec_aux, b.usb_rec_wet[0],
                                 b.usb_rec_wet[1] };
    for (int i = 0; i < 5; i++)
        if (usb[i] != usb_was[i])
            out[OFF_USB_REC + i] = usb[i];
    if (g->usb_mode != b.usb_mode)
        out[OFF_USB_MODE] = g->usb_mode;
    if (g->tempo_mode != b.tempo_mode)
        out[OFF_TEMPO_MODE] = g->tempo_mode;
    if (g->bpm != b.bpm)
        put16(out + OFF_BPM, g->bpm ? g->bpm : 0xFFFFu);
    for (int i = 0; i < 4; i++)
        if (g->midi_channel[i] != b.midi_channel[i])
            put16(out + OFF_MIDI_CH + 2 * i, (uint16_t)g->midi_channel[i]);
    const uint8_t midi[3] = { g->midi_source, g->clock_source, g->clock_out };
    const uint8_t midi_was[3] = { b.midi_source, b.clock_source, b.clock_out };
    for (int i = 0; i < 3; i++)
        if (midi[i] != midi_was[i])
            out[OFF_MIDI + i] = midi[i];
}
