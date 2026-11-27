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
#include "ui/i18n.h"
#include "ui/theme.h"
#include "usb/usb_audio.h"
#include "usb/usb_device.h"
#include "usb/usb_midi.h"
#include <stdarg.h>

#define REFRESH_MS 500

static lv_obj_t *panel, *text;
static uint32_t shown_at;

static const char *const crash_names[] = {   /* 0: the chip's watchdog, a text of the catalog */
    "-", "HardFault", "MemManage", "BusFault", "UsageFault", "NMI", "IRQ", "watchdog", 0,
};

/* "watchdog 32 kHz · last fault: UsageFault PC 0005A1C2 CFSR 00010000 · preset 12A slot 3 audio · 14 s · 2x" */
static void safety_line(char *out, unsigned size)
{
    char wd[32], crash[160];
    uint32_t hz = safety_watchdog_hz();
    if (hz)
        lv_snprintf(wd, sizeof wd, "watchdog %u Hz", (unsigned)hz);
    else
        lv_snprintf(wd, sizeof wd, "%s", T(MSG_SYS_DIAG_WD_OFF));
    struct crash_info c;
    if (!safety_last_crash(&c)) {
        lv_snprintf(crash, sizeof crash, "%s", T(MSG_SYS_DIAG_NO_CRASH));
    } else {
        char label[8] = "-";
        if (c.preset != SAFETY_NONE)
            preset_label(c.preset, label);
        const char *cause = c.cause < sizeof crash_names / sizeof crash_names[0] ? crash_names[c.cause] : "?";
        lv_snprintf(crash, sizeof crash, T(MSG_SYS_DIAG_CRASH), cause ? cause : T(MSG_SYS_DIAG_CHIP_WD),
                    (unsigned)c.pc, c.cause == CRASH_IRQ ? "exc" : "CFSR", (unsigned)c.detail, label,
                    c.slot == SAFETY_NONE ? 0 : c.slot + 1, c.in_audio ? T(MSG_SYS_DIAG_IN_AUDIO) : "",
                    (unsigned)c.uptime_s, (unsigned)c.count);
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
    uint16_t name;                                  /* enum i18n_msg */
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
        sp[0] = (struct span){ .name = MSG_SYS_DIAG_SPAN_APP, .start = (end + 0xfff) & ~0xfffu, .end = 0x3A0000 };
        sp[1] = (struct span){ .name = MSG_SYS_DIAG_SPAN_APP_END, .start = 0x3A0000, .end = 0x40A000 };
        sp[2] = (struct span){ .name = MSG_SYS_DIAG_SPAN_EFFECTS, .start = (lib + 0xfff) & ~0xfffu, .end = 0x700000 };
        sp[3] = (struct span){ .name = MSG_SYS_DIAG_SPAN_IR, .start = 0x73F000, .end = 0x753000 };
        for (int k = 0; k < 4; k++)
            span_scan(&sp[k]);
        done = 1;
    }
    int n = lv_snprintf(out, size, "%s", T(MSG_SYS_DIAG_FLASH));
    for (int k = 0; k < 4 && n > 0 && (unsigned)n < size; k++) {
        n += lv_snprintf(out + n, size - (unsigned)n, "%s", k ? " \xC2\xB7 " : " ");
        if ((unsigned)n < size)
            n += lv_snprintf(out + n, size - (unsigned)n, T(MSG_SYS_DIAG_SPAN), T(sp[k].name),
                             (unsigned)sp[k].start, (unsigned)sp[k].end, (unsigned)((sp[k].used + 1023) / 1024),
                             (unsigned)sp[k].last);
    }
}

/* a formatted text at out + n (n: the length so far, kept below size); the new length */
static unsigned add(char *out, unsigned size, unsigned n, const char *fmt, ...)
{
    if (n + 1 >= size)
        return n;
    va_list ap;
    va_start(ap, fmt);
    int k = lv_vsnprintf(out + n, size - n, fmt, ap);
    va_end(ap);
    n += k > 0 ? (unsigned)k : 0;
    return n < size ? n : size - 1;
}

static void update(void)
{
    char flash[400], safety[220], spans[240], lat[120], all[1600];
    struct latency_result lr;
    latency_get(&lr);
    if (lr.status == LAT_OK || lr.status == LAT_UNSTABLE) {
        int n = (int)(lr.mean * 10.0f + 0.5f);
        lv_snprintf(lat, sizeof lat, T(MSG_SYS_DIAG_LATENCY_LAST), n / 10, n % 10, latency_route_name(lr.route));
    } else {
        lv_snprintf(lat, sizeof lat, "%s", T(MSG_SYS_DIAG_LATENCY_NONE));
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
    unsigned n = 0;
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_CPU), (unsigned)((plat_core_hz() + 500000u) / 1000000u),
            (unsigned)((plat_boot_core_hz() + 500000u) / 1000000u), touch_ready() ? "" : T(MSG_SYS_DIAG_TOUCH_MISSING),
            touch_info());
    n = add(all, sizeof all, n, "\n");
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_DSP), (int)(st.block_ms * 1000.0f), (int)(st.max_ms * 1000.0f),
            (unsigned)st.overruns, (unsigned)st.overloads);
    n = add(all, sizeof all, n, "\n");
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_TAILS), (unsigned)st.spills,
            engine_spilling() ? T(MSG_SYS_DIAG_TAILS_SOUNDING) : "", (unsigned)st.spill_cuts);
    n = add(all, sizeof all, n, "\n");
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_LATENCY), LAT_BUFFERS, lat);
    n = add(all, sizeof all, n, "\n%s\n", safety);
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_USB_AUDIO),
            usb_device_configured() ? T(MSG_SYS_DIAG_USB_CONFIGURED) : T(MSG_SYS_DIAG_USB_NO_HOST),
            usb_device_mode() == USB_MODE_2X2 ? "2/2" : "6/4", usb_midi_ready() ? "ok" : "-",
            (unsigned)ua.rec_packets, (unsigned)ua.rec_short, (unsigned)ua.play_packets, (unsigned)ua.play_under);
    n = add(all, sizeof all, n, "\n");
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_USB_BUS), uc.high_speed ? "HS" : "FS", (unsigned)uc.resets,
            (unsigned)uc.setups, (unsigned)uc.last_setup, (unsigned)uc.completions, (unsigned)uc.errors);
    n = add(all, sizeof all, n, "\n");
    n = add(all, sizeof all, n, T(MSG_SYS_DIAG_MIDI_IN), (unsigned)ms.in[MIDI_TRS], (unsigned)ms.in[MIDI_USB],
            (unsigned)ms.clocks_out);
    add(all, sizeof all, n, "\n%s\n%s", flash, spans);
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
