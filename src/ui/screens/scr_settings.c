/*
 * Settings (GLOBAL), laid out like the original's Global Settings: cards of three by two (two pages), each a
 * group of settings with its icon, name and what it is set to; below them the selected card's settings in three
 * cells, on the knobs (‹ › for more than three).
 *
 * Two kinds of settings: the original's global settings (input, AUX IN, cabinets on the outputs, global EQ;
 * record 0, shared with the stock firmware) and waveod's own (tuner, display, touch).
 *
 * Controls: MENU turns = card, knobs 1-3 = its settings, MENU press = what the card opens (tuner, EQ,
 * calibration, controls, latency) or its next three settings, EXIT or GLOBAL = back. Touch: tap a card to
 * select it, tap it again = MENU press; swipe the cards sideways = the other page; tap a setting to open / step
 * it, drag it sideways to change it; ‹ › = its other settings.
 * Changes apply at once and are written to the flash when the screen is left.
 */
#include "engine/latency.h"
#include "model/globals.h"
#include "platform/audio.h"
#include "platform/flash.h"
#include "model/settings.h"
#include "model/sysmon.h"
#include "platform/platform.h"
#include "platform/safety.h"
#include "ui/diag_panel.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#ifndef WAVEOD_VERSION
#define WAVEOD_VERSION "dev"
#endif

#define COLS      3
#define ROWS      2
#define PER_PAGE  (COLS * ROWS)
#define GAP       6
#define GRID_Y    (UI_TOPBAR_H + 6)
#define CARD_W    ((UI_W - 2 * UI_PAD - (COLS - 1) * GAP) / COLS)
#define CARD_H    52
#define PAGE_Y    (GRID_Y + ROWS * CARD_H + (ROWS - 1) * GAP + 6)
#define CELLS_Y   (PAGE_Y + 8)
#define CELLS_H   (UI_H - CELLS_Y)
#define ARROW_W   28                        /* ‹ › beside the cells */
#define CELL_W    ((UI_W - 2 * ARROW_W) / 3)
#define ICON_X    10
#define TEXT_X    40
#define DRAG_PX   24                        /* finger travel per step */
#define C_FRAME   0x2A2926
#define C_SEL_BG  0x161514

enum row {
    R_TUNER, R_A4, R_TUNER_OUT, R_TUNER_VIEW, R_INPUT_LEVEL, R_INPUT_MODE, R_AUX, R_CABS, R_USB_MODE, R_USB_REC, R_USB_WET,
    R_USB_AUX, R_USB_MON, R_MIDI_SOURCE, R_MIDI_TRS, R_MIDI_USB, R_CLOCK_SOURCE, R_CLOCK_OUT, R_EQ, R_EQ_EDIT,
    R_TEMPO, R_EXP1_CAL, R_EXP2_CAL, R_CALIBRATE, R_EXP2_JACK, R_EXT1, R_EXT2, R_CONTROLS, R_CAB_ENGINE, R_SPILL, R_SCENE_EDIT,
    R_BRIGHTNESS, R_CURSOR, R_LATENCY, R_CPU_TEMP, R_BOOT_LOCK, R_DIAG, R_SAFETY_TEST, R_VERSION, R_COUNT
};

static const char *const titles[R_COUNT] = {
    [R_TUNER] = "AFINADOR",
    [R_A4] = "REFER\xC3\x8ANCIA A4",
    [R_TUNER_OUT] = "SOM AO AFINAR",
    [R_TUNER_VIEW] = "VISOR",
    [R_INPUT_LEVEL] = "N\xC3\x8DVEL IN",
    [R_INPUT_MODE] = "ENTRADA",
    [R_AUX] = "N\xC3\x8DVEL AUX IN",
    [R_CABS] = "CAB NAS SA\303\215" "DAS",
    [R_USB_MODE] = "CANAIS",
    [R_USB_REC] = "N\xC3\x8DVEL GRAV.",
    [R_USB_WET] = "GRAVA\xC3\x87\xC3\x83O 1 / 2",
    [R_USB_AUX] = "AUX NA GRAV.",
    [R_USB_MON] = "RETORNO DO PC",
    [R_MIDI_SOURCE] = "ENTRADA",
    [R_MIDI_TRS] = "CANAL DO TRS",
    [R_MIDI_USB] = "CANAL DO USB",
    [R_CLOCK_SOURCE] = "FONTE DO TEMPO",
    [R_CLOCK_OUT] = "ENVIAR CLOCK",
    [R_EQ] = "EQ GLOBAL",
    [R_EQ_EDIT] = "BANDAS",
    [R_TEMPO] = "TEMPO GLOBAL",
    [R_EXP1_CAL] = "EXP 1",
    [R_EXP2_CAL] = "EXP 2",
    [R_CALIBRATE] = "CALIBRAR",
    [R_EXP2_JACK] = "JACK EXP 2",
    [R_EXT1] = "FS EXT. 1",
    [R_EXT2] = "FS EXT. 2",
    [R_CONTROLS] = "LIGA\xC3\x87\xC3\x95" "ES",
    [R_BRIGHTNESS] = "BRILHO",
    [R_CURSOR] = "MARCA DE TOQUE",
    [R_CAB_ENGINE] = "MOTOR CAB / IR",
    [R_SPILL] = "RASTRO NA TROCA",
    [R_SCENE_EDIT] = "KNOBS NA CENA",
    [R_LATENCY] = "TESTE",
    [R_CPU_TEMP] = "TEMP. CPU",
    [R_BOOT_LOCK] = "BOOTLOADER",
    [R_DIAG] = "DIAGN\xC3\x93STICO",
    [R_SAFETY_TEST] = "PROTE\xC3\x87\xC3\x83O",
    [R_VERSION] = "VERS\xC3\x83O",
};

