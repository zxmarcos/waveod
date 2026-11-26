/*
 * What MIDI does on the pedal: midi/midi.c hands over the program changes, control changes and measured
 * tempos it accepted. The control changes are the original's global event table (0x45190, event ids
 * 0x10 | 0xB0 << 8 | CC << 16, handlers FUN_0002CBDC..FUN_00030CBC):
 *
 *   7       preset volume (values above 100 count as 100, here and below)
 *   11      EXP 1 position, its current A / B group      13      EXP 1 A (< 64) / B
 *   16 18 20  quick knob 1-3 to value / 100 of its range (BPM: 40 + value x 2.6)
 *   17 19 21  quick knob 1-3 a step down (< 64) / up
 *   22 / 23 bank up / down             24 / 25 preset up / down
 *   26 27 28  the original's bank screen: the presets list, open (>= 64) / back to the main screen
 *   29      preset mode (< 64) / stomp mode
 *   30      scene 1-4 (values 0-3; waveod's, not in the original)
 *   31      footswitch mode: 0 preset, 1 stomp, 2 scenes, 3-127 the next one (waveod's)
 *   43-54   block at chain position 1-12 off (< 64) / on
 *   58 59 92  tuner, looper, drum machine: open (>= 64) / back to the main screen
 *   60      looper record (auto record: arm) / overdub   61  auto record on (and armed)
 *   62      looper stop (< 64) / play        63  undo / redo        64  erase
 *   65 / 66 looper record / play level      67  looper POST (< 64) / PRE
 *   69      tempo 40-300 (with CC 68 as the high bits)  70  tap tempo
 *   71-74   CTRL 1-4: what stomp mode's footswitches A-D do
 *   93      drum machine off (< 64) / on    94  drum pattern 0-99 (100-119: MEUS RITMOS 1-20, waveod
 * Tempos go through model/tempo.c (the clock source decides). Settings changed here are written after a pause.
 * The CCs that change screens are dropped while the start-up animation runs.
 *
 * waveod's: a CC with links in the preset's control table (CK_MIDI) does what they say instead (model/control.c),
 * and while a screen learns a key (ui_learn) a CC is that key, nothing else.
 */
#include "model/control.h"
#include "engine/drums.h"
#include "engine/looper.h"
#include "model/drum_names.h"
#include "model/drum_user.h"
#include "model/tempo.h"
#include "ui/boot.h"
#include "ui/footswitch.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

static int percent(int v)
{
    return v > 100 ? 100 : v;
}

