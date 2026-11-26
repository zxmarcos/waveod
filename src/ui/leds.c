#include "ui/leds.h"
#include "engine/looper.h"
#include "platform/leds.h"
#include "platform/time.h"
#include "ui/alert.h"
#include "model/control.h"
#include "ui/footswitch.h"
#include "ui/ui_internal.h"
#include <string.h>

#define TICK_MS      10                     /* the original's key timer */
#define PERIOD       30000u                 /* one toggle every PERIOD / rate ms */
#define FOREVER      0xffffu
#define KEYS         6                      /* A, B, C, D, BANK -, BANK + */
#define KEEP         0xff                   /* functions that leave the LED as it is */
#define RATE_500MS   60
#define RATE_150MS   200
#define RATE_125MS   240
#define WRAP_OFF_MS  250

/* colour of a block's category (the original's table 0x20234479, by library menu) */
static const uint8_t category_colour[16] = {
    [MENU_DYN] = LED_CYAN,   [MENU_FX] = LED_GREEN,  [MENU_WAH] = LED_MAGENTA, [MENU_DRV] = LED_RED,
    [MENU_AMP] = LED_CYAN,   [MENU_CAB] = LED_YELLOW, [MENU_IR] = LED_YELLOW,  [MENU_EQ] = LED_GREEN,
    [MENU_MOD] = LED_BLUE,   [MENU_DLY] = LED_BLUE,   [MENU_RVB] = LED_CYAN,   [MENU_CLONE] = LED_YELLOW,
    [MENU_LOOP] = LED_WHITE, [MENU_SND] = LED_WHITE,  [MENU_RTN] = LED_WHITE,  [MENU_VOL] = LED_MAGENTA,
};

struct want {
    uint8_t colour;
    uint16_t rate;                          /* 0 = steady */
};

static struct led {
    uint8_t colour, lit, applied_ok, oneshot;
    uint16_t count, rate;
    uint32_t acc;
    struct want applied;
} led[LED_COUNT];

static struct {
    uint32_t last_ms;
    int mode;                               /* what owns the LEDs: enum below */
    uint8_t loop_state;
    uint32_t loop_pos;
    uint32_t loop_dark_until;
} st;

enum { MODE_NORMAL, MODE_DARK, MODE_LOOPER, MODE_PRESETS };

static void show(int n)
{
    leds_set(n, led[n].lit ? led[n].colour : LED_OFF);
}

/* led_config (FUN_00022B14): lit at once, then a toggle every PERIOD / rate ms, `count` times */
static void config(int n, int colour, uint16_t count, uint16_t rate)
{
    struct led *l = &led[n];
    l->colour = (uint8_t)colour;
    l->count = count;
    l->rate = rate;
    l->acc = 0;
    l->lit = 1;
    show(n);
}

/* led_tick (FUN_00022B44) */
static void blink(void)
{
    for (int n = 0; n < LED_COUNT; n++) {
        struct led *l = &led[n];
        if (!l->count || !l->rate)
            continue;
        l->acc += TICK_MS * l->rate;
        if (l->acc < PERIOD)
            continue;
        l->acc %= PERIOD;
        l->lit ^= 1;
        show(n);
        if (l->count != FOREVER && --l->count == 0 && l->oneshot) {
            l->oneshot = 0;
            l->applied_ok = 0;              /* back to what the screen wants */
        }
    }
}

static void oneshot(int n, int colour, uint16_t count, uint16_t rate)
{
    config(n, colour, count, rate);
    led[n].oneshot = 1;
}

/* give LED n what the screen wants, when that changed */
static void apply(int n, struct want w, int is_tap)
{
    struct led *l = &led[n];
    if (l->oneshot || (l->applied_ok && l->applied.colour == w.colour && l->applied.rate == w.rate))
        return;
    l->applied = w;
    l->applied_ok = 1;
    config(n, w.colour, w.rate ? FOREVER : 0, w.rate);
    if (is_tap)                             /* every tap LED restarts together: they stay in phase */
        for (int k = 0; k < KEYS; k++)
            if (k != n && led[k].applied_ok && led[k].applied.rate && led[k].applied.colour == LED_YELLOW &&
                !led[k].oneshot) {
                led[k].acc = 0;
                led[k].lit = 1;
                show(k);
            }
}


