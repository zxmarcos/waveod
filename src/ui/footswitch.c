#include "ui/footswitch.h"
#include "engine/drums.h"
#include "ui/leds.h"
#include "model/control.h"
#include "model/drum_names.h"
#include "model/drum_user.h"
#include "model/settings.h"
#include "model/tempo.h"
#include "platform/time.h"

#define COMBO_MS   180                      /* second key of a pair within this: the pair (18 ticks) */
#define REPEAT_MS  200                      /* bank / preset steps while held */
#define KEYS       6

/* keys in the original's order (event ids 0x10..0x15): A, B, C, D, BANK-, BANK+ */
static const uint32_t key_button[KEYS] = { BTN_FS_A, BTN_FS_B, BTN_FS_C, BTN_FS_D, BTN_BANK_DOWN, BTN_BANK_UP };

/* the pairs (event ids 0x90, 0x91, 0x92, 0x94) and their entries */
static const struct {
    uint8_t a, b;
} pairs[4] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 4, 5 } };

static struct {
    uint8_t down, held, in_combo;           /* key bit masks */
    uint32_t pressed_at[KEYS];
    int combo;                              /* pair index while its keys are down, -1 */
    int repeat_fn;                          /* function repeated while a key is held, 0 = none */
    uint32_t repeat_at;
    uint32_t tap_at;                        /* last tap tempo press, 0 = none */
} fs = { .combo = -1 };

static int key_of(const struct input_event *ev)
{
    if (ev->type == IN_TURN)
        return -1;
    for (int k = 0; k < KEYS; k++)
        if (key_button[k] == 1u << ev->id)
            return k;
    return -1;
}

int footswitch_mode_bit(const struct ui_ctx *ui)
{
    return ui->scene_mode ? CM_SCENE : ui->stomp_mode ? CM_STOMP : CM_PRESET;
}

