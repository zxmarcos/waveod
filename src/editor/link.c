#include "editor/link.h"
#include "editor/lzo.h"
#include "platform/time.h"
#include "usb/usb_midi.h"
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define SYSEX_MAX     512
#define CHUNK_OUT     0xB9                  /* bytes per packet the original sends (FUN_00031BF4) */
#define GAP_MS        3
#define TXQ           32768                 /* queued transfers: {flag, u16 len, bytes} */
#define HEADER        13                    /* F0 .. off_hi */
#define CATCH_UP      8                     /* packets at most per poll */
#define AWAY_MS       1000                  /* the host is not listening (link_away) after this long */

static const uint8_t mp_header[6] = { 0x21, 0x25, 0x4D, 0x50, 0x00, 0x00 };

static struct {
    link_handler handler;
    /* reception */
    uint8_t sysex[SYSEX_MAX];
    int sysex_len, in_sysex;
    uint8_t rx[LINK_MAX + 1];
    uint16_t rx_total, rx_got;              /* the original's +0x414 / +0x416 */
    /* sending */
    uint8_t txq[TXQ];
    uint32_t tx_head, tx_tail;              /* bytes in txq */
    uint8_t cur[LINK_MAX + 1];              /* the transfer being sent */
    uint16_t cur_len, cur_off;
    uint8_t cur_flag, cur_busy;
    uint32_t last_ms, sent;
    uint32_t rx_ms;                         /* when the host last sent something */
    uint8_t zbuf[LINK_MAX + 1];
} lk SDRAM_BSS;

/*
 * Nobody listening: the host has not taken what the pedal sent for AWAY_MS (it polls the IN endpoint only while
 * an application has the MIDI input open) and has sent nothing either. Then nothing is queued: notifications of
 * knobs and presets would pile up (32 KB, several seconds at one packet per 3 ms) in front of the answer to the
 * next application's first request, which then timed out.
 */
static int link_away(void)
{
    return usb_midi_tx_stalled_ms() > AWAY_MS && time_ms() - lk.rx_ms > AWAY_MS;
}

void link_init(link_handler handler)
{
    memset(&lk, 0, sizeof lk);
    lk.handler = handler;
}

int link_up(void)
{
    return usb_midi_ready();
}

static uint8_t nibble_sum(const uint8_t *nib, int n)
{
    unsigned s = 0;
    for (int i = 0; i < n; i++)
        s += nib[i];
    return (uint8_t)(s & 0x7f);
}

/* F0 21 25 4D 50 00 00 00 13 len len off off F7 (FUN_00031A08) */
static void send_nak(const uint8_t *p, unsigned off)
{
    uint8_t nak[14] = { 0xF0, 0x21, 0x25, 0x4D, 0x50, p[5], p[6], 0x00, LINK_NAK, p[9], p[10],
                        (uint8_t)(off & 0x7f), (uint8_t)(off >> 7 & 0x7f), 0xF7 };
    usb_midi_write(nak, sizeof nak);
}

static void packet(const uint8_t *p, int len)
{
    int nbytes = (len - 14) / 2;
    if (len < 14 || p[3] != 0x4D || p[4] != 0x50 || nbytes < 1)
        return;
    unsigned total = p[9] | p[10] << 7, off = p[11] | p[12] << 7;
    uint8_t cs = p[7], flag = p[8];
    if (lk.rx_got != off || off == 0) {     /* out of sequence (or a new transfer) */
        if (lk.rx_got != off) {
            lk.rx_got = lk.rx_total = 0;
            send_nak(p, off);
            if (off != 0)
                return;
        }
        lk.rx_total = (uint16_t)total;
        lk.rx_got = 0;
    }
    unsigned got = lk.rx_got + (unsigned)nbytes;
    if (got > lk.rx_total || got > LINK_MAX) {
        lk.rx_total = lk.rx_got = 0;
        return;
    }
    if (cs != nibble_sum(p + 13, 2 * nbytes) && cs >= 6) {
        lk.rx_got = 0;
        send_nak(p, off);
        return;
    }
    for (int i = 0; i < nbytes; i++)
        lk.rx[off + (unsigned)i] = (uint8_t)(p[13 + 2 * i] << 4 | (p[14 + 2 * i] & 0x0f));
    lk.rx_got = (uint16_t)got;
    if (lk.rx_got == lk.rx_total) {
        int n = lk.rx_total;
        lk.rx_total = lk.rx_got = 0;
        if (lk.handler)
            lk.handler(lk.rx, n, flag);
    }
}