/* FUN_0003C484: the LED of a key from its press function; KEEP for the functions it leaves alone */
static struct want key_want(const struct ui_ctx *ui, int fn, int *is_tap)
{
    const struct rig *r = ui->rig;
    struct want w = { LED_OFF, 0 };
    *is_tap = 0;

    if (fn >= FSF_PRESET_A && fn <= FSF_PRESET_D) {
        w.colour = r->index % 4 == fn - FSF_PRESET_A ? LED_WHITE : LED_OFF;
        return w;
    }
    switch (fn) {
    case FSF_NONE:        w.colour = LED_OFF; break;
    case FSF_LOOPER:      w.colour = LED_GREEN; break;
    case FSF_DRUM:        w.colour = LED_RED; break;
    case FSF_TUNER:       w.colour = LED_GREEN; break;
    case FSF_TAP:         w.colour = LED_YELLOW; w.rate = rig_tempo(r); *is_tap = 1; break;
    case FSF_BANK_UP:
    case FSF_BANK_DOWN:   w.colour = LED_WHITE; break;
    case FSF_PRESET_UP:
    case FSF_PRESET_DOWN: w.colour = LED_YELLOW; break;
    case FSF_BANK_MENU:   w.colour = LED_CYAN; break;
    case FSF_DRUM_NEXT:
    case FSF_DRUM_PREV:   w.colour = LED_RED; break;
    case FSF_EXP_AB:      w.colour = r->preset.exp_group ? LED_RED : LED_GREEN; break;
    case 21:              w.colour = LED_WHITE; break;
    case FSF_MODE:        w.colour = LED_BLUE; break;
    default:              w.colour = KEEP; break;      /* 22 and the slot switches (24-35) */
    }
    return w;
}

/* key k from the preset's links in the current mode: a key with blocks / parameters lit while its state is on
   (in its own colour, else the first block's category, else white), a scene key cyan on the active scene, a
   function key as the original shows that function */
static struct want link_want(const struct ui_ctx *ui, int k, int *is_tap)
{
    const struct rig *r = ui->rig;
    int mode = footswitch_mode_bit(ui);
    struct want w = { LED_OFF, 0 };
    *is_tap = 0;
    int state = control_state(r, k, mode, 0);
    if (state >= 0) {
        const struct control *b = control_first(r, k, mode, 0, CA_BLOCK);
        int menu = b ? r->preset.slot[b->slot].menu : -1;
        int own = control_table(r, k, mode, 0)->colour[k];
        int colour = own ? own
                     : menu >= 0 && menu < 16 ? category_colour[menu] : LED_WHITE;
        w.colour = state ? (uint8_t)colour : LED_OFF;
        return w;
    }
    const struct control *l = control_first(r, k, mode, 0, CA_NONE);
    if (!l)
        return w;
    if (l->action == CA_SCENE) {
        w.colour = l->arg >= SCENE_COUNT || rig_scene_active(r) == l->arg ? LED_CYAN : LED_OFF;
        return w;
    }
    if (l->action == CA_MIDI)                   /* a program change: nothing to show */
        return w;
    return key_want(ui, l->arg, is_tap);
}

static void normal(const struct ui_ctx *ui)
{
    for (int k = 0; k < KEYS; k++) {
        int is_tap;
        struct want w = link_want(ui, k, &is_tap);
        if (w.colour != KEEP)
            apply(k, w, is_tap);
    }
}