static void load(struct ui_ctx *ui, int index)
{
    ui_load_preset((index % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT);
}

void footswitch_tap(struct ui_ctx *ui)
{
    uint32_t now = time_ms();
    uint32_t dt = now - fs.tap_at;
    int first = fs.tap_at == 0;
    fs.tap_at = now ? now : 1;
    if (first || !dt)
        return;
    uint32_t bpm = 60000u / dt;
    if (bpm < 30)                           /* a pause: this press starts a new count */
        return;
    bpm = bpm < 40 ? 40 : bpm > 300 ? 300 : bpm;
    char text[32];
    if (tempo_set(ui->rig, ui->globals, (int)bpm, TEMPO_TAP))
        lv_snprintf(text, sizeof text, "TEMPO %d BPM", (int)bpm);
    else
        lv_snprintf(text, sizeof text, "TEMPO: CLOCK MIDI EXTERNO");
    ui_toast(text);
}

void footswitch_set_mode(struct ui_ctx *ui, int mode)
{
    ui->stomp_mode = mode == FS_MODE_STOMP;
    ui->scene_mode = mode == FS_MODE_SCENE;
    ui->rig->scene_mode = (uint8_t)ui->scene_mode;
    /* the mode the pedal starts in: stomp / preset in the globals like the original (FUN_00033B7C), scene mode
       in waveod's settings (the original knows only the first two) */
    int scene = mode == FS_MODE_SCENE;
    if (ui->settings->scene_mode != scene) {
        ui->settings->scene_mode = (uint8_t)scene;
        settings_touch();
    }
    if (!scene && ui->globals->fs_group != (mode == FS_MODE_STOMP ? FS_STOMP : FS_PRESET)) {
        ui->globals->fs_group = mode == FS_MODE_STOMP ? FS_STOMP : FS_PRESET;
        globals_touch();
    }
}

int footswitch_mode(const struct ui_ctx *ui)
{
    return ui->scene_mode ? FS_MODE_SCENE : ui->stomp_mode ? FS_MODE_STOMP : FS_MODE_PRESET;
}

void footswitch_scene(struct ui_ctx *ui, int n)
{
    if (rig_scene(ui->rig, n) != 0)
        return;
    if (ui->current != SCR_SCENE) {
        char name[SCENE_NAME_LEN + 1];
        rig_scene_name(ui->rig, n, name);
        ui_toast(name);
    }
    ui_changed();
}


/* one footswitch function (FUN_0002693C) */
static void run(struct ui_ctx *ui, int fn)
{
    struct rig *r = ui->rig;
    int index = r->index;
    switch (fn) {
    case FSF_NONE:
        return;
    case FSF_TUNER:
        if (ui->current != SCR_TUNER)
            ui_open_tuner();
        return;
    case FSF_TAP:
        footswitch_tap(ui);
        return;
    case FSF_BANK_UP:
        load(ui, index + 4);
        return;
    case FSF_BANK_DOWN:
        load(ui, index - 4);
        return;
    case FSF_PRESET_UP:
        load(ui, index + 1);
        return;
    case FSF_PRESET_DOWN:
        load(ui, index - 1);
        return;
    case FSF_EXP_AB:
        rig_exp_group(r, !r->preset.exp_group);
        ui_toast(r->preset.exp_group ? "EXP 1: B" : "EXP 1: A");
        ui_changed();
        return;
    case FSF_MODE:                          /* preset -> stomp -> scenes -> preset */
        ui_set_mode((footswitch_mode(ui) + 1) % 3);
        return;
    case FSF_LOOPER:
        ui_go(SCR_LOOPER);
        return;
    case FSF_BANK_MENU:                     /* the presets list, the original's bank screen */
        if (ui->current == SCR_PRESETS)
            ui_back();
        else
            ui_go(SCR_PRESETS);
        return;
    case FSF_DRUM:
        if (!drums_available()) {
            ui_toast("Bateria: o firmware base n\xC3\xA3o trouxe os ritmos");
            return;
        }
        drums_play(!drums_playing());
        ui_toast(drums_playing() ? "BATERIA LIGADA" : "BATERIA PARADA");
        ui_changed();
        return;
    case FSF_DRUM_NEXT:
    case FSF_DRUM_PREV: {
        int d = fn == FSF_DRUM_NEXT ? 1 : -1, user = drum_user_selected();
        if (user >= 0) {                    /* MEUS RITMOS: the next imported one */
            struct drum_user_info info;
            int next = drum_user_step(user, d);
            if (next >= 0 && drum_user_select(next) == 0 && drum_user_info(next, &info) == 0)
                ui_toast(info.name);
        } else {
            globals_set_drum_pattern(ui->globals, globals_drum_pattern(ui->globals) + d);
            ui_toast(drum_pattern_names[globals_drum_pattern(ui->globals)]);
        }
        ui_changed();
        return;
    }
    default:
        break;
    }
    if (fn >= FSF_CTRL1 && fn <= FSF_CTRL4) {   /* the external footswitches, MIDI CC 71-74: stomp key A-D */
        if (control_press(r, fn - FSF_CTRL1, CM_STOMP, 0, footswitch_link, ui))
            ui_changed();
    } else if (fn >= FSF_PRESET_A && fn <= FSF_PRESET_D) {
        int target = index / 4 * 4 + fn - FSF_PRESET_A;
        if (target != index)
            load(ui, target);
        else if (control_press(r, CK_AGAIN, CM_ALL, 0, footswitch_link, ui)) {    /* the active preset again */
            leds_preset_ctrl(index % 4);
            ui_changed();
        }
    } else if (fn >= FSF_SLOT1 && fn <= FSF_SLOT12) {
        rig_toggle(r, fn - FSF_SLOT1);
        ui_changed();
    }
}

/* a function or a scene of a pressed key */
void footswitch_link(void *ctx, const struct control *link)
{
    struct ui_ctx *ui = ctx;
    if (link->action == CA_FN) {
        run(ui, link->arg);
    } else if (link->action == CA_SCENE) {
        int n = rig_scene_active(ui->rig);
        n = link->arg == SCENE_NEXT ? n + 1 : link->arg == SCENE_PREV ? n - 1 : link->arg;
        footswitch_scene(ui, (n % SCENE_COUNT + SCENE_COUNT) % SCENE_COUNT);
    }
}

/* key's press (or long press) group in the current mode; while learning (ui_learn) the key is only told */
static void press(struct ui_ctx *ui, int key, int hold)
{
    if (ui->learned) {
        ui_learn_fn fn = ui->learned;
        ui->learned = 0;
        fn(ui, key, hold);
        return;
    }
    if (control_press(ui->rig, key, footswitch_mode_bit(ui), hold, footswitch_link, ui))
        ui_changed();
}

static void moment(struct ui_ctx *ui, int key, int down)
{
    if (ui->learned)
        return;
    if (control_moment(ui->rig, key, footswitch_mode_bit(ui), down, footswitch_link, ui))
        ui_changed();
}

void footswitch_run(struct ui_ctx *ui, int fn)
{
    run(ui, fn);
}

static int repeats(int fn)
{
    return fn == FSF_BANK_UP || fn == FSF_BANK_DOWN || fn == FSF_PRESET_UP || fn == FSF_PRESET_DOWN;
}

/* the EXP 2 jack's footswitches: their global function when the jack is set for them, on release */
static int external(struct ui_ctx *ui, const struct input_event *ev)
{
    if (ev->type == IN_TURN || (1u << ev->id != BTN_EXT1 && 1u << ev->id != BTN_EXT2))
        return 0;
    int n = 1u << ev->id == BTN_EXT2;
    const struct globals *g = ui->globals;
    if (ui->learned)                        /* global functions, not links: not learned */
        return 1;
    if (ev->type == IN_RELEASE && g->exp2_jack != EXP2_PEDAL && (n == 0 || g->exp2_jack == EXP2_TWO_SWITCHES))
        run(ui, g->ext_fn[n]);
    return 1;
}

int footswitch_input(struct ui_ctx *ui, const struct input_event *ev)
{
    if (external(ui, ev))
        return 1;
    int k = key_of(ev);
    if (k < 0)
        return 0;
    uint8_t bit = (uint8_t)(1u << k);
    uint32_t now = time_ms();

    if (ev->type == IN_PRESS) {
        fs.down |= bit;
        fs.held &= (uint8_t)~bit;
        fs.pressed_at[k] = now;
        moment(ui, k, 1);
        for (int c = 0; c < 4 && fs.combo < 0; c++) {
            int other = pairs[c].a == k ? pairs[c].b : pairs[c].b == k ? pairs[c].a : -1;
            if (other >= 0 && (fs.down & (1u << other)) && !(fs.held & (1u << other)) &&
                now - fs.pressed_at[other] < COMBO_MS) {
                fs.combo = c;
                fs.in_combo = (uint8_t)(1u << pairs[c].a | 1u << pairs[c].b);
            }
        }
        return 1;
    }
    if (ev->type == IN_HOLD) {
        fs.held |= bit;
        if (fs.in_combo & bit)
            return 1;                       /* a held pair does nothing */
        const struct control *l = control_first(ui->rig, k, footswitch_mode_bit(ui), 1, CA_FN);
        int fn = l && !ui->learned ? l->arg : FSF_NONE;
        press(ui, k, 1);
        if (repeats(fn)) {
            fs.repeat_fn = fn;
            fs.repeat_at = now + REPEAT_MS;
        }
        return 1;
    }
    if (ev->type == IN_RELEASE) {
        fs.down &= (uint8_t)~bit;
        fs.repeat_fn = 0;
        moment(ui, k, 0);
        if (fs.in_combo & bit) {
            fs.in_combo &= (uint8_t)~bit;
            if (!fs.in_combo && fs.combo >= 0) {
                int c = fs.combo;
                uint8_t pair = (uint8_t)(1u << pairs[c].a | 1u << pairs[c].b);
                fs.combo = -1;
                if (!(fs.held & pair))
                    press(ui, CK_COMBO + c, 0);
                fs.held &= (uint8_t)~pair;
            }
            return 1;
        }
        if (!(fs.held & bit))
            press(ui, k, 0);
        fs.held &= (uint8_t)~bit;
        return 1;
    }
    return 0;
}

void footswitch_press(struct ui_ctx *ui, int key)
{
    if (key >= 0 && key < KEYS)
        press(ui, key, 0);
}

void footswitch_tick(struct ui_ctx *ui)
{
    if (fs.repeat_fn && (int32_t)(time_ms() - fs.repeat_at) >= 0) {
        fs.repeat_at += REPEAT_MS;
        run(ui, fs.repeat_fn);
    }
}
