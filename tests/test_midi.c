/* midi/midi.c against fake ports: parsing, filters, bank select, thru, the clock in and out. */
#include "midi/midi.h"
#include "platform/midi_hw.h"
#include "test.h"
#include "usb/usb_midi.h"
#include <string.h>

/* ---------------------------------------------------------------- fakes */

static struct {
    uint8_t in[2][256];
    int in_len[2];
    uint8_t out[2][512];
    int out_len[2];
    int clocks[2];
    uint32_t now;
    int programs[16], nprog;
    int cc[16][2], ncc;
    int tempos[8], ntempo;
    uint8_t usb_bytes[64];
    int nusb;
} f;

uint32_t test_now;
uint32_t time_ms(void) { return f.now + test_now; }
void midi_hw_init(void) {}
void midi_timer_start(uint32_t first_us, uint32_t (*fn)(void)) { (void)first_us; (void)fn; }

static int take(int port, uint8_t *buf, int max)
{
    int n = f.in_len[port] < max ? f.in_len[port] : max;
    memcpy(buf, f.in[port], (size_t)n);
    memmove(f.in[port], f.in[port] + n, (size_t)(f.in_len[port] - n));
    f.in_len[port] -= n;
    return n;
}

static int put(int port, const uint8_t *msg, int len)
{
    memcpy(f.out[port] + f.out_len[port], msg, (size_t)len);
    f.out_len[port] += len;
    return 0;
}

int test_midi_out(int port, uint8_t *buf, int max)
{
    int n = f.out_len[port] < max ? f.out_len[port] : max;
    memcpy(buf, f.out[port], (size_t)n);
    f.out_len[port] = 0;
    return n;
}

int midi_uart_read(uint8_t *buf, int max) { return take(MIDI_TRS, buf, max); }
int midi_uart_write(const uint8_t *msg, int len) { return put(MIDI_TRS, msg, len); }
void midi_uart_clock(void) { f.clocks[MIDI_TRS]++; }
int usb_midi_ready(void) { return 1; }
int usb_midi_read(uint8_t *buf, int max) { return take(MIDI_USB, buf, max); }
int usb_midi_write(const uint8_t *msg, int len) { return put(MIDI_USB, msg, len); }
void usb_midi_clock(void) { f.clocks[MIDI_USB]++; }

static void on_program(int p) { f.programs[f.nprog++ & 15] = p; }
static void on_control(int cc, int v) { f.cc[f.ncc & 15][0] = cc; f.cc[f.ncc++ & 15][1] = v; }
static void on_tempo(int bpm) { f.tempos[f.ntempo++ & 7] = bpm; }
static void on_usb(const uint8_t *b, int n)
{
    for (int i = 0; i < n && f.nusb < 64; i++)
        f.usb_bytes[f.nusb++] = b[i];
}

static const struct midi_handlers handlers = { on_program, on_control, on_tempo, on_usb };

static void start(const struct midi_config *c)
{
    memset(&f, 0, sizeof f);
    f.now = 1000;
    midi_init(&handlers);
    if (c)
        midi_configure(c);
}

static void in(int port, const uint8_t *b, int n)
{
    memcpy(f.in[port] + f.in_len[port], b, (size_t)n);
    f.in_len[port] += n;
    midi_poll();
}

#define IN(port, ...) do { const uint8_t m_[] = { __VA_ARGS__ }; in(port, m_, (int)sizeof m_); } while (0)

static const struct midi_config factory = { { 0, 0 }, 2, 4, 0 };

/* ---------------------------------------------------------------- tests */

static void receive(void)
{
    start(&factory);
    IN(MIDI_TRS, 0xC0, 0x05);
    CHECK(f.nprog == 1 && f.programs[0] == 5, "program change: %d %d", f.nprog, f.programs[0]);
    CHECK(f.out_len[MIDI_TRS] == 2 && f.out[MIDI_TRS][0] == 0xC0 && f.out_len[MIDI_USB] == 2,
          "thru to both outputs: %d %d", f.out_len[MIDI_TRS], f.out_len[MIDI_USB]);

    IN(MIDI_TRS, 0xC0, 0x01, 0x02);         /* running status */
    CHECK(f.nprog == 3 && f.programs[1] == 1 && f.programs[2] == 2, "running status: %d", f.nprog);
    CHECK(f.out_len[MIDI_TRS] == 6 && f.out[MIDI_TRS][4] == 0xC0 && f.out[MIDI_TRS][5] == 0x02,
          "running status thru as whole messages");

    IN(MIDI_USB, 0xB0, 0xF8, 0x07, 0x64);   /* a real-time byte inside a message */
    CHECK(f.ncc == 1 && f.cc[0][0] == 7 && f.cc[0][1] == 100, "CC around a clock: %d", f.ncc);
    CHECK(f.out[MIDI_USB][f.out_len[MIDI_USB] - 4] == 0xF8, "the clock thru first");

    IN(MIDI_TRS, 0xB0, 0x00, 0x01, 0xC0, 0x05);     /* bank 1 */
    CHECK(f.programs[3] == 125, "bank 1: %d", f.programs[3]);
    IN(MIDI_TRS, 0xC0, 0x7F);
    CHECK(f.programs[4] == 239, "bank 1, program 127: %d", f.programs[4]);
    IN(MIDI_TRS, 0xB0, 0x00, 0x00, 0xC0, 0x7F);
    CHECK(f.programs[5] == 119, "bank 0, program 127: %d", f.programs[5]);
    int n = f.nprog;
    IN(MIDI_TRS, 0xB0, 0x00, 0x02, 0xC0, 0x05);
    CHECK(f.nprog == n, "bank 2: no preset");
    CHECK(f.ncc == 1, "CC 0 is not a control: %d", f.ncc);

    IN(MIDI_USB, 0xB0, 0x44, 0x02, 0xB0, 0x45, 0x0C);
    CHECK(f.ncc == 2 && f.cc[1][0] == 69 && f.cc[1][1] == (2 << 7 | 12), "tempo CC 68 / 69: %d %d", f.cc[1][0],
          f.cc[1][1]);

    int out = f.out_len[MIDI_TRS];
    IN(MIDI_USB, 0xF0, 0x21, 0x25, 0x4D, 0xF7, 0x05);
    CHECK(f.nusb >= 5 && f.usb_bytes[f.nusb - 6] == 0xF0, "USB bytes to the editor");
    CHECK(f.out_len[MIDI_TRS] == out && f.ncc == 2, "SysEx not sent on, its bytes not taken as data");

    IN(MIDI_TRS, 0xF2, 0x01, 0x02, 0x03);   /* song position, then a stray data byte */
    CHECK(f.out_len[MIDI_TRS] == out + 3, "system common thru, no running status: %d", f.out_len[MIDI_TRS] - out);
}

