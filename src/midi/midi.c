#include "midi/midi.h"
#include "platform/midi_hw.h"
#include "platform/time.h"
#include "usb/usb_midi.h"
#include <string.h>

#define READS_PER_POLL  8
#define CLOCK_GAP_MS    200                 /* clocks at 40 BPM come every 62.5 ms */

struct parser {
    uint8_t msg[3];
    uint8_t status;         /* running status (0 = none) */
    uint8_t need, n;        /* bytes of the message, bytes so far */
    uint8_t in_sysex;
};

static struct {
    struct midi_handlers h;
    struct midi_config cfg;
    struct parser p[MIDI_PORTS];
    uint8_t bank;                           /* CC 0 (0x2023400C[0]) */
    uint8_t tempo_hi;                       /* CC 68 ([1]) */
    /* clock in, per port (0x20234014 / 18 / 1C) */
    uint16_t clocks[MIDI_PORTS];
    uint32_t since[MIDI_PORTS];
    uint32_t last[MIDI_PORTS];              /* the last clock (waveod) */
    volatile uint16_t tempo;                /* the running tempo (also read by the clock timer) */
    float frac;                             /* clock out: microseconds carried (0x2023400C + 4) */
    struct midi_stats stats;
} md;

static void send(const uint8_t *msg, int len)
{
    midi_uart_write(msg, len);
    if (usb_midi_ready())
        usb_midi_write(msg, len);
}

/* the port is a MIDI source and the message is on its input channel (FUN_0003082C / FUN_00030750) */
static int accepted(int port, int channel)
{
    if (!((md.cfg.source + 1) & (1 << port)))
        return 0;
    uint8_t want = (uint8_t)md.cfg.in_channel[port];
    return want == 0 || want == channel + 1;
}

static void program(int pc)
{
    int preset;
    if (md.bank == 0)
        preset = pc > 119 ? 119 : pc;
    else if (md.bank == 1)
        preset = pc < 120 ? pc + 120 : 239;
    else
        return;                             /* the original loads preset "input channel" here (stale register) */
    if (md.h.program)
        md.h.program(preset);
}

static void control(int cc, int value)
{
    if (cc == 0) {
        md.bank = (uint8_t)value;
        return;
    }
    if (cc == 0x44) {
        md.tempo_hi = (uint8_t)value;
        return;
    }
    if (cc == 0x45)
        value |= (md.tempo_hi & 0x7f) << 7;
    if (md.h.control)
        md.h.control(cc, value);
}

/* FUN_000308A4. After a pause the original measures from its last measurement and takes 40 BPM for a moment;
   here a gap of more than CLOCK_GAP_MS starts the measurement again. */
static void clock_in(int port)
{
    uint32_t now = time_ms(), gap = now - md.last[port];
    md.last[port] = now;
    if (gap > CLOCK_GAP_MS) {               /* this clock starts the count */
        md.clocks[port] = 0;
        md.since[port] = now;
        return;
    }
    uint32_t elapsed = now - md.since[port];
    uint16_t n = ++md.clocks[port];
    int bpm = elapsed ? (int)((float)n * 2500.0f / (float)elapsed + 0.5f) : 300;
    bpm = bpm < 40 ? 40 : bpm > 300 ? 300 : bpm;
    if (n < bpm / 4)
        return;
    md.clocks[port] = 0;
    md.since[port] = now;
    if (bpm == md.tempo)
        return;
    switch (md.cfg.clock_source) {
    case 1: if (port != MIDI_TRS) return; break;
    case 2: if (port != MIDI_USB) return; break;
    case 3: case 4: break;
    default: return;                        /* internal */
    }
    if (md.h.tempo)
        md.h.tempo(bpm);
}

static void dispatch(int port, const uint8_t *msg, int len)
{
    md.stats.in[port]++;
    send(msg, len);                         /* thru (FUN_000307D8 and the others first of all) */
    int kind = msg[0] >> 4, channel = msg[0] & 0x0f;
    if (kind == 0xC && accepted(port, channel))
        program(msg[1] & 0x7f);
    else if (kind == 0xB && accepted(port, channel))
        control(msg[1] & 0x7f, msg[2] & 0x7f);
    else if (msg[0] == 0xF8)
        clock_in(port);
}

/* message length by status (the original's table 0x4711A, with F1 / F3 = 2 and F2 = 3) */
static int length(uint8_t status)
{
    if (status < 0xF0)
        return (status >> 4) == 0xC || (status >> 4) == 0xD ? 2 : 3;
    return status == 0xF2 ? 3 : status == 0xF1 || status == 0xF3 ? 2 : 1;
}

static void feed(int port, uint8_t b)
{
    struct parser *p = &md.p[port];
    if (b >= 0xF8) {                        /* real time: anywhere, nothing else changes */
        dispatch(port, &b, 1);
        return;
    }
    if (b == 0xF0 || b == 0xF7) {
        p->in_sysex = b == 0xF0;
        p->status = 0;
        return;
    }
    if (b & 0x80) {
        p->in_sysex = 0;
        p->msg[0] = b;
        p->n = 1;
        p->need = (uint8_t)length(b);
        p->status = b;
        if (p->need == 1) {
            p->status = 0;
            dispatch(port, p->msg, 1);
        }
        return;
    }
    if (p->in_sysex || !p->status)
        return;
    p->msg[p->n++] = b;
    if (p->n < p->need)
        return;
    dispatch(port, p->msg, p->need);
    p->n = 1;
    if (p->status >= 0xF0)                  /* system common: no running status */
        p->status = 0;
}

void midi_poll(void)
{
    uint8_t buf[256];
    int n;
    for (int k = 0; k < READS_PER_POLL && (n = midi_uart_read(buf, sizeof buf)) > 0; k++)
        for (int i = 0; i < n; i++)
            feed(MIDI_TRS, buf[i]);
    for (int k = 0; k < READS_PER_POLL && (n = usb_midi_read(buf, sizeof buf)) > 0; k++) {
        if (md.h.usb_bytes)
            md.h.usb_bytes(buf, n);
        for (int i = 0; i < n; i++)
            feed(MIDI_USB, buf[i]);
    }
}

uint32_t midi_clock_tick(void)
{
    uint8_t out = md.cfg.clock_out;
    if (out & 1)
        midi_uart_clock();
    if (out & 2)
        usb_midi_clock();
    if (out & 3)
        md.stats.clocks_out++;
    int bpm = md.tempo >= 40 ? md.tempo : 120;
    float next = 2500000.0f / (float)bpm + md.frac;
    uint32_t us = (uint32_t)next;
    md.frac = next - (float)us;
    return us;
}

void midi_init(const struct midi_handlers *h)
{
    memset(&md, 0, sizeof md);
    md.h = *h;
    md.tempo = 120;
    md.cfg.source = 2;                      /* the factory settings until configured */
    md.cfg.clock_source = 4;
    midi_hw_init();
    midi_timer_start(1000, midi_clock_tick);    /* FUN_00035F00(1, 1000, ...) */
}

void midi_configure(const struct midi_config *c)
{
    md.cfg = *c;
}

void midi_set_tempo(int bpm)
{
    md.tempo = (uint16_t)bpm;
}

void midi_send(const uint8_t *msg, int len)
{
    send(msg, len);
}

void midi_stats(struct midi_stats *out)
{
    *out = md.stats;
}
