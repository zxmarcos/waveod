/*
 * A key's links (a card of CONTROLES, scr_control_map.c; EXP): what a footswitch does in a footswitch mode, or
 * what the toe switch and the expression pedals move, as the preset's control table (fw/preset_format.h,
 * model/control.c); where each goes is chosen on the destination screen (scr_target.c). Not in the design; a
 * list like the settings.
 *
 * Rows (knob 1 / MENU press):
 *   TECLA              key (A..D, BANK -/+, the pairs, in preset mode the active preset's key again, the toe
 *                      switch, EXP 1A / 1B / 2, MIDI CC 1..127) / learn it: the next footswitch pressed, pedal
 *                      moved or MIDI CC received
 *   NOME NO CARD       - / the name keyboard (A..D, BANK -/+)
 *   COR                automatic or a LED colour
 *   (a modulator, MOD 1-4, instead of these two: its settings, fw/preset_format.h struct preset_mod: TIPO, then
 *   per type FORMA, SINCRONIA, VELOCIDADE, FASE / SENSIBILIDADE, ATAQUE, RETORNO / SINCRONIA, VELOCIDADE, PASSOS,
 *   DESLIZE and one row per step; knob 1 or MENU press changes them)
 *   one row per link   - / where it goes (the destination screen)
 *   + NOVA LIGAÇÃO     - / a new one, on the destination screen
 * MENU hold removes the selected link; EFX = the mode edited (preset, stomp, scenes); EXIT stops learning, else
 * goes back.
 * Touch: tap a row to select it (tap again = MENU press), drag it sideways = knob 1, hold a link = remove it.
 * Changes are part of the preset (EDITADO) and saved with it; on the global table (GLOBAL › CONTROLES GLOBAIS,
 * ui->ctl_global) they are stored when the control screens are left. On a preset, the global links of the groups it
 * has no links in follow its own as GLOBAL rows: MENU press copies the group into the preset (then its destination),
 * MENU hold gives the group a CA_NOTHING link.
 */
#include "engine/modulators.h"
#include "model/control.h"
#include "model/param_format.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "ui/footswitch.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/row_list.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define ROW_H    32
#define DRAG_PX  24

enum { R_KEY, R_NAME, R_COLOUR, R_LINKS };

static struct {
    struct row_list list;
    char titles[ROW_LIST_MAX][24];
    int key;                                /* enum control_key */
    int mode;                               /* enum control_mode bit edited */
    int links[CONTROL_MAX], nlinks;         /* table entries of the key, in the rows' order */
    uint8_t from_global[CONTROL_MAX];       /* that row is a global link (a group the preset has no links in) */
    int global;                             /* editing the global table (GLOBAL › CONTROLES GLOBAIS) */
    int first;                              /* the first link row (R_LINKS, or after a modulator's settings) */
    int fields[ROW_LIST_MAX], nfields;      /* a modulator's settings rows, from row 1 (enum mod_field) */
    int drag_start;
} s SDRAM_BSS;

static const char *key_name(int key)
{
    return key == CK_AGAIN ? "PRESET ATIVO DE NOVO" : ui_key_name(key);
}

static const char *const curve_names[CC_COUNT] = { "LINEAR", "LENTA", "R\xC3\x81PIDA" };

static const char *const colour_names[8] = {
    "AUTOM\xC3\x81TICA", "VERMELHO", "VERDE", "AMARELO", "AZUL", "MAGENTA", "CIANO", "BRANCO",
};

static int pedal(void)
{
    return CK_IS_PEDAL(s.key);
}

static struct preset_controls *table(struct ui_ctx *ui)
{
    return ui_ctl_table(ui);
}

/* the keys offered: footswitches and pairs, the active preset's key (preset mode only, not global), the toe
   switch, the pedals, the MIDI CCs (these three in every mode) */
static int key_ok(int mode, int key)
{
    return key >= 0 && CK_VALID(key) && (key != CK_AGAIN || (mode == CM_PRESET && !s.global)) &&
           !(s.global && CK_IS_MOD(key));      /* a modulator belongs to its preset */
}

/* ---------------------------------------------------------------- a modulator's settings */