/* the cards: name, icon, settings (-1 ends), what MENU opens (-1 = the next settings) */
#define CARD_ROWS 6
static const struct card_def {
    const char *name;
    const lv_image_dsc_t *icon;
    int8_t rows[CARD_ROWS];
    int8_t open;
} cards[] = {
    { "IN / OUT", &icon_g_io, { R_INPUT_MODE, R_INPUT_LEVEL, R_CABS, R_AUX, -1 }, -1 },
    { "AFINADOR", &icon_g_tuner, { R_TUNER, R_A4, R_TUNER_OUT, R_TUNER_VIEW, -1 }, R_TUNER },
    { "\xC3\x81UDIO USB", &icon_g_usb, { R_USB_MODE, R_USB_REC, R_USB_MON, R_USB_WET, R_USB_AUX, -1 }, -1 },
    { "PEDAIS EXP", &icon_g_exp, { R_EXP1_CAL, R_EXP2_CAL, R_CALIBRATE, -1 }, R_CALIBRATE },
    { "EQ GLOBAL", &icon_g_eq, { R_EQ, R_EQ_EDIT, -1 }, R_EQ_EDIT },
    { "EXP 2 / FS EXT.", &icon_g_fs, { R_EXP2_JACK, R_EXT1, R_EXT2, -1 }, -1 },
    { "MIDI", &icon_g_midi, { R_MIDI_SOURCE, R_MIDI_TRS, R_MIDI_USB, R_CLOCK_SOURCE, R_CLOCK_OUT, -1 }, -1 },
    { "PRESETS", &icon_g_presets, { R_TEMPO, R_SPILL, R_SCENE_EDIT, R_CAB_ENGINE, -1 }, -1 },
    { "CONTROLES", &icon_g_controls, { R_CONTROLS, -1 }, R_CONTROLS },
    { "TELA", &icon_g_display, { R_BRIGHTNESS, R_CURSOR, -1 }, -1 },
    { "LAT\xC3\x8ANCIA", &icon_g_latency, { R_LATENCY, -1 }, R_LATENCY },
    { "SISTEMA", &icon_g_system, { R_CPU_TEMP, R_BOOT_LOCK, R_DIAG, R_SAFETY_TEST, R_VERSION, -1 }, -1 },
};
#define CARDS ((int)(sizeof cards / sizeof cards[0]))
#define PAGES ((CARDS + PER_PAGE - 1) / PER_PAGE)

static const char *const input_modes[3] = { "VIOL\xC3\x83O", "GUITARRA", "LINHA" };
static const char *const exp2_jacks[3] = { "PEDAL", "1 FOOTSWITCH", "2 FOOTSWITCHES" };

/* cabinets on the outputs, as one choice: index = no cab on L | no cab on R << 1 */
static const char *const cab_outputs[4] = { "L E R", "S\xC3\x93 EM R", "S\xC3\x93 EM L", "NENHUMA" };
static const uint8_t cab_cycle[4] = { 0, 2, 1, 3 };     /* order of the choices: both, L only, R only, none */

/* USB recording channels 1 / 2: index = processed L | processed R << 1 */
static const char *const usb_wet[4] = { "SECA / SECA", "PROC. / SECA", "SECA / PROC.", "PROC. / PROC." };

/* MIDI (midi/midi.h): the original editor's lists */
static const char *const midi_sources[3] = { "S\xC3\x93 TRS", "S\xC3\x93 USB", "TRS E USB" };
static const char *const clock_sources[5] = { "INTERNO", "CLOCK DO TRS", "CLOCK DO USB", "CLOCK EXTERNO",
                                              "INTERNO E CLOCK" };
