#include "app/app.h"
#include "app/recovery.h"
#include "engine/audio_engine.h"
#include "engine/cabir.h"
#include "model/modulation.h"
#include "engine/drums.h"
#include "engine/latency.h"
#include "engine/looper.h"
#include "engine/pools.h"
#include "input/input.h"
#include "midi/midi.h"
#include "model/drum_user.h"
#include "model/globals.h"
#include "model/rig.h"
#include "model/settings.h"
#include "model/sysmon.h"
#include "platform/audio.h"
#include "platform/platform.h"
#include "platform/safety.h"
#include "platform/touch.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "usb/usb_device.h"
#include "editor/editor.h"
#include "lvgl.h"                     /* lv_snprintf: there is no printf on the pedal */
#include <string.h>
#include "platform/sdram.h"

#define MAX_EVENTS 16

static struct rig rig SDRAM_BSS;
static struct settings settings;
static struct globals globals;

static const struct midi_handlers midi_handlers = { ui_midi_program, ui_midi_control, ui_midi_tempo, editor_feed };

/*
 * rig_init runs before the settings are read (they live in its preset source), so its texts are in the reference
 * language (English): the notice it left and, when it started without a preset (boot != 0: safe mode, no
 * library), the name it gave the empty one. Written again here in the language of the settings.
 */
static void boot_texts(int boot)
{
    static const uint16_t msgs[] = { MSG_RIG_NO_LIBRARY, MSG_RIG_NO_MEMORY_CHAIN, MSG_RIG_NO_LIBRARY_NAME,
                                     MSG_RIG_SAFE_MODE };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        const char *ref = i18n_texts[LANG_EN][msgs[i]];
        if (strcmp(rig.notice, ref) == 0)
            lv_snprintf(rig.notice, sizeof rig.notice, "%s", T(msgs[i]));
        if (boot && strcmp(rig.preset.name, ref) == 0)
            lv_snprintf(rig.preset.name, sizeof rig.preset.name, "%s", T(msgs[i]));
    }
}

void app_init(void)
{
    plat_init();
    pools_init();
    engine_init(&rig.chain, plat_core_hz());
    looper_init();
    drums_init(plat_drum_pack());
    input_init();
    int boot = rig_init(&rig, recovery_avoid());
    int started_safe = boot == 1;
    settings_load(&settings, &rig.src);
    i18n_set(settings.language);
    boot_texts(boot);
    settings_apply(&settings);
    if (!cabir_in_use())
        rig_rebuild(&rig);                  /* the preset was loaded with waveod's Cab / IR engine */
    globals_load(&globals, &rig.src);
    usb_device_init(globals.usb_mode ? USB_MODE_2X2 : USB_MODE_6X4);
    midi_init(&midi_handlers);              /* configured by globals_apply */
    rig_set_global_tempo(&rig, globals.tempo_mode ? globals.bpm : 0);
    engine_set_master(input_pot(POT_MASTER));
    audio_start(engine_audio);
    globals_apply(&globals);                /* after audio_start: the input mode is on the shift register */
    drum_user_init(&settings, &globals);    /* MEUS RITMOS: the pattern chosen last */
    ui_init(&rig, &settings, &globals);
    editor_init(&rig, &globals, &settings);
    recovery_report(&rig, started_safe);

    /* the core clock (600 MHz, set like the original when the bootloader left less) and the touch panel */
    uint32_t mhz = (plat_core_hz() + 500000u) / 1000000u;
    if (mhz) {
        char text[64];
        lv_snprintf(text, sizeof text, T(touch_ready() ? MSG_SYS_BOOT_TOUCH_OK : MSG_SYS_BOOT_NO_TOUCH),
                    (unsigned)mhz);
        ui_notice(text);
    }
    safety_arm();                           /* from here on the main loop must keep running */
}

struct settings *app_settings(void)
{
    return &settings;
}

struct rig *app_rig(void)
{
    return &rig;
}

void app_step(void)
{
    safety_feed();
    struct input_event ev[MAX_EVENTS];
    int n = input_poll(ev, MAX_EVENTS);
    for (int i = 0; i < n; i++)
        ui_input(&ev[i]);
    engine_set_master(input_pot(POT_MASTER));
    editor_set_master(input_pot(POT_MASTER));
    usb_device_poll();
    midi_set_tempo(rig_tempo(&rig));
    midi_poll();
    editor_poll();
    globals_poll(&globals, &rig.src);
    settings_poll(&settings, &rig.src);
    latency_poll();
    chain_poll(&rig.chain);
    modulation_poll(&rig);
    sysmon_poll();
    ui_tick();
}