static void load(int index)
{
    ui_load_preset((index % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT);
}

/* CC 58 / 59 / 92 (FUN_0002CF18 and the others) */
static void open_or_home(struct ui_ctx *ui, enum screen_id id, int value)
{
    if (value < 64)
        ui_tab(ui_mode_tab());
    else if (ui->current != id)
        ui_go(id);
}

/* FUN_0002CDBC (absolute) / FUN_0002CD0C (relative) */
static void quick_knob(struct ui_ctx *ui, int i, int value, int relative)
{
    struct rig *r = ui->rig;
    const struct quick_knob *q = &ui->assign.quick[i];
    int step = value < 64 ? -1 : 1;
    if (q->slot == QUICK_VOLUME) {
        rig_set_volume(r, relative ? r->preset.volume + step : percent(value));
    } else if (q->slot == QUICK_BPM) {
        int bpm = relative ? r->preset.bpm + step : percent(value) * 260 / 100 + 40;
        tempo_set(r, ui->globals, bpm < 40 ? 40 : bpm > 300 ? 300 : bpm, TEMPO_KNOB);
    } else if (q->slot >= 0 && relative) {
        rig_param_nudge(r, q->slot, q->k, step);
    } else if (q->slot >= 0) {
        struct rig_param p;
        if (rig_param(r, q->slot, q->k, &p) == 0)
            rig_param_set(r, q->slot, q->k, p.def.min + (p.def.max - p.def.min) * ((float)percent(value) / 100.0f));
    }
}

static void looper(struct ui_ctx *ui, int cc, int value)
{
    struct globals *g = ui->globals;
    struct looper_status st;
    looper_status(&st);
    switch (cc) {
    case 60:                                /* FUN_00030C80 */
        if (st.state == LOOP_EMPTY && looper_auto())
            looper_arm(1);
        else if (st.state == LOOP_EMPTY)
            looper_command(LOOP_CMD_REC);
        else if (st.state == LOOP_PLAY)
            looper_command(LOOP_CMD_DUB);
        return;
    case 61:                                /* FUN_00030BE2 */
        if (st.state == LOOP_EMPTY || st.state == LOOP_PLAY)
            looper_arm(1);
        looper_set_auto(1);
        return;
    case 62:                                /* FUN_00030C0E */
        if (value < 64)
            looper_command(LOOP_CMD_STOP);
        else if (st.state == LOOP_REC || st.state == LOOP_DUB || st.state == LOOP_STOPPED)
            looper_command(LOOP_CMD_PLAY);
        return;
    case 63:
        if (st.layer)
            looper_undo();
        return;
    case 64:
        looper_command(LOOP_CMD_CLEAR);
        return;
    case 65:
        g->loop_rec = (uint8_t)percent(value);
        break;
    case 66:
        g->loop_play = (uint8_t)percent(value);
        break;
    case 67:
        g->loop_post = value < 64;
        break;
    }
    looper_set(g->loop_rec, g->loop_play, g->loop_post, g->loop_undo);
    globals_touch();
}

static void drums(struct ui_ctx *ui, int cc, int value)
{
    struct globals *g = ui->globals;
    if (cc == 93) {
        drums_play(value >= 64);
        return;
    }
    if (cc == 94) {                         /* 0-99 the bank's patterns, 100-119 MEUS RITMOS 1-20 */
        if (value >= DRUM_PATTERNS) {
            drum_user_select(value - DRUM_PATTERNS);
            ui_changed();
            return;
        }
        drum_user_deselect();
        globals_set_drum_pattern(g, value);
    } else {
        g->drum_volume = (uint8_t)percent(value);
        drums_set_volume(g->drum_volume);
    }
    globals_touch();
}

void ui_midi_control(int cc, int value)
{
    struct ui_ctx *ui = ui_context();
    struct rig *r = ui->rig;
    if (ui->learned && CK_MIDI_CC_OK(cc) && !boot_running()) {
        ui_learn_fn fn = ui->learned;
        ui->learned = 0;
        fn(ui, CK_MIDI + cc, 0);
        return;
    }
    if (CK_MIDI_CC_OK(cc) && control_count(r, CK_MIDI + cc, CM_ALL, 0, CA_NONE)) {
        if (control_midi(r, cc, value, footswitch_link, ui))
            ui_changed();
        return;
    }
    if (cc >= 43 && cc <= 54) {             /* FUN_0002CED2 */
        int pos = cc - 43;
        if (pos < r->preset.count) {
            int slot = r->preset.order[pos];
            if (rig_slot_used(r, slot) && r->preset.slot[slot].on != (value >= 64))
                rig_toggle(r, slot);
        }
    } else if (cc >= 60 && cc <= 67) {
        looper(ui, cc, value);
    } else if (cc >= 71 && cc <= 74) {
        footswitch_run(ui, FSF_CTRL1 + cc - 71);   /* stomp mode's key A-D */
    } else if (cc >= 93 && cc <= 95) {
        drums(ui, cc, value);
    } else {
        if (boot_running() && ((cc >= 26 && cc <= 29) || cc == 31 || cc == 58 || cc == 59 || cc == 92))
            return;
        switch (cc) {
        case 7:  rig_set_volume(r, percent(value)); break;
        case 11: rig_exp(r, r->preset.exp_group, (float)percent(value) / 100.0f); break;
        case 13: rig_exp_group(r, value >= 64); break;
        case 16: case 18: case 20: quick_knob(ui, (cc - 16) / 2, value, 0); break;
        case 17: case 19: case 21: quick_knob(ui, (cc - 17) / 2, value, 1); break;
        case 22: load(r->index + 4); return;
        case 23: load(r->index - 4); return;
        case 24: load(r->index + 1); return;
        case 25: load(r->index - 1); return;
        case 26: case 27: case 28: open_or_home(ui, SCR_PRESETS, value); return;
        case 29: ui_set_mode(value >= 64 ? FS_MODE_STOMP : FS_MODE_PRESET); return;
        case 31: ui_set_mode(value <= FS_MODE_SCENE ? value : (footswitch_mode(ui) + 1) % 3); return;
        case 30: if (value < SCENE_COUNT) footswitch_scene(ui, value); return;
        case 58: open_or_home(ui, SCR_TUNER, value); return;
        case 59: open_or_home(ui, SCR_LOOPER, value); return;
        case 92: open_or_home(ui, SCR_DRUM, value); return;
        case 69: tempo_set(r, ui->globals, value < 40 ? 40 : value > 300 ? 300 : value, TEMPO_TAP); break;
        case 70: footswitch_tap(ui); return;
        default: return;
        }
    }
    ui_changed();
}

void ui_midi_program(int preset)
{
    ui_load_preset(preset);
}

void ui_midi_tempo(int bpm)
{
    struct ui_ctx *ui = ui_context();
    if (tempo_set(ui->rig, ui->globals, bpm, TEMPO_MIDI_CLOCK))
        ui_changed();
}