void link_feed(const uint8_t *buf, int n)
{
    if (n > 0)
        lk.rx_ms = time_ms();
    for (int i = 0; i < n; i++) {
        uint8_t b = buf[i];
        if (b == 0xF0) {
            lk.in_sysex = 1;
            lk.sysex_len = 0;
        } else if (b >= 0xF8) {
            continue;                       /* real time: allowed inside SysEx */
        } else if (b & 0x80 && b != 0xF7) {
            lk.in_sysex = 0;                /* another message: the SysEx was cut short */
            continue;
        }
        if (!lk.in_sysex)
            continue;
        if (lk.sysex_len < SYSEX_MAX)
            lk.sysex[lk.sysex_len++] = b;
        if (b == 0xF7) {
            lk.in_sysex = 0;
            if (lk.sysex_len < SYSEX_MAX)
                packet(lk.sysex, lk.sysex_len);
        }
    }
}

/* ---------------------------------------------------------------- sending */

static uint32_t txq_used(void)
{
    return lk.tx_head - lk.tx_tail;
}

static void txq_put(const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        lk.txq[(lk.tx_head + i) % TXQ] = src[i];
    lk.tx_head += n;
}

static void txq_get(uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dst[i] = lk.txq[(lk.tx_tail + i) % TXQ];
    lk.tx_tail += n;
}

int link_send_raw(const void *msg, uint32_t len, int flag)
{
    if (!usb_midi_ready() || len == 0 || len > LINK_MAX || txq_used() + len + 3 > TXQ || link_away())
        return -1;
    uint8_t h[3] = { (uint8_t)flag, (uint8_t)len, (uint8_t)(len >> 8) };
    txq_put(h, 3);
    txq_put(msg, len);
    return 0;
}

int link_send(uint32_t cmd, const void *payload, uint32_t n)
{
    if (n + n / 255 + 21 + 8 > LINK_MAX)
        return -1;
    uint32_t z = lzo1x_store(payload, n, lk.zbuf + 8);
    memcpy(lk.zbuf, &cmd, 4);
    memcpy(lk.zbuf + 4, &z, 4);
    return link_send_raw(lk.zbuf, z + 8, LINK_DATA);
}

/* one packet of the current transfer (FUN_00031BF4) */
static void send_next(void)
{
    if (!lk.cur_busy) {
        if (txq_used() < 3)
            return;
        uint8_t h[3];
        txq_get(h, 3);
        lk.cur_flag = h[0];
        lk.cur_len = (uint16_t)(h[1] | h[2] << 8);
        txq_get(lk.cur, lk.cur_len);
        lk.cur_off = 0;
        lk.cur_busy = 1;
    }
    unsigned n = lk.cur_len - lk.cur_off;
    if (n > CHUNK_OUT)
        n = CHUNK_OUT;
    if (usb_midi_tx_free() < (int)(HEADER + 2 * n + 1))
        return;
    uint8_t p[HEADER + 2 * CHUNK_OUT + 1];
    p[0] = 0xF0;
    memcpy(p + 1, mp_header, 6);
    for (unsigned i = 0; i < n; i++) {
        uint8_t b = lk.cur[lk.cur_off + i];
        p[HEADER + 2 * i] = b >> 4;
        p[HEADER + 2 * i + 1] = b & 0x0f;
    }
    p[7] = nibble_sum(p + HEADER, (int)(2 * n));
    p[8] = lk.cur_flag;
    p[9] = (uint8_t)(lk.cur_len & 0x7f);
    p[10] = (uint8_t)(lk.cur_len >> 7 & 0x7f);
    p[11] = (uint8_t)(lk.cur_off & 0x7f);
    p[12] = (uint8_t)(lk.cur_off >> 7 & 0x7f);
    p[HEADER + 2 * n] = 0xF7;
    if (usb_midi_write(p, (int)(HEADER + 2 * n + 1)) != 0)
        return;
    lk.cur_off = (uint16_t)(lk.cur_off + n);
    lk.last_ms = time_ms();
    lk.sent++;
    if (lk.cur_off >= lk.cur_len)
        lk.cur_busy = 0;
}

void link_poll(void)
{
    if (!usb_midi_ready()) {                /* a new connection starts clean */
        lk.tx_head = lk.tx_tail = 0;
        lk.cur_busy = 0;
        lk.rx_total = lk.rx_got = 0;
        lk.in_sysex = 0;
        return;
    }
    if (link_away())                        /* what waits is stale by the time anyone reads it */
        lk.tx_tail = lk.tx_head;
    /* one packet per 3 ms: a slow main loop catches up with a few at once */
    uint32_t due = (time_ms() - lk.last_ms) / GAP_MS;
    for (uint32_t k = 0; k < due && k < CATCH_UP; k++) {
        uint32_t sent = lk.sent;
        send_next();
        if (lk.sent == sent)
            break;
    }
}
