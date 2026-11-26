#include "ui/diag_panel.h"
#include "engine/audio_engine.h"
#include "engine/latency.h"
#include "midi/midi.h"
#include "platform/flash.h"
#include "platform/platform.h"
#include "platform/safety.h"
#include "fw/preset_format.h"
#include "platform/usb.h"
#include "platform/time.h"
#include "platform/touch.h"
#include "ui/theme.h"
#include "usb/usb_audio.h"
#include "usb/usb_device.h"
#include "usb/usb_midi.h"

#define REFRESH_MS 500

static lv_obj_t *panel, *text;
static uint32_t shown_at;

static const char *const crash_names[] = {
    "-", "HardFault", "MemManage", "BusFault", "UsageFault", "NMI", "IRQ", "watchdog", "watchdog do chip",
};

/* "watchdog 32 kHz · última falha: UsageFault PC 0005A1C2 CFSR 00010000 · preset 12A slot 3 áudio · 14 s · 2x" */
static void safety_line(char *out, unsigned size)
{
    char wd[24], crash[160];
    uint32_t hz = safety_watchdog_hz();
    if (hz)
        lv_snprintf(wd, sizeof wd, "watchdog %u Hz", (unsigned)hz);
    else
        lv_snprintf(wd, sizeof wd, "watchdog desligado");
    struct crash_info c;
    if (!safety_last_crash(&c)) {
        lv_snprintf(crash, sizeof crash, "nenhuma falha antes deste in\xC3\xAD" "cio");
    } else {
        char label[8] = "-";
        if (c.preset != SAFETY_NONE)
            preset_label(c.preset, label);
        lv_snprintf(crash, sizeof crash, "\xC3\xBAltima falha: %s PC %08X %s %08X \xC2\xB7 preset %s slot %d%s \xC2\xB7 %u s \xC2\xB7 %ux",
                    c.cause < sizeof crash_names / sizeof crash_names[0] ? crash_names[c.cause] : "?", (unsigned)c.pc,
                    c.cause == CRASH_IRQ ? "exc" : "CFSR", (unsigned)c.detail, label,
                    c.slot == SAFETY_NONE ? 0 : c.slot + 1, c.in_audio ? " \xC3\xA1udio" : "", (unsigned)c.uptime_s,
                    (unsigned)c.count);
    }
    lv_snprintf(out, size, "%s \xC2\xB7 %s", wd, crash);
}

/*
 * What the updates leave in the partitions, to learn whether the bootloader erases a whole partition or only
 * what it writes: the bytes other than 0xFF (KB) and the last one, in the app partition after this image (the
 * original's app is longer: anything left there is its), after the original's app, after the effect library
 * and after the factory IRs. Scanned once (a few MB of XIP reads).
 */
struct span {
    const char *name;
    uint32_t start, end, used, last;
};