static const char *const clock_outs[4] = { "DESLIGADO", "TRS", "USB", "TRS E USB" };

/* deliberate failures (platform/safety.h): the pedal restarts and tells what happened; 0 = none chosen */
static const char *const safety_tests[4] = { "ESCOLHA", "FALHA", "TRAVAR", "TRAVAR SEM IRQ" };

/* the bootloader's write protection in the flash chip (platform/flash.h enum flash_lock) */
static const char *const boot_lock_names[4] = { "SEM PROTE\xC3\x87\xC3\x83O", "PROTEGIDO", "FALHOU", "N\xC3\x83O SUPORTADO" };

struct card_obj {
    lv_obj_t *box, *icon, *name, *status;
};

static struct {
    struct card_obj card[PER_PAGE];
    lv_obj_t *page_seg[PAGES];
    struct param_cell cell[3];
    lv_obj_t *cell_hit[3], *prev_hit, *next_hit, *prev, *next;
    int sel;                                /* selected card */
    int sub;                                /* its settings shown: from sub * 3 */
    int drag_start;
    int test;                               /* the safety test offered */
    int exp_seen[2];                        /* pedal values shown */
    uint32_t lat_seen;
    uint32_t temp_seen;                     /* sysmon's seq shown */
} s SDRAM_BSS;

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int card_rows(int c)
{
    int n = 0;
    while (n < CARD_ROWS && cards[c].rows[n] >= 0)
        n++;
    return n;
}

static int subs(int c)
{
    return (card_rows(c) + 2) / 3;
}

/* the setting in cell k of the selected card, -1 = none */
static int cell_row(int k)
{
    int i = s.sub * 3 + k;
    return i < card_rows(s.sel) ? cards[s.sel].rows[i] : -1;
}

/* ---------------------------------------------------------------- values */

static void change(struct ui_ctx *ui, int row, int d)
{
    struct settings *st = ui->settings;
    struct globals *g = ui->globals;
    switch (row) {
    case R_A4:
        st->a4 = (uint16_t)clamp(st->a4 + d, 430, 450);
        break;
    case R_TUNER_OUT:
        if (d)
            st->tuner_mute = !st->tuner_mute;
        break;
    case R_TUNER_VIEW:
        if (d)
            st->tuner_strobe = !st->tuner_strobe;
        break;
    case R_INPUT_LEVEL:
        g->input_level = (int8_t)clamp(g->input_level + d, -20, 20);
        globals_apply(g);
        break;
    case R_INPUT_MODE:
        g->input_mode = (uint8_t)((g->input_mode + 3 + d % 3) % 3);
        globals_apply(g);
        break;
    case R_AUX:
        g->aux_level = (uint8_t)clamp(g->aux_level + d, 0, 100);
        globals_apply(g);
        break;
    case R_CABS: {
        int now = g->no_cab[0] | g->no_cab[1] << 1, i = 0;
        while (cab_cycle[i] != now)
            i++;
        int next = cab_cycle[((i + d) % 4 + 4) % 4];
        g->no_cab[0] = (uint8_t)(next & 1);
        g->no_cab[1] = (uint8_t)(next >> 1);
        globals_apply(g);
        break;
    }
    case R_USB_MODE:                        /* the computer sees the pedal again with the other configuration */
        if (d) {
            g->usb_mode = !g->usb_mode;
            globals_apply(g);
        }
        break;
    case R_USB_REC:
        g->usb_rec_level = (int8_t)clamp(g->usb_rec_level + d, -20, 20);
        globals_apply(g);
        break;
    case R_USB_MON:
        g->usb_mon_level = (int8_t)clamp(g->usb_mon_level + d, -20, 20);
        globals_apply(g);
        break;
    case R_USB_AUX:
        if (d) {
            g->usb_rec_aux = !g->usb_rec_aux;
            globals_apply(g);
        }
        break;
    case R_USB_WET: {
        int v = ((g->usb_rec_wet[0] | g->usb_rec_wet[1] << 1) + d % 4 + 4) % 4;
        g->usb_rec_wet[0] = (uint8_t)(v & 1);
        g->usb_rec_wet[1] = (uint8_t)(v >> 1);
        globals_apply(g);
        break;
    }
    case R_MIDI_SOURCE:
        g->midi_source = (uint8_t)(((g->midi_source < 3 ? g->midi_source : 2) + 3 + d % 3) % 3);
        globals_apply(g);
        break;
    case R_MIDI_TRS:
    case R_MIDI_USB: {
        int16_t *ch = &g->midi_channel[row == R_MIDI_USB];
        *ch = (int16_t)((((*ch >= 0 && *ch <= 16) ? *ch : 0) + 17 + d % 17) % 17);
        globals_apply(g);
        break;
    }
    case R_CLOCK_SOURCE:
        g->clock_source = (uint8_t)(((g->clock_source < 5 ? g->clock_source : 4) + 5 + d % 5) % 5);
        globals_apply(g);
        break;
    case R_CLOCK_OUT:
        g->clock_out = (uint8_t)(((g->clock_out & 3) + 4 + d % 4) % 4);
        globals_apply(g);
        break;
    case R_EQ:
        if (d) {
            g->eq_on = !g->eq_on;
            globals_apply(g);
        }
        break;
    case R_TEMPO: {
        /* off below 40 BPM; the effects follow it instead of the presets' tempo while it is on */
        int bpm = g->tempo_mode && g->bpm ? g->bpm : 39;
        bpm = clamp(bpm + d, 39, 300);
        if (!g->tempo_mode && d > 0)
            bpm = g->bpm ? g->bpm : ui->rig->preset.bpm;
        g->tempo_mode = bpm >= 40;
        if (bpm >= 40)
            g->bpm = (uint16_t)bpm;
        rig_set_global_tempo(ui->rig, g->tempo_mode ? g->bpm : 0);
        break;
    }
    case R_EXP2_JACK:
        g->exp2_jack = (uint8_t)((g->exp2_jack + 3 + d % 3) % 3);
        break;
    case R_EXT1:
    case R_EXT2:
        g->ext_fn[row == R_EXT2] = (int8_t)assign_fn_step(g->ext_fn[row == R_EXT2], d);
        break;
    case R_BRIGHTNESS:
        st->brightness = (uint8_t)clamp(st->brightness + 10 * d, 10, 100);
        settings_apply(st);
        break;
    case R_SPILL:                           /* from the next preset change */
        st->spill = (uint8_t)clamp(st->spill + d, 0, SETTINGS_SPILL_CHOICES - 1);
        settings_apply(st);
        break;
    case R_SCENE_EDIT:                      /* AUTO: a parameter turned while the scenes are in play joins them */
        if (d) {
            st->scene_manual = !st->scene_manual;
            ui->rig->scene_auto = !st->scene_manual;
        }
        break;
    case R_SAFETY_TEST:
        s.test = (s.test + 4 + d % 4) % 4;
        break;
    case R_CURSOR:
        if (d) {
            st->touch_cursor = !st->touch_cursor;
            gesture_cursor_set(st->touch_cursor);
        }
        break;
    case R_CAB_ENGINE:                      /* A / B: the preset reloads on the other engine */
        if (d) {
            st->cab_original = !st->cab_original;
            settings_apply(st);
            rig_rebuild(ui->rig);
        }
        break;
    case R_DIAG:
        if (d)
            diag_toggle();
        break;
    default:
        break;
    }
    ui->rev++;
}