static void looper(void)
{
    struct looper_status ls;
    looper_status(&ls);
    uint32_t now = time_ms();
    int playing = ls.state == LOOP_PLAY || ls.state == LOOP_DUB;
    if (playing && (ls.state != st.loop_state || ls.pos < st.loop_pos))  /* the loop (re)starts */
        st.loop_dark_until = now + WRAP_OFF_MS;
    if (st.loop_state != LOOP_EMPTY && ls.state == LOOP_EMPTY) {        /* erased */
        oneshot(LED_FS_A, LED_RED, 9, RATE_125MS);
        oneshot(LED_FS_B, LED_RED, 9, RATE_125MS);
    }
    st.loop_state = ls.state;
    st.loop_pos = ls.pos;

    struct want w = { LED_OFF, 0 };
    switch (ls.state) {
    case LOOP_EMPTY:   if (ls.armed) { w.colour = LED_RED; w.rate = rig_tempo(ui_context()->rig); } break;
    case LOOP_STOPPED: w.colour = LED_GREEN; w.rate = RATE_500MS; break;
    case LOOP_PLAY:    w.colour = LED_GREEN; break;
    case LOOP_REC:     w.colour = LED_RED; break;
    case LOOP_DUB:     w.colour = LED_BLUE; break;
    default:           break;
    }
    if ((int32_t)(now - st.loop_dark_until) < 0)
        w = (struct want){ LED_OFF, 0 };
    apply(LED_FS_A, w, 0);
    apply(LED_FS_B, w, 0);
    apply(LED_FS_C, (struct want){ LED_OFF, 0 }, 0);
    apply(LED_FS_D, (struct want){ looper_auto() ? LED_CYAN : LED_OFF, 0 }, 0);
    apply(LED_BANK_DOWN, (struct want){ LED_OFF, 0 }, 0);
    apply(LED_BANK_UP, (struct want){ LED_OFF, 0 }, 0);
}

/* the presets list (the original's bank menu, whose LED blinks cyan): BANK -/+ blinking cyan, A-D cyan, the
   active preset's key white when it is in the cursor's bank */
static void presets(const struct ui_ctx *ui)
{
    int bank = ui->preset_cursor / 4;
    for (int k = 0; k < 4; k++)
        apply(k, (struct want){ ui->rig->index == bank * 4 + k ? LED_WHITE : LED_CYAN, 0 }, 0);
    apply(LED_BANK_DOWN, (struct want){ LED_CYAN, RATE_500MS }, 0);
    apply(LED_BANK_UP, (struct want){ LED_CYAN, RATE_500MS }, 0);
}

void leds_tick(struct ui_ctx *ui)
{
    uint32_t now = time_ms();
    if (now - st.last_ms >= TICK_MS) {
        uint32_t steps = (now - st.last_ms) / TICK_MS;
        st.last_ms += steps * TICK_MS;
        while (steps--)
            blink();
    }

    int mode = alert_shown() || ui->current == SCR_TUNER ? MODE_DARK
               : ui->current == SCR_LOOPER                ? MODE_LOOPER
               : ui->current == SCR_PRESETS               ? MODE_PRESETS
                                                          : MODE_NORMAL;
    if (mode != st.mode) {                  /* a screen that owns the LEDs: start from all off */
        st.mode = mode;
        for (int n = 0; n < KEYS; n++) {
            led[n].oneshot = 0;
            led[n].applied_ok = 0;
            config(n, LED_OFF, 0, 0);
        }
        struct looper_status ls;
        looper_status(&ls);
        st.loop_state = ls.state;
        st.loop_pos = ls.pos;
        st.loop_dark_until = 0;
    }
    if (mode == MODE_NORMAL)
        normal(ui);
    else if (mode == MODE_LOOPER)
        looper();
    else if (mode == MODE_PRESETS)
        presets(ui);
    else
        for (int n = 0; n < KEYS; n++)
            apply(n, (struct want){ LED_OFF, 0 }, 0);

    /* LED 6: the EXP 1 group, on every screen */
    apply(LED_EXP1, (struct want){ ui->rig->preset.exp_group ? LED_RED : LED_GREEN, 0 }, 0);
}


void leds_preset_ctrl(int letter)
{
    if (letter >= 0 && letter < 4)
        oneshot(letter, LED_WHITE, 18, RATE_150MS);
}
