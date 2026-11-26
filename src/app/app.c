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
#include "ui/ui.h"
#include "usb/usb_device.h"
#include "editor/editor.h"
#include <string.h>
#include "platform/sdram.h"

#define MAX_EVENTS 16

static struct rig rig SDRAM_BSS;
static struct settings settings;
static struct globals globals;

static const struct midi_handlers midi_handlers = { ui_midi_program, ui_midi_control, ui_midi_tempo, editor_feed };

void app_init(void)
{
    plat_init();
    pools_init();
    engine_init(&rig.chain, plat_core_hz());
    looper_init();
    drums_init(plat_drum_pack());
    input_init();
    int started_safe = rig_init(&rig, recovery_avoid()) == 1;
    settings_load(&settings, &rig.src);
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
        char text[48] = "waveod \xC2\xB7 CPU ";
        char *p = text + strlen(text);
        if (mhz >= 100)
            *p++ = (char)('0' + mhz / 100 % 10);
        if (mhz >= 10)
            *p++ = (char)('0' + mhz / 10 % 10);
        *p++ = (char)('0' + mhz % 10);
        const char *tail = touch_ready() ? " MHz \xC2\xB7 toque OK" : " MHz \xC2\xB7 sem toque";
        memcpy(p, tail, strlen(tail) + 1);
        ui_notice(text);
    }
    safety_arm();                           /* from here on the main loop must keep running */
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