/* a tap on the setting, or MENU on a card that opens something */
static void activate(struct ui_ctx *ui, int row)
{
    switch (row) {
    case R_TUNER:
        ui_open_tuner();
        break;
    case R_EQ_EDIT:
        ui_go(SCR_EQ);
        break;
    case R_CONTROLS:
        ui_global_controls();
        break;
    case R_LATENCY:
        ui_go(SCR_LATENCY);
        break;
    case R_CALIBRATE:
    case R_EXP1_CAL:
    case R_EXP2_CAL:
        ui->exp_cal = row == R_EXP2_CAL;
        ui_go(SCR_EXP_CAL);
        break;
    case R_SAFETY_TEST:
        if (!s.test)
            break;
        settings_save(ui->settings, &ui->rig->src);     /* the test restarts the pedal */
        globals_save(ui->globals, &ui->rig->src);
        safety_test((enum safety_test)(s.test - 1));
        break;
    case R_VERSION:
    case R_CPU_TEMP:
    case R_BOOT_LOCK:
        break;
    default:
        change(ui, row, 1);                 /* toggles flip, choices step */
        break;
    }
}

/* shown, not set */
static int is_info(int row)
{
    return row == R_VERSION || row == R_CPU_TEMP || row == R_BOOT_LOCK;
}

static int is_action(int row)
{
    return row == R_TUNER || row == R_EQ_EDIT || row == R_CONTROLS || row == R_LATENCY || row == R_CALIBRATE ||
           row == R_EXP1_CAL || row == R_EXP2_CAL;
}

static void signed_db(char *out, unsigned size, int db)
{
    lv_snprintf(out, size, "%s%d dB", db > 0 ? "+" : "", db);
}

static void latency_text(char *out, unsigned size)
{
    struct latency_result r;
    latency_get(&r);
    if (r.status == LAT_OK || r.status == LAT_UNSTABLE) {
        int n = (int)(r.mean * (100000.0f / AUDIO_RATE) + 0.5f);       /* hundredths of a ms */
        lv_snprintf(out, size, "%d.%02d ms", n / 100, n % 100);
    } else {
        lv_snprintf(out, size, "MEDIR");
    }
}