enum mod_field { MF_TYPE, MF_SHAPE, MF_SYNC, MF_RATE, MF_PHASE, MF_SENS, MF_ATTACK, MF_RELEASE, MF_STEPS, MF_GLIDE,
                 MF_STEP };                 /* MF_STEP + i: step i */

static int modulator(void)
{
    return CK_IS_MOD(s.key);
}

static struct preset_mod *the_mod(struct ui_ctx *ui)
{
    return &ui->rig->preset.mods.m[(s.key - CK_MOD1) & 3];
}

/* the settings rows of the modulator's type */
static void mod_fields(struct ui_ctx *ui)
{
    const struct preset_mod *m = the_mod(ui);
    int *f = s.fields, n = 0;
    f[n++] = MF_TYPE;
    if (m->type == MOD_LFO) {
        f[n++] = MF_SHAPE;
        f[n++] = MF_SYNC;
        if (!m->sync)
            f[n++] = MF_RATE;
        f[n++] = MF_PHASE;
    } else if (m->type == MOD_ENV) {
        f[n++] = MF_SENS;
        f[n++] = MF_ATTACK;
        f[n++] = MF_RELEASE;
    } else if (m->type == MOD_SEQ) {
        f[n++] = MF_SYNC;
        if (!m->sync)
            f[n++] = MF_RATE;
        f[n++] = MF_STEPS;
        f[n++] = MF_GLIDE;
        for (int i = 0; i < m->steps && i < MOD_STEPS; i++)
            f[n++] = MF_STEP + i;
    }
    s.nfields = n;
}