static void filters(void)
{
    struct midi_config c = factory;
    c.in_channel[MIDI_TRS] = 2;
    start(&c);
    IN(MIDI_TRS, 0xC0, 0x05);
    CHECK(f.nprog == 0 && f.out_len[MIDI_TRS] == 2, "other channel: sent on, not taken");
    IN(MIDI_TRS, 0xC1, 0x06);
    CHECK(f.nprog == 1 && f.programs[0] == 6, "input channel 2");
    IN(MIDI_USB, 0xC5, 0x07);
    CHECK(f.nprog == 2, "USB omni");

    c.source = 1;                           /* USB only */
    midi_configure(&c);
    IN(MIDI_TRS, 0xC1, 0x08);
    IN(MIDI_USB, 0xC0, 0x09);
    CHECK(f.nprog == 3 && f.programs[2] == 9, "source USB only: %d", f.nprog);

    c.source = 2;
    c.in_channel[MIDI_USB] = -1;            /* out of range: matches nothing (the original compares the low byte) */
    midi_configure(&c);
    IN(MIDI_USB, 0xC0, 0x01, 0xCF, 0x02);
    CHECK(f.nprog == 3, "channel -1");
}

static void clock_in(void)
{
    struct midi_config c = factory;
    c.clock_source = 1;                     /* TRS */
    start(&c);
    midi_set_tempo(120);
    for (int k = 0; k < 30; k++) {          /* 100 BPM: a clock every 25 ms */
        f.now += 25;
        IN(MIDI_TRS, 0xF8);
    }
    CHECK(f.ntempo == 1 && f.tempos[0] == 100, "100 BPM on TRS: %d %d", f.ntempo, f.tempos[0]);
    midi_set_tempo(100);
    for (int k = 0; k < 30; k++) {
        f.now += 25;
        IN(MIDI_TRS, 0xF8);
    }
    CHECK(f.ntempo == 1, "the same tempo again: not told");
    for (int k = 0; k < 60; k++) {          /* USB clock: not the clock source */
        f.now += 20;
        IN(MIDI_USB, 0xF8);
    }
    CHECK(f.ntempo == 1, "USB clock with the source on TRS");
    f.now += 5000;                          /* a pause, then 150 BPM (16.7 ms): no 40 BPM on the way */
    for (int k = 0; k < 40; k++) {
        f.now += (k % 3 == 2) ? 16 : 17;
        IN(MIDI_TRS, 0xF8);
    }
    CHECK(f.ntempo == 2 && f.tempos[1] >= 149 && f.tempos[1] <= 151, "150 BPM after a pause: %d %d", f.ntempo,
          f.tempos[1]);
    c.clock_source = 0;
    midi_configure(&c);
    for (int k = 0; k < 60; k++) {
        f.now += 25;
        IN(MIDI_TRS, 0xF8);
    }
    CHECK(f.ntempo == 2, "internal clock source");
}

static void clock_out(void)
{
    struct midi_config c = factory;
    c.clock_out = 3;
    start(&c);
    midi_set_tempo(120);
    uint32_t a = midi_clock_tick(), b = midi_clock_tick(), d = midi_clock_tick();
    CHECK(a == 20833 && b == 20833 && d == 20834, "120 BPM: %u %u %u", (unsigned)a, (unsigned)b, (unsigned)d);
    CHECK(f.clocks[MIDI_TRS] == 3 && f.clocks[MIDI_USB] == 3, "a clock on both");
    c.clock_out = 2;
    midi_configure(&c);
    midi_set_tempo(300);
    a = midi_clock_tick();
    CHECK(a == 8333 && f.clocks[MIDI_TRS] == 3 && f.clocks[MIDI_USB] == 4, "USB only, 300 BPM: %u", (unsigned)a);
}

void test_midi(void)
{
    receive();
    filters();
    clock_in();
    clock_out();
}