/* the value and its position in the range (-1 = no bar) */
static float value_text(const struct ui_ctx *ui, int row, char *out, unsigned size)
{
    const struct settings *st = ui->settings;
    const struct globals *g = ui->globals;
    switch (row) {
    case R_TUNER:       lv_snprintf(out, size, "ABRIR \xC2\xBB"); return -1;
    case R_A4:          lv_snprintf(out, size, "%d Hz", st->a4); return (st->a4 - 430) / 20.0f;
    case R_TUNER_OUT:   lv_snprintf(out, size, st->tuner_mute ? "MUDO" : "LIGADO"); return -1;
    case R_TUNER_VIEW:  lv_snprintf(out, size, st->tuner_strobe ? "STROBE" : "AGULHA"); return -1;
    case R_INPUT_LEVEL: signed_db(out, size, g->input_level); return (g->input_level + 20) / 40.0f;
    case R_INPUT_MODE:  lv_snprintf(out, size, "%s", g->input_mode < 3 ? input_modes[g->input_mode] : "MODO 4"); return -1;
    case R_AUX:         lv_snprintf(out, size, "%d", g->aux_level); return g->aux_level / 100.0f;
    case R_CABS:        lv_snprintf(out, size, "%s", cab_outputs[g->no_cab[0] | g->no_cab[1] << 1]); return -1;
    case R_USB_MODE:    lv_snprintf(out, size, g->usb_mode ? "2 IN / 2 OUT" : "6 IN / 4 OUT"); return -1;
    case R_USB_REC:     signed_db(out, size, g->usb_rec_level); return (g->usb_rec_level + 20) / 40.0f;
    case R_USB_MON:     signed_db(out, size, g->usb_mon_level); return (g->usb_mon_level + 20) / 40.0f;
    case R_USB_AUX:     lv_snprintf(out, size, g->usb_rec_aux ? "LIGADO" : "DESLIGADO"); return -1;
    case R_USB_WET:     lv_snprintf(out, size, "%s", usb_wet[g->usb_rec_wet[0] | g->usb_rec_wet[1] << 1]); return -1;
    case R_MIDI_SOURCE: lv_snprintf(out, size, "%s", midi_sources[g->midi_source < 3 ? g->midi_source : 2]); return -1;
    case R_MIDI_TRS:
    case R_MIDI_USB: {
        int ch = g->midi_channel[row == R_MIDI_USB];
        if (ch == 0)
            lv_snprintf(out, size, "OMNI");
        else
            lv_snprintf(out, size, "%d", ch);
        return -1;
    }
    case R_CLOCK_SOURCE: lv_snprintf(out, size, "%s", g->clock_source < 5 ? clock_sources[g->clock_source] : "-"); return -1;
    case R_CLOCK_OUT:   lv_snprintf(out, size, "%s", clock_outs[g->clock_out & 3]); return -1;
    case R_EQ:          lv_snprintf(out, size, g->eq_on ? "LIGADO" : "DESLIGADO"); return -1;
    case R_EQ_EDIT:     lv_snprintf(out, size, "EDITAR \xC2\xBB"); return -1;
    case R_TEMPO:
        if (g->tempo_mode && g->bpm) {
            lv_snprintf(out, size, "%d BPM", g->bpm);
            return (g->bpm - 40) / 260.0f;
        }
        lv_snprintf(out, size, "DESLIGADO");
        return 0;
    case R_EXP1_CAL:
    case R_EXP2_CAL: {                      /* the range; the bar is where the pedal is now */
        int n = row == R_EXP2_CAL;
        lv_snprintf(out, size, "%d \xE2\x80\x93 %d", g->exp_min[n], g->exp_max[n]);
        return expression_value(ui->pedals, n) / 255.0f;
    }
    case R_CALIBRATE:   lv_snprintf(out, size, "ABRIR \xC2\xBB"); return -1;
    case R_EXP2_JACK:   lv_snprintf(out, size, "%s", exp2_jacks[g->exp2_jack % 3]); return -1;
    case R_EXT1:
    case R_EXT2:        lv_snprintf(out, size, "%s", assign_fn_name(g->ext_fn[row == R_EXT2])); return -1;
    case R_BRIGHTNESS:  lv_snprintf(out, size, "%d%%", st->brightness); return st->brightness / 100.0f;
    case R_CURSOR:      lv_snprintf(out, size, st->touch_cursor ? "LIGADO" : "DESLIGADO"); return -1;
    case R_CAB_ENGINE:  lv_snprintf(out, size, st->cab_original ? "ORIGINAL" : "WAVEOD"); return -1;
    case R_SPILL:
        if (st->spill && st->spill < SETTINGS_SPILL_CHOICES)
            lv_snprintf(out, size, "AT\xC3\x89 %d s", settings_spill_ms[st->spill] / 1000);
        else
            lv_snprintf(out, size, "DESLIGADO");
        return st->spill / (float)(SETTINGS_SPILL_CHOICES - 1);
    case R_SCENE_EDIT:  lv_snprintf(out, size, st->scene_manual ? "MANUAL" : "AUTO"); return -1;
    case R_CONTROLS:    lv_snprintf(out, size, "%d \xC2\xBB", ui->rig->global.count); return -1;
    case R_LATENCY: {
        char ms[16];
        latency_text(ms, sizeof ms);
        lv_snprintf(out, size, "%s \xC2\xBB", ms);
        return -1;
    }
    case R_DIAG:        lv_snprintf(out, size, diag_shown() ? "MOSTRANDO" : "OCULTO"); return -1;
    case R_SAFETY_TEST: lv_snprintf(out, size, "%s", safety_tests[s.test]); return -1;
    case R_VERSION:     lv_snprintf(out, size, "%s", WAVEOD_VERSION); return -1;
    case R_BOOT_LOCK:   lv_snprintf(out, size, "%s", boot_lock_names[flash_lock_state() & 3]); return -1;
    case R_CPU_TEMP: {                      /* the bar: 20 .. 95 C, the part's limit */
        int t = sysmon_get()->temp;
        sysmon_temp_text(t, out, size);
        return t == PLAT_TEMP_NONE ? -1 : (t - 200) / 750.0f;
    }
    default:            out[0] = 0; return -1;
    }
}

