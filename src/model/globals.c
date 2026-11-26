#include "model/globals.h"
#include "engine/audio_engine.h"
#include "engine/drums.h"
#include "engine/looper.h"
#include "midi/midi.h"
#include "model/drum_names.h"
#include "platform/audio.h"
#include "platform/time.h"
#include "usb/usb_device.h"
#include <string.h>
#include "platform/sdram.h"

void globals_load(struct globals *g, const struct preset_source *src)
{
    globals_decode(presets_globals(src), g);
}

#define SAVE_AFTER_MS 2000

static uint32_t generation;
static uint8_t touched;
static uint32_t touched_at;

uint32_t globals_generation(void)
{
    return generation;
}

void globals_apply(const struct globals *g)
{
    generation++;
    audio_set_input_mode(g->input_mode);
    engine_set_input_level(g->input_level);
    engine_set_aux_level(g->aux_level);
    engine_set_no_cab(g->no_cab[0], g->no_cab[1]);
    engine_set_usb(g->usb_rec_level, g->usb_mon_level, g->usb_rec_aux, g->usb_rec_wet[0], g->usb_rec_wet[1]);
    usb_device_set_mode(g->usb_mode ? USB_MODE_2X2 : USB_MODE_6X4);
    engine_set_global_eq(g->eq_on, g->eq);
    looper_set(g->loop_rec, g->loop_play, g->loop_post, g->loop_undo);
    drums_set_pattern(globals_drum_pattern(g));
    drums_set_volume(g->drum_volume);
    if (!g->drum_sync)
        drums_set_bpm(g->drum_bpm);
    struct midi_config m = { { g->midi_channel[0], g->midi_channel[1] }, g->midi_source, g->clock_source,
                             g->clock_out };
    midi_configure(&m);
}

int globals_save(struct globals *g, struct preset_source *src)
{
    static uint8_t rec[GLOBALS_RECORD_SIZE] SDRAM_BSS;
    const uint8_t *stored = presets_globals(src);
    globals_encode(g, g->raw, rec);         /* over what was loaded: the editor may have set raw fields */
    if (memcmp(rec, stored ? stored : globals_factory, GLOBALS_RECORD_SIZE) == 0)
        return 0;                           /* unchanged (or factory values with nothing stored yet) */
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    int err = presets_write_globals(src, rec);
    engine_flash_end(flash);
    if (!err)
        memcpy(g->raw, rec, GLOBALS_RECORD_SIZE);
    return err;
}

void globals_touch(void)
{
    touched = 1;
    touched_at = time_ms();
}

void globals_poll(struct globals *g, struct preset_source *src)
{
    if (touched && time_ms() - touched_at >= SAVE_AFTER_MS) {
        touched = 0;
        globals_save(g, src);
    }
}

void globals_eq_nudge(struct globals *g, int k, int steps)
{
    if (k < 0 || k >= EQ_PARAMS || !steps)
        return;
    const struct eq_param_range *r = &eq_param_ranges[k];
    float step = r->step < 0 ? -r->step : r->step;
    if (r->max - r->min > 1000.0f) {        /* frequencies: steps of about 2 % */
        float f = g->eq[k], d = f * 0.02f;
        step = d < 1.0f ? 1.0f : (float)(int)(d + 0.5f);
    }
    float v = g->eq[k] + (float)steps * step;
    if (step >= 1.0f)
        v = (float)(int)(v + (v >= 0 ? 0.5f : -0.5f));
    else
        v = (float)(int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f)) * 0.1f;     /* Q in tenths */
    g->eq[k] = v < r->min ? r->min : v > r->max ? r->max : v;
}

int globals_drum_pattern(const struct globals *g)
{
    int genre = g->drum_genre < DRUM_GENRES ? g->drum_genre : 0;
    int k = g->drum_pattern < drum_genre_count[genre] ? g->drum_pattern : 0;
    return drum_genre_first(genre) + k;
}

void globals_set_drum_pattern(struct globals *g, int pattern)
{
    pattern = (pattern % DRUM_PATTERNS + DRUM_PATTERNS) % DRUM_PATTERNS;
    g->drum_genre = (uint16_t)drum_genre_of(pattern);
    g->drum_pattern = (uint16_t)(pattern - drum_genre_first(g->drum_genre));
    drums_set_pattern(pattern);
}
