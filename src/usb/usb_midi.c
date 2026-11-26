#include "usb/usb_midi.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "usb/usb_desc.h"
#include <string.h>

#define RX_RING     8192                    /* MIDI bytes, power of two */
#define TX_RING     1024                    /* event packets, power of two */
#define XFER_IN     256                     /* bytes per IN transfer: always a short packet */
#define RX_ROOM     (USB_XFER_MAX / 4 * 3)  /* the most MIDI bytes one OUT transfer can carry */

/* MIDI bytes carried by each code index number (USB MIDI 1.0, table 4-1) */
static const uint8_t cin_len[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };

static struct {
    volatile uint8_t up;
    volatile uint8_t rx_armed, tx_busy;
    volatile uint32_t busy_since;           /* time_ms() when the pending IN transfer was queued */
    uint8_t rx[RX_RING];
    volatile uint32_t rx_head, rx_tail;     /* head: USB interrupt, tail: main loop */
    uint32_t tx[TX_RING];
    volatile uint32_t tx_head, tx_tail;     /* head: main loop, tail: USB interrupt */
    volatile uint32_t clk_posted;           /* timing clocks: posted by the clock interrupt, */
    uint32_t clk_sent;                      /* sent by the USB interrupt (or the main loop holding it off) */
    uint8_t xfer[XFER_IN];
} um;

void usb_midi_init(void)
{
    memset(&um, 0, sizeof um);
}

/* both contexts, with the interrupt held off when called from the main loop */
static void arm_rx(void)
{
    if (um.up && !um.rx_armed && RX_RING - (um.rx_head - um.rx_tail) >= RX_ROOM &&
        usb_dcd_xfer(USB_EP_MIDI_OUT, 0, USB_XFER_MAX) == 0)
        um.rx_armed = 1;
}

static void kick_tx(void)
{
    uint32_t clocks = um.clk_posted - um.clk_sent;
    if (!um.up || um.tx_busy || (um.tx_head == um.tx_tail && !clocks))
        return;
    uint32_t n = 0, tail = um.tx_tail;
    static const uint8_t clock_event[4] = { 0x0f, 0xf8, 0, 0 };
    for (uint32_t k = 0; k < clocks && n + 4 <= XFER_IN; k++) {
        memcpy(um.xfer + n, clock_event, 4);
        n += 4;
    }
    uint32_t clocks_in = n / 4;
    while (tail != um.tx_head && n + 4 <= XFER_IN) {
        memcpy(um.xfer + n, &um.tx[tail & (TX_RING - 1)], 4);
        tail++;
        n += 4;
    }
    if (usb_dcd_xfer(USB_EP_MIDI_IN, um.xfer, n) == 0) {
        um.tx_tail = tail;
        um.clk_sent += clocks_in;
        um.busy_since = time_ms();
        um.tx_busy = 1;
    }
}

uint32_t usb_midi_tx_stalled_ms(void)
{
    return um.up && um.tx_busy ? time_ms() - um.busy_since : 0;
}

void usb_midi_service(void)
{
    kick_tx();
}

void usb_midi_clock(void)
{
    if (!um.up)
        return;
    um.clk_posted++;
    usb_dcd_wake();
}

void usb_midi_start(void)
{
    usb_dcd_ep_open(USB_EP_MIDI_OUT, USB_EP_BULK, 512, 1);
    usb_dcd_ep_open(USB_EP_MIDI_IN, USB_EP_BULK, 512, 1);
    um.rx_armed = um.tx_busy = 0;
    um.up = 1;
    arm_rx();
    kick_tx();
}

void usb_midi_stop(void)
{
    if (um.up) {
        usb_dcd_ep_close(USB_EP_MIDI_OUT);
        usb_dcd_ep_close(USB_EP_MIDI_IN);
    }
    um.up = 0;
    um.rx_armed = um.tx_busy = 0;
    um.tx_tail = um.tx_head;                /* whatever was waiting goes with the connection */
    um.clk_sent = um.clk_posted;
}

void usb_midi_xfer_done(uint8_t ep, const uint8_t *data, uint32_t len, int ok)
{
    if (ep == USB_EP_MIDI_OUT) {
        um.rx_armed = 0;
        uint32_t head = um.rx_head;
        for (uint32_t i = 0; ok && i + 4 <= len; i += 4) {
            int n = cin_len[data[i] & 0x0f];
            for (int k = 0; k < n; k++)
                um.rx[(head++) & (RX_RING - 1)] = data[i + 1 + k];
        }
        um.rx_head = head;
        arm_rx();
    } else if (ep == USB_EP_MIDI_IN) {
        um.tx_busy = 0;
        kick_tx();
    }
}

int usb_midi_ready(void)
{
    return um.up;
}

int usb_midi_read(uint8_t *buf, int max)
{
    int n = 0;
    uint32_t tail = um.rx_tail;
    while (n < max && tail != um.rx_head)
        buf[n++] = um.rx[(tail++) & (RX_RING - 1)];
    um.rx_tail = tail;
    if (!um.rx_armed && um.up) {
        usb_dcd_lock();
        arm_rx();
        usb_dcd_unlock();
    }
    return n;
}

int usb_midi_tx_free(void)
{
    return (int)(TX_RING - (um.tx_head - um.tx_tail)) * 3;
}

static void put_event(uint32_t *head, uint8_t cin, const uint8_t *b, int n)
{
    uint8_t e[4] = { cin, 0, 0, 0 };
    memcpy(e + 1, b, (unsigned)n);
    memcpy(&um.tx[(*head)++ & (TX_RING - 1)], e, 4);
}

int usb_midi_write(const uint8_t *msg, int len)
{
    if (len <= 0 || !um.up)
        return -1;
    int events = msg[0] == 0xF0 ? (len + 2) / 3 : 1;
    if ((uint32_t)events > TX_RING - (um.tx_head - um.tx_tail))
        return -1;
    uint32_t head = um.tx_head;
    if (msg[0] == 0xF0) {                   /* SysEx: 3 bytes per packet, the last one says how many end it */
        int i = 0;
        while (len - i > 3) {
            put_event(&head, 0x4, msg + i, 3);
            i += 3;
        }
        put_event(&head, (uint8_t)(0x4 + (len - i)), msg + i, len - i);
    } else if (msg[0] >= 0x80 && msg[0] < 0xF0) {
        put_event(&head, (uint8_t)(msg[0] >> 4), msg, len > 3 ? 3 : len);
    } else {                                /* system common / real time */
        static const uint8_t sys_cin[3] = { 0xF, 0x2, 0x3 };
        int n = len > 3 ? 3 : len;
        put_event(&head, msg[0] >= 0xF8 ? 0xF : sys_cin[n - 1], msg, n);
    }
    usb_dcd_lock();
    um.tx_head = head;
    kick_tx();
    usb_dcd_unlock();
    return 0;
}