/* the line under a card's name: what it is set to */
static void card_status(const struct ui_ctx *ui, int c, char *out, unsigned size)
{
    const struct settings *st = ui->settings;
    const struct globals *g = ui->globals;
    char a[24], b[24];
    switch (c) {
    case 0:
        value_text(ui, R_INPUT_MODE, a, sizeof a);
        value_text(ui, R_INPUT_LEVEL, b, sizeof b);
        lv_snprintf(out, size, "%s \xC2\xB7 %s", a, b);
        break;
    case 1:
        lv_snprintf(out, size, "%d Hz \xC2\xB7 %s", st->a4, st->tuner_mute ? "MUDO" : "SOM");
        break;
    case 2:
        value_text(ui, R_USB_MODE, out, size);
        break;
    case 3:
        lv_snprintf(out, size, "%d\xE2\x80\x93%d \xC2\xB7 %d\xE2\x80\x93%d", g->exp_min[0], g->exp_max[0], g->exp_min[1],
                    g->exp_max[1]);
        break;
    case 4:
        value_text(ui, R_EQ, out, size);
        break;
    case 5:
        value_text(ui, R_EXP2_JACK, out, size);
        break;
    case 6:
        value_text(ui, R_MIDI_SOURCE, out, size);
        break;
    case 7:
        if (g->tempo_mode && g->bpm) {
            lv_snprintf(out, size, "%d BPM", g->bpm);
        } else {
            value_text(ui, R_SPILL, a, sizeof a);
            lv_snprintf(out, size, "RASTRO %s", a);
        }
        break;
    case 8:
        lv_snprintf(out, size, "%d LIGA\xC3\x87\xC3\x95" "ES", ui->rig->global.count);
        break;
    case 9:
        lv_snprintf(out, size, "BRILHO %d%%", st->brightness);
        break;
    case 10:
        latency_text(out, size);
        break;
    default: {
        int t = sysmon_get()->temp;
        if (t != PLAT_TEMP_NONE) {
            sysmon_temp_text(t, a, sizeof a);
            lv_snprintf(out, size, "CPU %s", a);
        } else {
            lv_snprintf(out, size, "waveod %s", WAVEOD_VERSION);
        }
        break;
    }
    }
}

/* ---------------------------------------------------------------- touch */

static void select_card(struct ui_ctx *ui, int c)
{
    c = (c % CARDS + CARDS) % CARDS;
    if (c != s.sel) {
        s.sel = c;
        s.sub = 0;
    }
    ui->rev++;
}

static void card_menu(struct ui_ctx *ui)
{
    if (cards[s.sel].open >= 0) {
        activate(ui, cards[s.sel].open);
    } else if (subs(s.sel) > 1) {
        s.sub = (s.sub + 1) % subs(s.sel);
        ui->rev++;
    }
}

