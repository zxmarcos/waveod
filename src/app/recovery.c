#include "app/recovery.h"
#include "platform/controls.h"
#include "platform/safety.h"
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
    case CRASH_HARDFAULT:   return "falha grave do processador";
    case CRASH_MEMMANAGE:   return "acesso proibido \xC3\xA0 mem\xC3\xB3ria";
    case CRASH_BUSFAULT:    return "erro de barramento";
    case CRASH_USAGEFAULT:  return "instru\xC3\xA7\xC3\xA3o inv\xC3\xA1lida";
    case CRASH_NMI:         return "interrup\xC3\xA7\xC3\xA3o n\xC3\xA3o mascar\xC3\xA1vel";
    case CRASH_IRQ:         return "interrup\xC3\xA7\xC3\xA3o sem tratamento";
    case CRASH_WATCHDOG:    return "o programa parou de responder";
    case CRASH_HW_WATCHDOG: return "o watchdog do chip reiniciou o pedal";
    default:                return "falha desconhecida";
    }
}

/* "no preset 12A, bloco 3 (TS808), processando o áudio" */
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
            lv_snprintf(out, size, " no preset %s, bloco %d (%s), %s", label, pos + 1, name,
                     c->in_audio ? "processando o \xC3\xA1udio" : "ao carregar");
        else
            lv_snprintf(out, size, " no preset %s (%s)", label, name);
    } else {
        lv_snprintf(out, size, " no preset %s", label);
    }
}

void recovery_report(const struct rig *r, int started_safe)
{
    char text[320], where[96], label[8];
    preset_label(r->index, label);
    if (manual) {
        lv_snprintf(text, sizeof text, "EXIT segurado ao ligar: o preset %s n\xC3\xA3o foi carregado. Escolha um preset "
                 "para continuar.", label);
        ui_alert("MODO SEGURO", text);
        return;
    }
    struct crash_info c;
    if (!safety_last_crash(&c))
        return;
    where_text(r, &c, where, sizeof where);
    if (started_safe) {
        lv_snprintf(text, sizeof text, "O pedal reiniciou depois de uma falha%s: %s. Este preset n\xC3\xA3o foi carregado; "
                 "escolha outro ou corrija este pelo editor.", where, cause_text(c.cause));
        ui_alert("MODO SEGURO", text);
    } else {
        lv_snprintf(text, sizeof text, "O pedal reiniciou depois de uma falha%s: %s.%s", where, cause_text(c.cause),
                 c.preset != SAFETY_NONE ? " Se acontecer de novo logo, o preset n\xC3\xA3o ser\xC3\xA1 carregado." : "");
        ui_alert("PEDAL REINICIADO", text);
    }
}
