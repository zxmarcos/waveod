#include "app/recovery.h"
#include "platform/controls.h"
#include "platform/safety.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "lvgl.h"                     /* lv_snprintf: there is no printf on the pedal */

#define EARLY_S 20                  /* a crash this soon after the start-up skips that preset at once */

static int manual;                  /* EXIT held at power-up */

int recovery_avoid(void)
{
    struct controls_raw raw;
    controls_read(&raw);
    if (raw.buttons & BTN_EXIT) {
        manual = 1;
        return RIG_AVOID_ANY;
    }
    struct crash_info c;
    if (!safety_last_crash(&c) || c.preset == SAFETY_NONE)
        return RIG_AVOID_NONE;
    /* a module of that preset failed soon after the start-up, or the pedal keeps crashing with it loaded */
    int in_module = c.slot != SAFETY_NONE;
    return (in_module && c.uptime_s < EARLY_S) || c.count >= 2 ? c.preset : RIG_AVOID_NONE;
}

static const char *cause_text(int cause)
{
    switch (cause) {
    case CRASH_HARDFAULT:   return T(MSG_SYS_CAUSE_HARDFAULT);
    case CRASH_MEMMANAGE:   return T(MSG_SYS_CAUSE_MEMMANAGE);
    case CRASH_BUSFAULT:    return T(MSG_SYS_CAUSE_BUSFAULT);
    case CRASH_USAGEFAULT:  return T(MSG_SYS_CAUSE_USAGEFAULT);
    case CRASH_NMI:         return T(MSG_SYS_CAUSE_NMI);
    case CRASH_IRQ:         return T(MSG_SYS_CAUSE_IRQ);
    case CRASH_WATCHDOG:    return T(MSG_SYS_CAUSE_WATCHDOG);
    case CRASH_HW_WATCHDOG: return T(MSG_SYS_CAUSE_HW_WATCHDOG);
    default:                return T(MSG_SYS_CAUSE_UNKNOWN);
    }
}

/* " in preset 12A, block 3 (TS808), processing audio" */
static void where_text(const struct rig *r, const struct crash_info *c, char *out, unsigned size)
{
    out[0] = 0;
    if (c->preset == SAFETY_NONE)
        return;
    char label[8], name[40];
    preset_label(c->preset, label);
    int pos = -1;
    if (c->slot != SAFETY_NONE && rig_slot_name_in(r, c->preset, c->slot, &pos, name, sizeof name) == 0) {
        if (pos >= 0)
            lv_snprintf(out, size, T(MSG_SYS_WHERE_BLOCK), label, pos + 1, name,
                        c->in_audio ? T(MSG_SYS_WHERE_AUDIO) : T(MSG_SYS_WHERE_LOADING));
        else
            lv_snprintf(out, size, T(MSG_SYS_WHERE_ITEM), label, name);
    } else {
        lv_snprintf(out, size, T(MSG_SYS_WHERE_PRESET), label);
    }
}

void recovery_report(const struct rig *r, int started_safe)
{
    char text[320], where[96], label[8];
    preset_label(r->index, label);
    if (manual) {
        lv_snprintf(text, sizeof text, T(MSG_SYS_SAFE_MANUAL), label);
        ui_alert(T(MSG_SYS_SAFE_TITLE), text);
        return;
    }
    struct crash_info c;
    if (!safety_last_crash(&c))
        return;
    where_text(r, &c, where, sizeof where);
    if (started_safe) {
        lv_snprintf(text, sizeof text, T(MSG_SYS_SAFE_CRASH), where, cause_text(c.cause));
        ui_alert(T(MSG_SYS_SAFE_TITLE), text);
    } else {
        lv_snprintf(text, sizeof text, T(MSG_SYS_CRASH), where, cause_text(c.cause),
                    c.preset != SAFETY_NONE ? T(MSG_SYS_CRASH_AGAIN) : "");
        ui_alert(T(MSG_SYS_RESTARTED_TITLE), text);
    }
}