static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    int page = s.sel / PER_PAGE, c = page * PER_PAGE + g->tag;
    if (g->kind == GESTURE_DRAG_END) {
        int d = gesture_swipe_x(g);         /* finger to the left = next page */
        if (d)
            select_card(ui, ((page - d + PAGES) % PAGES) * PER_PAGE);
    } else if (g->kind == GESTURE_TAP && c < CARDS) {
        if (c == s.sel)
            card_menu(ui);
        else
            select_card(ui, c);
    }
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    int row = cell_row(g->tag);
    if (row < 0)
        return;
    if (g->kind == GESTURE_TAP) {
        activate(ui, row);
    } else if (g->kind == GESTURE_DRAG && !is_action(row) && !is_info(row)) {
        if (g->first)
            s.drag_start = 0;
        int steps = g->dx / DRAG_PX;
        if (steps != s.drag_start) {
            change(ui, row, steps - s.drag_start);
            s.drag_start = steps;
        }
    }
}

static void on_arrow(struct ui_ctx *ui, const struct gesture *g)
{
    int n = subs(s.sel);
    if (g->kind != GESTURE_TAP || n < 2)
        return;
    s.sub = (s.sub + g->tag + n) % n;
    ui->rev++;
}

/* ---------------------------------------------------------------- drawing */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    for (int i = 0; i < PER_PAGE; i++) {
        struct card_obj *o = &s.card[i];
        int x = UI_PAD + (i % COLS) * (CARD_W + GAP), y = GRID_Y + (i / COLS) * (CARD_H + GAP);
        o->box = theme_box(root, x, y, CARD_W, CARD_H, C_BG);
        lv_obj_set_style_radius(o->box, 3, 0);
        lv_obj_set_style_border_width(o->box, 1, 0);
        lv_obj_set_style_border_color(o->box, rgb(C_FRAME), 0);
        gesture_bind(o->box, on_card, i);
        o->icon = theme_icon(o->box, &icon_g_io, C_LABEL);
        lv_obj_set_pos(o->icon, ICON_X, (CARD_H - 22) / 2 - 1);
        o->name = theme_label(o->box, &font_body_14, C_TEXT, 0);
        lv_obj_set_pos(o->name, TEXT_X, 8);
        lv_obj_set_width(o->name, CARD_W - TEXT_X - 6);
        lv_label_set_long_mode(o->name, LV_LABEL_LONG_DOT);
        o->status = theme_label(o->box, &font_tag_12, C_OFF, 0);
        lv_obj_set_pos(o->status, TEXT_X, 29);
        lv_obj_set_width(o->status, CARD_W - TEXT_X - 6);
        lv_label_set_long_mode(o->status, LV_LABEL_LONG_DOT);
    }

    /* the page: one segment per page on a hairline, like the original's */
    theme_box(root, UI_PAD, PAGE_Y + 1, UI_W - 2 * UI_PAD, 1, C_LINE);
    int seg_w = (UI_W - 2 * UI_PAD) / PAGES;
    for (int p = 0; p < PAGES; p++)
        s.page_seg[p] = theme_box(root, UI_PAD + p * seg_w, PAGE_Y, seg_w, 3, C_LINE);

    /* the card's settings: three cells between ‹ and › */
    theme_box(root, 0, CELLS_Y - 1, UI_W, 1, C_LINE);
    for (int k = 0; k < 3; k++) {
        param_cell_create(&s.cell[k], root, ARROW_W + k * CELL_W, CELLS_Y, CELL_W, CELLS_H, &font_value_20, k > 0);
        s.cell_hit[k] = s.cell[k].box;
        gesture_bind(s.cell_hit[k], on_cell, k);
    }
    s.prev_hit = theme_box(root, 0, CELLS_Y, ARROW_W, CELLS_H, C_BG);
    gesture_bind(s.prev_hit, on_arrow, -1);
    s.prev = theme_icon(s.prev_hit, &icon_prev_16, C_LABEL);
    lv_obj_center(s.prev);
    s.next_hit = theme_box(root, UI_W - ARROW_W, CELLS_Y, ARROW_W, CELLS_H, C_BG);
    gesture_bind(s.next_hit, on_arrow, 1);
    s.next = theme_icon(s.next_hit, &icon_next_16, C_LABEL);
    lv_obj_center(s.next);
}

static void enter(struct ui_ctx *ui)
{
    (void)ui;
    for (int n = 0; n < 2; n++)
        s.exp_seen[n] = -1;
}

static void leave(struct ui_ctx *ui)
{
    if (diag_shown())
        diag_toggle();
    int err = settings_save(ui->settings, &ui->rig->src);
    err |= globals_save(ui->globals, &ui->rig->src);
    if (err)
        ui_toast("Falha ao gravar as configura\xC3\xA7\xC3\xB5" "es");
}