static uint32_t rd32(uint32_t off)
{
    const uint8_t *p = flash_ptr(off, 4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

static void span_scan(struct span *s)
{
    s->used = s->last = 0;
    const uint8_t *p = s->end > s->start ? flash_ptr(s->start, s->end - s->start) : 0;
    if (!p || ((s->start | s->end) & 3))
        return;
    const uint32_t *w = (const uint32_t *)p;        /* words: erased flash goes by quickly */
    for (uint32_t i = 0; i < (s->end - s->start) / 4; i++) {
        if (w[i] == 0xffffffffu)
            continue;
        for (int b = 0; b < 4; b++)
            if (p[4 * i + b] != 0xff) {
                s->used++;
                s->last = s->start + 4 * i + b;
            }
    }
}

static void spans_line(char *out, unsigned size)
{
    static struct span sp[4];
    static int done;
    if (!done) {
        /* this image: the bootloader's (flash address, size, load address) pairs at 0x20010 */
        uint32_t end = 0x20000;
        for (int k = 0; k < 2; k++) {
            uint32_t fa = rd32(0x20010 + 12 * k), n = rd32(0x20014 + 12 * k);
            if (fa >= 0x20000 && fa + n < 0x40A000 && fa + n > end)
                end = fa + n;
        }
        uint32_t lib = rd32(0x480000 + 0x0c);       /* the library's total size */
        lib = lib && lib < 0x280000 ? 0x480000 + lib : 0x700000;
        sp[0] = (struct span){ "app", (end + 0xfff) & ~0xfffu, 0x3A0000 };
        sp[1] = (struct span){ "fim app", 0x3A0000, 0x40A000 };
        sp[2] = (struct span){ "efeitos", (lib + 0xfff) & ~0xfffu, 0x700000 };
        sp[3] = (struct span){ "IR", 0x73F000, 0x753000 };
        for (int k = 0; k < 4; k++)
            span_scan(&sp[k]);
        done = 1;
    }
    int n = lv_snprintf(out, size, "Flash, bytes fora de FF:");
    for (int k = 0; k < 4 && n > 0 && (unsigned)n < size; k++)
        n += lv_snprintf(out + n, size - (unsigned)n, "%s %s %06X-%06X %u KB at\xC3\xA9 %06X", k ? " \xC2\xB7" : "",
                         sp[k].name, (unsigned)sp[k].start, (unsigned)sp[k].end, (unsigned)((sp[k].used + 1023) / 1024),
                         (unsigned)sp[k].last);
}

static void update(void)
{
    char flash[400], usb[300], safety[220], spans[240], lat[120], all[1600];
    struct latency_result lr;
    latency_get(&lr);
    if (lr.status == LAT_OK || lr.status == LAT_UNSTABLE) {
        int n = (int)(lr.mean * 10.0f + 0.5f);
        lv_snprintf(lat, sizeof lat, "\xC3\xBAltima medi\xC3\xA7\xC3\xA3o %d.%d amostras (%s)", n / 10, n % 10,
                    latency_route_name(lr.route));
    } else {
        lv_snprintf(lat, sizeof lat, "sem medi\xC3\xA7\xC3\xA3o (GLOBAL \xC2\xBB LAT\xC3\x8ANCIA)");
    }
    safety_line(safety, sizeof safety);
    spans_line(spans, sizeof spans);
    struct usb_dcd_counts uc;
    usb_dcd_counts(&uc);
    struct engine_stats st;
    struct usb_audio_stats ua;
    struct midi_stats ms;
    midi_stats(&ms);
    flash_diag(flash, sizeof flash);
    engine_stats_peek(&st);
    usb_audio_stats(&ua);
    lv_snprintf(usb, sizeof usb, "USB: %s \xC2\xB7 %s \xC2\xB7 MIDI %s \xC2\xB7 grava %u (%u curtos) \xC2\xB7 toca %u (%u falhas)\n"
                "USB %s: resets %u \xC2\xB7 setups %u (\xC3\xBAltimo %08X) \xC2\xB7 transfer\xC3\xAAncias %u \xC2\xB7 erros %u\n"
                "MIDI recebido: TRS %u \xC2\xB7 USB %u \xC2\xB7 clocks enviados %u",
                usb_device_configured() ? "configurado" : "sem computador",
                usb_device_mode() == USB_MODE_2X2 ? "2/2" : "6/4", usb_midi_ready() ? "ok" : "-",
                (unsigned)ua.rec_packets, (unsigned)ua.rec_short, (unsigned)ua.play_packets, (unsigned)ua.play_under,
                uc.high_speed ? "HS" : "FS", (unsigned)uc.resets, (unsigned)uc.setups, (unsigned)uc.last_setup,
                (unsigned)uc.completions, (unsigned)uc.errors, (unsigned)ms.in[MIDI_TRS], (unsigned)ms.in[MIDI_USB], (unsigned)ms.clocks_out);
    lv_snprintf(all, sizeof all,
                "CPU %u MHz (bootloader %u) \xC2\xB7 toque: %s%s\n"
                "DSP: bloco %d us \xC2\xB7 m\xC3\xA1ximo %d us \xC2\xB7 acima de 1250 us: %u \xC2\xB7 sobrecargas: %u\n"
                "Rastros: %u%s \xC2\xB7 cortados por CPU %u\n"
                "Lat\xC3\xAAncia: buffers %d amostras (2.90 ms) \xC2\xB7 %s\n%s\n%s\n%s\n%s",
                (unsigned)((plat_core_hz() + 500000u) / 1000000u), (unsigned)((plat_boot_core_hz() + 500000u) / 1000000u),
                touch_ready() ? "" : "ausente ", touch_info(), (int)(st.block_ms * 1000.0f), (int)(st.max_ms * 1000.0f),
                (unsigned)st.overruns, (unsigned)st.overloads, (unsigned)st.spills, engine_spilling() ? " (soando)" : "",
                (unsigned)st.spill_cuts, LAT_BUFFERS, lat, safety, usb, flash, spans);
    theme_set_text(text, all);
}

void diag_toggle(void)
{
    if (!panel) {
        panel = theme_box(lv_layer_top(), 8, UI_TOPBAR_H + 4, UI_W - 16, 10, C_LINE);
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_border_color(panel, rgb(C_ACCENT), 0);
        lv_obj_set_style_pad_all(panel, 8, 0);
        text = theme_label(panel, &font_body_14, C_TEXT, 0);
        lv_obj_set_width(text, UI_W - 16 - 18);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
        update();
        shown_at = time_ms();
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

int diag_shown(void)
{
    return panel && !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

void diag_tick(void)
{
    if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN) || time_ms() - shown_at < REFRESH_MS)
        return;
    shown_at = time_ms();
    update();
}
