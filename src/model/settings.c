#include "model/settings.h"
#include "engine/audio_engine.h"
#include "engine/cabir.h"
#include "fw/settings_record.h"
#include "model/drum_user.h"
#include "platform/display.h"
#include "platform/time.h"
#include <string.h>

/* stored layout (little endian, version 1 of the record) */
struct stored {
    uint8_t brightness;
    uint8_t tuner_mute;
    uint8_t touch_cursor;
    uint8_t scene_mode;                     /* was reserved (0) */
    uint16_t a4;
    uint8_t cab_original;                   /* added: older records are 6 bytes, read as 0 */
    uint8_t spill;                          /* the choice + 1; 0 (older records) = the default */
    uint8_t tuner_strobe;                   /* added: 0 (older records) = the needle */
    uint8_t scene_manual;                   /* added: 0 (older records) = AUTO */
    uint8_t drum_user;                      /* added: 0 (older records) = the bank's pattern */
};

#define STORED_V1 6                         /* the first layout, up to a4 */
#define SPILL_DEFAULT 3

const uint16_t settings_spill_ms[SETTINGS_SPILL_CHOICES] = { 0, 2000, 5000, 10000, 20000 };

static const struct settings defaults = { 60, 440, 1, 0, 0, 0, SPILL_DEFAULT, 0, 0, 0 };

#define SAVE_AFTER_MS 2000

static uint8_t touched;
static uint32_t touched_at;
static uint32_t generation;

uint32_t settings_generation(void)
{
    return generation;
}

static void to_stored(const struct settings *s, struct stored *o)
{
    memset(o, 0, sizeof *o);
    o->brightness = s->brightness;
    o->tuner_mute = s->tuner_mute;
    o->touch_cursor = s->touch_cursor;
    o->scene_mode = s->scene_mode;
    o->a4 = s->a4;
    o->cab_original = s->cab_original;
    o->spill = s->spill == SPILL_DEFAULT ? 0 : (uint8_t)(s->spill + 1);
    o->tuner_strobe = s->tuner_strobe;
    o->scene_manual = s->scene_manual;
    o->drum_user = s->drum_user;
}

void settings_load(struct settings *s, const struct preset_source *src)
{
    struct stored o;
    *s = defaults;
    if (settings_record_read(src, &o, sizeof o) < STORED_V1)
        return;
    if (o.brightness >= 10 && o.brightness <= 100)
        s->brightness = o.brightness;
    if (o.a4 >= 430 && o.a4 <= 450)
        s->a4 = o.a4;
    s->tuner_mute = o.tuner_mute != 0;
    s->touch_cursor = o.touch_cursor != 0;
    s->scene_mode = o.scene_mode == 1;
    s->cab_original = o.cab_original == 1;
    if (o.spill >= 1 && o.spill <= SETTINGS_SPILL_CHOICES)
        s->spill = (uint8_t)(o.spill - 1);
    s->tuner_strobe = o.tuner_strobe == 1;
    s->scene_manual = o.scene_manual == 1;
    s->drum_user = o.drum_user <= DRUM_USER_SLOTS ? o.drum_user : 0;
}

void settings_apply(const struct settings *s)
{
    generation++;
    display_backlight(s->brightness);
    cabir_set_in_use(!s->cab_original);         /* the chain follows at its next build */
    engine_set_spill(settings_spill_ms[s->spill < SETTINGS_SPILL_CHOICES ? s->spill : SPILL_DEFAULT]);
}

int settings_save(const struct settings *s, struct preset_source *src)
{
    struct stored now, before;
    to_stored(s, &now);
    int stored = settings_record_read(src, &before, sizeof before);
    if (stored >= STORED_V1 && memcmp(&now, &before, sizeof now) == 0)
        return 0;                           /* unchanged */
    if (stored < 0 && s->brightness == defaults.brightness && s->a4 == defaults.a4 &&
        s->tuner_mute == defaults.tuner_mute && s->touch_cursor == defaults.touch_cursor &&
        s->scene_mode == defaults.scene_mode && s->cab_original == defaults.cab_original &&
        s->spill == defaults.spill && s->tuner_strobe == defaults.tuner_strobe &&
        s->scene_manual == defaults.scene_manual && s->drum_user == defaults.drum_user)
        return 0;                           /* nothing stored and nothing to store */
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    int err = settings_record_write(src, &now, sizeof now);
    engine_flash_end(flash);
    return err;
}

void settings_touch(void)
{
    touched = 1;
    touched_at = time_ms();
}

void settings_poll(const struct settings *s, struct preset_source *src)
{
    if (touched && time_ms() - touched_at >= SAVE_AFTER_MS) {
        touched = 0;
        settings_save(s, src);
    }
}