/* the setting of a row, -1 = not a modulator's settings row */
static int field_of(int row)
{
    return modulator() && row >= 1 && row < 1 + s.nfields ? s.fields[row - 1] : -1;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* a time in ms d steps on (finer when short) */
static int step_ms(int ms, int d, int lo, int hi)
{
    for (; d > 0; d--)
        ms += ms < 20 ? 1 : ms < 100 ? 5 : ms < 500 ? 10 : 50;
    for (; d < 0; d++)
        ms -= ms <= 20 ? 1 : ms <= 100 ? 5 : ms <= 500 ? 10 : 50;
    return clampi(ms, lo, hi);
}

static void mod_knob(struct ui_ctx *ui, int field, int d)
{
    struct preset_mod *m = the_mod(ui);
    if (field == MF_TYPE) {
        int t = clampi(m->type + d, MOD_OFF, MOD_TYPES - 1);
        if (t != m->type && m->type == MOD_OFF)
            preset_mod_default(m, t);
        m->type = (uint8_t)t;
    } else if (field == MF_SHAPE) {
        m->shape = (uint8_t)(((m->shape + d) % MS_SHAPES + MS_SHAPES) % MS_SHAPES);
    } else if (field == MF_SYNC) {
        m->sync = (uint8_t)clampi(m->sync + d, 0, 11);
    } else if (field == MF_RATE) {
        int c = (int)(m->rate * 100.0f + 0.5f);     /* hundredths of a Hz */
        for (; d > 0; d--)
            c += c < 100 ? 5 : c < 500 ? 10 : c < 1000 ? 50 : 100;
        for (; d < 0; d++)
            c -= c <= 100 ? 5 : c <= 500 ? 10 : c <= 1000 ? 50 : 100;
        m->rate = (float)clampi(c, 5, 2000) * 0.01f;
    } else if (field == MF_PHASE) {
        m->phase = (uint8_t)clampi(m->phase + 5 * d, 0, 95);
    } else if (field == MF_SENS) {
        m->sens = (int8_t)clampi(m->sens + d, -20, 40);
    } else if (field == MF_ATTACK) {
        m->attack = (uint16_t)step_ms(m->attack, d, 1, 2000);
    } else if (field == MF_RELEASE) {
        m->release = (uint16_t)step_ms(m->release, d, 10, 5000);
    } else if (field == MF_STEPS) {
        m->steps = (uint8_t)clampi(m->steps + d, 1, MOD_STEPS);
    } else if (field == MF_GLIDE) {
        m->glide = (uint8_t)clampi(m->glide + 5 * d, 0, 100);
    } else if (field >= MF_STEP && field < MF_STEP + MOD_STEPS) {
        uint8_t *v = &m->step[field - MF_STEP];
        *v = (uint8_t)clampi(*v + 5 * d, 0, 100);
    }
}

static void field_title(int field, char *buf, unsigned size)
{
    static const char *const titles[MF_STEP] = {
        "TIPO", "FORMA", "SINCRONIA", "VELOCIDADE", "FASE INICIAL", "SENSIBILIDADE", "ATAQUE", "RETORNO", "PASSOS",
        "DESLIZE",
    };
    if (field >= MF_STEP)
        lv_snprintf(buf, size, "PASSO %d", field - MF_STEP + 1);
    else
        lv_snprintf(buf, size, "%s", field >= 0 ? titles[field] : "");
}

static void field_text(struct ui_ctx *ui, int field, char *out, unsigned size)
{
    static const char *const types[MOD_TYPES] = { "DESLIGADO", "LFO", "ENVELOPE", "SEQU\xC3\x8A" "NCIA" };
    static const char *const shapes[MS_SHAPES] = {
        "SENOIDE", "TRI\xC3\x82NGULO", "QUADRADA", "DENTE SUBINDO", "DENTE DESCENDO", "ALEAT\xC3\x93RIA",
        "ALEAT\xC3\x93RIA SUAVE",
    };
    const struct preset_mod *m = the_mod(ui);
    int c = (int)(m->rate * 100.0f + 0.5f);
    switch (field) {
    case MF_TYPE:
        if (m->type == MOD_OFF)
            lv_snprintf(out, size, "%s", types[MOD_OFF]);
        else
            lv_snprintf(out, size, "%s \xC2\xB7 AGORA %d%%", types[m->type % MOD_TYPES],
                        (int)(mods_value(s.key - CK_MOD1) * 100.0f + 0.5f));
        break;
    case MF_SHAPE:   lv_snprintf(out, size, "%s", shapes[m->shape % MS_SHAPES]); break;
    case MF_SYNC:    lv_snprintf(out, size, "%s", m->sync ? param_sync_divisions[(m->sync - 1) % 11] : "LIVRE"); break;
    case MF_RATE:    lv_snprintf(out, size, "%d.%02d Hz", c / 100, c % 100); break;
    case MF_PHASE:   lv_snprintf(out, size, "%d%%", m->phase); break;
    case MF_SENS:    lv_snprintf(out, size, "%+d dB", m->sens); break;
    case MF_ATTACK:  lv_snprintf(out, size, "%d ms", m->attack); break;
    case MF_RELEASE: lv_snprintf(out, size, "%d ms", m->release); break;
    case MF_STEPS:   lv_snprintf(out, size, "%d", m->steps); break;
    case MF_GLIDE:   lv_snprintf(out, size, "%d%%", m->glide); break;
    default:         lv_snprintf(out, size, "%d%%", m->step[(field - MF_STEP) % MOD_STEPS]); break;
    }
}

/* the next key offered after `key` (dir 1) or before it (-1), round */
static int next_key(int mode, int key, int dir)
{
    for (int n = 0; n < 2 * 256; n++) {
        key = dir > 0 ? (key + 1 == CK_COUNT ? CK_MIDI : key + 1 > 255 ? 0 : key + 1)
                      : (key - 1 < 0 ? 255 : key == CK_MIDI ? CK_COUNT - 1 : key - 1);
        if (key_ok(mode, key))
            return key;
    }
    return 0;
}

static int step_key(int mode, int key, int d)
{
    for (; d > 0; d--)
        key = next_key(mode, key, 1);
    for (; d < 0; d++)
        key = next_key(mode, key, -1);
    return key;
}

static int in_group(const struct control *l, int hold)
{
    return l->key == s.key && (l->modes & s.mode) && (hold ? l->when == CW_HOLD : l->when != CW_HOLD);
}

/* the key's links in the edited mode; on a preset, the global ones of the groups (press, long press) it has none
   in after them */
static void collect(struct ui_ctx *ui)
{
    const struct preset_controls *c = table(ui), *g = &ui->rig->global;
    s.nfields = 0;
    if (modulator())
        mod_fields(ui);
    s.first = modulator() ? 1 + s.nfields : R_LINKS;
    int max = ROW_LIST_MAX - s.first - 1, own[2] = { 0, 0 };
    s.nlinks = 0;
    for (int i = 0; i < c->count && s.nlinks < max; i++)
        if (c->c[i].key == s.key && (c->c[i].modes & s.mode)) {
            own[c->c[i].when == CW_HOLD]++;
            s.from_global[s.nlinks] = 0;
            s.links[s.nlinks++] = i;
        }
    for (int i = 0; !s.global && i < g->count && s.nlinks < max; i++)
        if ((in_group(&g->c[i], 0) && !own[0]) || (in_group(&g->c[i], 1) && !own[1])) {
            s.from_global[s.nlinks] = 1;
            s.links[s.nlinks++] = i;
        }
}

static int row_global(int row)
{
    return row >= s.first && row < s.first + s.nlinks && s.from_global[row - s.first];
}

/* a row's link in the edited table, -1 = none (or a global link shown on a preset) */
static int row_link(int row)
{
    return row >= s.first && row < s.first + s.nlinks && !row_global(row) ? s.links[row - s.first] : -1;
}

static const struct control *row_control(struct ui_ctx *ui, int row)
{
    if (row < s.first || row >= s.first + s.nlinks)
        return 0;
    return row_global(row) ? &ui->rig->global.c[s.links[row - s.first]] : &table(ui)->c[s.links[row - s.first]];
}

static int key_modes(void)
{
    return s.key >= CK_TOE ? CM_ALL : s.mode;   /* the toe switch, the pedals, MIDI: the same in every mode */
}

/* the global links of a group (hold: the long press one) copied into the preset, which then has that group; the
   table index of the copy of global link `which` (-1: none / the table is full) */
static int copy_group(struct ui_ctx *ui, int hold, int which)
{
    struct preset_controls *c = &ui->rig->preset.controls;
    const struct preset_controls *g = &ui->rig->global;
    int at = -1;
    for (int i = 0; i < g->count; i++) {
        if (!in_group(&g->c[i], hold))
            continue;
        struct control l = g->c[i];
        l.modes = (uint8_t)key_modes();
        if (preset_control_add(c, &l) != 0) {
            ui_toast("Tabela de controles cheia");
            return -1;
        }
        if (i == which)
            at = c->count - 1;
    }
    return at;
}

static int row_add(void)
{
    return s.first + s.nlinks;
}

static void edited(struct ui_ctx *ui)
{
    ui_ctl_edited(ui);
}

/* the link of a row made the edited mode's own (a link shared with other modes splits), its table index */
static int own(struct ui_ctx *ui, int row)
{
    struct preset_controls *c = table(ui);
    int i = row_link(row);
    if (i < 0)
        return -1;
    if (c->c[i].modes != s.mode && s.key < CK_TOE) {   /* the toe switch and the pedals are the same in every mode */
        if (c->count >= CONTROL_MAX) {
            ui_toast("Tabela de controles cheia");
            return -1;
        }
        /* the copy right after it: the rows keep their order */
        memmove(&c->c[i + 2], &c->c[i + 1], (size_t)(c->count - i - 1) * sizeof c->c[0]);
        c->count++;
        c->c[i + 1] = c->c[i];
        c->c[i + 1].modes = (uint8_t)s.mode;
        c->c[i].modes &= (uint8_t)~s.mode;
        collect(ui);
        i++;
    }
    return i;
}

/* ---------------------------------------------------------------- changes */

static void knob(struct ui_ctx *ui, int row, int d)
{
    struct preset_controls *c = table(ui);
    if (row == R_KEY) {
        s.key = step_key(s.mode, s.key, d);
        collect(ui);
        ui->rev++;
    } else if (field_of(row) >= 0) {
        mod_knob(ui, field_of(row), d);
        collect(ui);                                /* a type or sync change shows other rows */
        edited(ui);
    } else if (modulator()) {
        return;
    } else if (row == R_COLOUR && s.key < CK_KEYS) {
        c->colour[s.key] = (uint8_t)(((c->colour[s.key] + d) % 8 + 8) % 8);
        edited(ui);
    }
}

/* a new link that does nothing yet: the destination screen gives it one, or drops it. On a preset whose press
   group is the global one, that group is copied in first (the new link joins it). */
static void add_link(struct ui_ctx *ui)
{
    if (!s.global && control_from_global(ui->rig, s.key, s.key >= CK_TOE ? CM_ALL : s.mode, 0)) {
        copy_group(ui, 0, -1);
        edited(ui);
    }
    struct control l = { .key = (uint8_t)s.key, .modes = (uint8_t)key_modes(),
                         .when = CW_PRESS, .action = CA_FN, .slot = -1, .arg = FSF_NONE };
    if (preset_control_add(table(ui), &l) != 0) {
        ui_toast("Tabela de controles cheia");
        return;
    }
    collect(ui);
    s.list.sel = s.first + s.nlinks - 1;
    ui_target(table(ui)->count - 1, 1);
}

static void remove_link(struct ui_ctx *ui, int row)
{
    struct preset_controls *c = table(ui);
    if (row_global(row)) {                      /* not in this preset: its group gets a NOTHING link */
        const struct control *g = row_control(ui, row);
        struct control none = { .key = (uint8_t)s.key, .modes = (uint8_t)key_modes(),
                                .when = (uint8_t)(g->when == CW_HOLD ? CW_HOLD : CW_PRESS), .action = CA_NOTHING,
                                .slot = -1 };
        if (preset_control_add(c, &none) != 0) {
            ui_toast("Tabela de controles cheia");
            return;
        }
        collect(ui);
        ui_toast("SEM AS GLOBAIS NESTE PRESET");
        edited(ui);
        return;
    }
    int i = row_link(row);
    if (i < 0)
        return;
    if (c->c[i].modes != s.mode && s.key < CK_TOE)
        c->c[i].modes &= (uint8_t)~s.mode;      /* still in the other modes */
    else
        preset_control_remove(c, i);
    collect(ui);
    ui_toast("LIGA\xC3\x87\xC3\x83O REMOVIDA");
    edited(ui);
}

/* learning the key: the footswitch or pedal the player uses next */
static void learned(struct ui_ctx *ui, int key, int hold)
{
    (void)hold;
    if (!key_ok(s.mode, key))
        return;
    s.key = key;
    s.list.sel = R_KEY;
    collect(ui);
    ui->rev++;
}

static void activate(struct ui_ctx *ui, int row)
{
    if (row == R_KEY) {
        ui_learn(ui->learned == learned ? 0 : learned);
    } else if (field_of(row) >= 0) {
        knob(ui, row, 1);
    } else if (row == R_NAME && !modulator()) {
        if (s.key < CK_KEYS)
            ui_name_key(s.key);
    } else if (row == row_add()) {
        add_link(ui);
    } else if (row_global(row)) {               /* made the preset's own (with the rest of its group) */
        int i = copy_group(ui, row_control(ui, row)->when == CW_HOLD, s.links[row - s.first]);
        if (i >= 0) {
            ui_toast("COPIADA PARA O PRESET");
            edited(ui);
            ui_target(i, 0);
        }
    } else if (row_link(row) >= 0) {
        int i = own(ui, row);
        if (i >= 0)
            ui_target(i, 0);
    } else {
        knob(ui, row, 1);
    }
}

/* ---------------------------------------------------------------- text */

static const char *when_text(int when)
{
    return when == CW_HOLD ? "SEGURAR" : when == CW_MOMENT ? "ENQUANTO PISA" : "APERTAR";
}

static void link_text(struct ui_ctx *ui, const struct control *l, char *out, unsigned size)
{
    char name[24] = "?";
    if (l->action == CA_NOTHING) {
        lv_snprintf(out, size, "NADA (SEM AS GLOBAIS)");
    } else if (l->action == CA_MIDI) {
        if (l->flags & CF_PC)
            lv_snprintf(out, size, "MIDI PC %d \xC2\xB7 CANAL %d", l->arg, (l->slot & 15) + 1);
        else
            lv_snprintf(out, size, "MIDI CC %d %d \xC2\xBB %d \xC2\xB7 CANAL %d", l->arg, (int)l->off, (int)l->on,
                        (l->slot & 15) + 1);
    } else if (CK_IS_PEDAL(l->key) && l->action == CA_BLOCK) {
        ui_slot_name(ui, l->slot, name, sizeof name);
        lv_snprintf(out, size, "%s %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name,
                    l->arg ? "LIGA AO MOVER" : "COM O GRUPO");
    } else if (l->action == CA_BLOCK) {
        ui_slot_name(ui, l->slot, name, sizeof name);
        lv_snprintf(out, size, "%s %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name, l->arg ? "DESLIGA" : "LIGA");
    } else if (l->action == CA_PARAM) {
        struct rig_param p;
        char lo[16], hi[16];
        if (control_param_index(ui->rig, l, &p) < 0) {
            lv_snprintf(out, size, "%s ?", ui_slot_tag(ui, l->slot));
            return;
        }
        ui_param_label(&p, name, sizeof name);
        param_format(&p.def, l->off, 0, lo, sizeof lo);
        param_format(&p.def, l->on, 0, hi, sizeof hi);
        if (CK_SWEEPS(l->key) && l->curve != CC_LINEAR)
            lv_snprintf(out, size, "%s %s %s \xC2\xBB %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name, lo, hi,
                        curve_names[l->curve % CC_COUNT]);
        else
            lv_snprintf(out, size, "%s %s %s \xC2\xBB %s", ui_slot_tag(ui, l->slot), name, lo, hi);
    } else if (l->action == CA_SCENE) {
        if (l->arg < SCENE_COUNT) {
            rig_scene_name(ui->rig, l->arg, name);
            lv_snprintf(out, size, "CENA %d \xC2\xB7 %s", l->arg + 1, name);
        } else {
            lv_snprintf(out, size, l->arg == SCENE_NEXT ? "PR\xC3\x93XIMA CENA" : "CENA ANTERIOR");
        }
    } else {
        lv_snprintf(out, size, "%s", assign_fn_name(l->arg));
    }
}

static void value_text(void *ctx, int row, char *out, unsigned size)
{
    struct ui_ctx *ui = ctx;
    const struct preset_controls *c = table(ui);
    if (row == R_KEY && ui->learned == learned) {
        lv_snprintf(out, size, "PISE, MEXA UM PEDAL OU MANDE UM CC");
    } else if (row == R_KEY) {
        lv_snprintf(out, size, "%s \xC2\xB7 %d", key_name(s.key), s.nlinks);
    } else if (field_of(row) >= 0) {
        field_text(ui, field_of(row), out, size);
    } else if (row == R_NAME) {
        lv_snprintf(out, size, "%s", s.key >= CK_KEYS ? "-" :
                                     c->label[s.key][0] ? c->label[s.key] : "(AUTOM\xC3\x81TICO)");
    } else if (row == R_COLOUR) {
        lv_snprintf(out, size, "%s", s.key >= CK_KEYS ? "-" : colour_names[c->colour[s.key] & 7]);
    } else if (row == row_add()) {
        lv_snprintf(out, size, "MENU \xC2\xBB");
    } else if (row_control(ui, row)) {
        link_text(ui, row_control(ui, row), out, size);
    } else {
        out[0] = 0;
    }
}

/* ---------------------------------------------------------------- touch */

static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    struct row_list *l = &s.list;
    if (g->tag >= l->count)
        return;
    if (row_list_scroll(l, g)) {
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        if (l->sel == g->tag)
            activate(ui, g->tag);
        l->sel = g->tag;
        ui->rev++;
    } else if (g->kind == GESTURE_LONG && row_link(g->tag) >= 0) {
        l->sel = g->tag;
        remove_link(ui, g->tag);
    } else if (g->kind == GESTURE_DRAG) {
        if (g->first)
            s.drag_start = 0;
        l->sel = g->tag;
        int steps = g->dx / DRAG_PX;
        if (steps != s.drag_start) {
            knob(ui, g->tag, steps - s.drag_start);
            s.drag_start = steps;
        }
    }
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    row_list_create(&s.list, root, ROW_LIST_MAX, UI_TOPBAR_H, ROW_H, on_row);
}

/* opened on ui_controls' key, mode and link when given; back from the destination screen as it was */
static void enter(struct ui_ctx *ui)
{
    s.global = ui->ctl_global;
    if (ui->controls_key >= 0 || ui->controls_mode) {
        s.mode = ui->controls_mode ? ui->controls_mode : footswitch_mode_bit(ui);
        if (ui->controls_key >= 0)
            s.key = ui->controls_key;
        s.list.sel = 0;
        s.list.top = 0;
    } else if (!s.mode) {
        s.mode = footswitch_mode_bit(ui);
    }
    if (!key_ok(s.mode, s.key))
        s.key = 0;
    collect(ui);
    for (int k = 0; k < s.nlinks; k++)
        if (s.links[k] == ui->controls_link)
            s.list.sel = s.first + k;
    ui->controls_key = -1;
    ui->controls_mode = 0;
    ui->controls_link = -1;
}

static void refresh(struct ui_ctx *ui)
{
    static const char *const modes[] = { "", "MODO PRESET", "MODO STOMP", "", "MODO CENAS" };
    char title[48];
    const char *pre = s.global ? "GLOBAIS \xC2\xB7 " : "";
    if (CK_IS_MIDI(s.key))
        lv_snprintf(title, sizeof title, "%sMIDI \xC2\xB7 TODOS OS MODOS", pre);
    else if (modulator())
        lv_snprintf(title, sizeof title, "MODULADORES \xC2\xB7 DESTE PRESET");
    else if (s.key >= CK_TOE)
        lv_snprintf(title, sizeof title, "%sPEDAIS \xC2\xB7 TODOS OS MODOS", pre);
    else
        lv_snprintf(title, sizeof title, "%s%s \xC2\xB7 %s", pre, s.global ? "TECLAS" : "FOOTSWITCHES", modes[s.mode]);
    top_bar_sub(title);
    collect(ui);
    struct row_list *l = &s.list;
    int n = row_add() + 1;
    for (int r = n; r < ROW_LIST_MAX; r++)
        lv_obj_add_flag(l->rows[r], LV_OBJ_FLAG_HIDDEN);
    l->count = n;
    l->last_stop = n - 1;
    if (l->sel >= n)
        l->sel = n - 1;
    lv_snprintf(s.titles[R_KEY], sizeof s.titles[0], "TECLA");
    if (modulator()) {
        for (int r = 1; r < s.first; r++)
            field_title(field_of(r), s.titles[r], sizeof s.titles[0]);
    } else {
        lv_snprintf(s.titles[R_NAME], sizeof s.titles[0], "NOME NO CARD");
        lv_snprintf(s.titles[R_COLOUR], sizeof s.titles[0], "COR");
    }
    for (int k = 0; k < s.nlinks; k++)
        lv_snprintf(s.titles[s.first + k], sizeof s.titles[0], "%s%s", s.from_global[k] ? "GLOBAL \xC2\xB7 " : "",
                    pedal() ? "PEDAL" : modulator() ? "MOD" : CK_IS_MIDI(s.key) ? "MIDI" :
                    s.key == CK_TOE ? "APERTAR" : when_text(row_control(ui, s.first + k)->when));
    lv_snprintf(s.titles[row_add()], sizeof s.titles[0], "+ NOVA LIGA\xC3\x87\xC3\x83O");
    for (int r = 0; r < n; r++)
        row_list_title(l, r, s.titles[r]);
    row_list_refresh(l, value_text, ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        row_list_step(&s.list, d);
        ui->rev++;
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB1);
    if (d) {
        knob(ui, s.list.sel, d);
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU) {
        remove_link(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        activate(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {                  /* the next mode */
        s.mode = s.mode == CM_PRESET ? CM_STOMP : s.mode == CM_STOMP ? CM_SCENE : CM_PRESET;
        if (!key_ok(s.mode, s.key))
            s.key = 0;
        collect(ui);
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) && ui->learned) {     /* stop learning */
        ui_learn(0);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_CTRL)) {
        ui_back();
        return 1;
    }
    return 0;
}

/* a modulator's value now, on its TIPO row, 10 times a second */
static void tick(struct ui_ctx *ui)
{
    static uint32_t last;
    if (!modulator() || the_mod(ui)->type == MOD_OFF || time_ms() - last < 100)
        return;
    last = time_ms();
    row_list_refresh(&s.list, value_text, ui);
}

const struct screen screen_controls = { create, enter, refresh, input, 0, tick };