/* the pedals' values and the latency result are on screen: redraw when they move */
static void tick(struct ui_ctx *ui)
{
    struct latency_result r;
    latency_get(&r);
    int moved = r.seq != s.lat_seen || sysmon_get()->seq != s.temp_seen;
    s.lat_seen = r.seq;
    s.temp_seen = sysmon_get()->seq;
    if (s.sel == 3)
        for (int n = 0; n < 2; n++) {
            int v = expression_value(ui->pedals, n);
            moved |= v != s.exp_seen[n];
            s.exp_seen[n] = v;
        }
    if (moved)
        ui->rev++;
}

static void refresh(struct ui_ctx *ui)
{
    top_bar_sub("GLOBAL");
    int page = s.sel / PER_PAGE;
    for (int i = 0; i < PER_PAGE; i++) {
        struct card_obj *o = &s.card[i];
        int c = page * PER_PAGE + i, sel = c == s.sel;
        if (c >= CARDS) {
            lv_obj_add_flag(o->box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(o->box, LV_OBJ_FLAG_HIDDEN);
        char status[40];
        card_status(ui, c, status, sizeof status);
        theme_set_bg(o->box, sel ? C_SEL_BG : C_BG);
        theme_set_border(o->box, sel ? C_ACCENT : C_FRAME);
        lv_obj_set_style_border_width(o->box, sel ? 2 : 1, 0);
        theme_set_icon(o->icon, cards[c].icon, sel ? C_ACCENT : C_LABEL);
        theme_set_text(o->name, cards[c].name);
        theme_set_color(o->name, sel ? C_TEXT : C_NEUTRAL_ON);
        theme_set_text(o->status, status);
        theme_set_color(o->status, sel ? C_LABEL : C_OFF);
    }
    for (int p = 0; p < PAGES; p++)
        theme_set_bg(s.page_seg[p], p == page ? C_ACCENT : C_LINE);

    for (int k = 0; k < 3; k++) {
        int row = cell_row(k);
        if (row < 0) {
            param_cell_set(&s.cell[k], "", "", "", -1, C_LABEL, C_TEXT, C_ACCENT);
            continue;
        }
        char value[40];
        float frac = value_text(ui, row, value, sizeof value);
        uint32_t vc = is_action(row) && row != R_EXP1_CAL && row != R_EXP2_CAL ? C_ACCENT
                      : row == R_VERSION                                        ? C_LABEL
                                                                                : C_TEXT;
        char source[24] = "";
        uint32_t fill = row == R_EXP1_CAL || row == R_EXP2_CAL ? C_TUNED : C_ACCENT;
        if (row == R_CPU_TEMP) {            /* the highest since start-up beside the name; red from 85 C */
            const struct sysmon *m = sysmon_get();
            if (m->temp_max != PLAT_TEMP_NONE)
                lv_snprintf(source, sizeof source, "M\xC3\x81X %d\xC2\xB0", (m->temp_max + 5) / 10);
            if (m->temp != PLAT_TEMP_NONE && m->temp >= 850)
                fill = vc = C_CLIP;
        }
        if (row == R_BOOT_LOCK) {           /* protected in green, a failed lock in red */
            int lock = flash_lock_state();
            vc = lock == FLASH_LOCK_ON ? C_TUNED : lock == FLASH_LOCK_FAILED ? C_CLIP : C_LABEL;
        }
        param_cell_set(&s.cell[k], titles[row], source, value, frac, C_LABEL, vc, fill);
        /* a long value ("INTERNO E CLOCK", a footswitch function) in the smaller type, on the same baseline */
        const lv_font_t *font = lv_text_get_width(value, lv_strlen(value), &font_value_20, 0) > CELL_W - 28
                                    ? &font_body_14 : &font_value_20;
        if (lv_obj_get_style_text_font(s.cell[k].value, 0) != font) {
            lv_obj_set_style_text_font(s.cell[k].value, font, 0);
            lv_obj_set_style_translate_y(s.cell[k].value, font == &font_value_20 ? 0 : 5, 0);
        }
    }
    int more = subs(s.sel) > 1;
    theme_set_icon(s.prev, &icon_prev_16, more ? C_LABEL : C_LINE);
    theme_set_icon(s.next, &icon_next_16, more ? C_LABEL : C_LINE);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        select_card(ui, s.sel + d);
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        d = ui_turned(ev, k);
        int row = cell_row(k - ENC_KNOB1);
        if (d && row >= 0) {
            if (!is_action(row) && !is_info(row))
                change(ui, row, d);
            return 1;
        }
    }
    if (ui_pressed(ev, BTN_MENU)) {
        card_menu(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_GLOBAL)) {
        if (diag_shown()) {
            diag_toggle();
            ui->rev++;
        } else {
            ui_back();
        }
        return 1;
    }
    return 0;
}

const struct screen screen_settings = { create, enter, refresh, input, leave, tick };
