#include "model/assign.h"
#include "model/control.h"

/* the parameter (library order) at knob position `knob` of `slot`, or -1 */
static int knob_k(const struct rig *r, int slot, int knob)
{
    for (int k = 0; k < rig_param_count(r, slot); k++) {
        struct rig_param p;
        if (rig_param(r, slot, k, &p) == 0 && p.def.idx == knob)
            return k;
    }
    return -1;
}

void assign_build(const struct rig *r, struct assign *a)
{
    const struct preset *p = &r->preset;
    for (int i = 0; i < STOMP_COUNT; i++) {
        struct stomp_key *k = &a->stomp[i];
        const struct control *fn = control_first(r, i, CM_STOMP, 0, CA_FN);
        const struct control *sc = control_first(r, i, CM_STOMP, 0, CA_SCENE);
        const struct control *blk = control_first(r, i, CM_STOMP, 0, CA_BLOCK);
        const struct control *prm = control_first(r, i, CM_STOMP, 0, CA_PARAM);
        const struct control *mid = control_first(r, i, CM_STOMP, 0, CA_MIDI);
        k->midi = (int16_t)(mid ? mid->arg + (mid->flags & CF_PC ? 0x100 : 0) : -1);
        k->fn = (int8_t)(fn ? fn->arg : FSF_NONE);
        k->scene = (int8_t)(sc ? sc->arg : -1);
        k->slot = (int8_t)(blk ? blk->slot : prm ? prm->slot : -1);
        k->param = (int8_t)(!blk && prm ? knob_k(r, prm->slot, prm->arg) : -1);
        k->members = (uint8_t)(control_count(r, i, CM_STOMP, 0, CA_BLOCK) + control_count(r, i, CM_STOMP, 0, CA_PARAM));
        k->state = (int8_t)control_state(r, i, CM_STOMP, 0);
    }
    for (int i = 0; i < QUICK_COUNT; i++) {
        struct quick_knob *q = &a->quick[i];
        int t = p->qk_target[i];
        q->k = p->qk_param[i] >= 0 ? p->qk_param[i] : 0;
        if (t == QK_VOLUME)
            q->slot = QUICK_VOLUME;
        else if (t == QK_BPM)
            q->slot = QUICK_BPM;
        else if (t >= 0 && t < PRESET_SLOTS && rig_slot_used(r, t) && q->k < rig_param_count(r, t))
            q->slot = (int8_t)t;
        else
            q->slot = QUICK_NONE;
    }
}

const char *assign_fn_name(int fn)
{
    static const char *const names[] = {
        [FSF_NONE] = "-", [FSF_LOOPER] = "LOOPER", [FSF_DRUM] = "BATERIA", [FSF_TUNER] = "AFINADOR",
        [FSF_TAP] = "TAP", [FSF_BANK_UP] = "BANCO +", [FSF_BANK_DOWN] = "BANCO -", [FSF_PRESET_UP] = "PRESET +",
        [FSF_PRESET_DOWN] = "PRESET -", [FSF_BANK_MENU] = "BANCOS", [FSF_DRUM_NEXT] = "RITMO +",
        [FSF_DRUM_PREV] = "RITMO -", [FSF_EXP_AB] = "EXP A/B", [FSF_MODE] = "MODO",
    };
    static const char *const ctrl[] = { "CTRL 1", "CTRL 2", "CTRL 3", "CTRL 4" };
    static const char *const preset[] = { "PRESET A", "PRESET B", "PRESET C", "PRESET D" };
    if (fn >= FSF_CTRL1 && fn <= FSF_CTRL4)
        return ctrl[fn - FSF_CTRL1];
    if (fn >= FSF_PRESET_A && fn <= FSF_PRESET_D)
        return preset[fn - FSF_PRESET_A];
    if (fn >= FSF_SLOT1 && fn <= FSF_SLOT12)
        return "BLOCO";
    if (fn >= 0 && fn < (int)(sizeof names / sizeof names[0]) && names[fn])
        return names[fn];
    return "?";
}

/* footswitch functions offered, in the editor's order and then the others the original knows */
static const int8_t functions[] = {
    FSF_NONE, FSF_CTRL1, FSF_CTRL1 + 1, FSF_CTRL1 + 2, FSF_CTRL4, FSF_TAP, FSF_LOOPER, FSF_DRUM, FSF_DRUM_PREV,
    FSF_DRUM_NEXT, FSF_TUNER, FSF_EXP_AB, FSF_PRESET_DOWN, FSF_PRESET_UP, FSF_BANK_DOWN, FSF_BANK_UP,
    FSF_PRESET_A, FSF_PRESET_A + 1, FSF_PRESET_A + 2, FSF_PRESET_D, FSF_MODE,
};
#define FUNCTIONS ((int)(sizeof functions / sizeof functions[0]))

int assign_fn_step(int cur, int d)
{
    int i = 0;
    while (i < FUNCTIONS && functions[i] != cur)
        i++;
    if (i == FUNCTIONS)
        i = 0;
    return functions[((i + d) % FUNCTIONS + FUNCTIONS) % FUNCTIONS];
}
