/*
 * Host build: the USB device controller replaced by a scripted host (host/host.h). Transfers the stack queues
 * wait until the test moves data: host_usb_control runs a whole control transfer, host_usb_out fills the
 * oldest queued OUT transfer of an endpoint, host_usb_in takes the oldest queued IN transfer. Completion
 * callbacks run synchronously, as the interrupt would.
 */
#include "host/host.h"
#include "platform/usb.h"
#include <string.h>

#define EPS     32
#define DEPTH   8

struct xfer {
    uint32_t len;
    uint8_t data[USB_XFER_MAX];
};

static struct {
    uint8_t open, depth, head, count, stalled;
    struct xfer q[DEPTH];
} eps[EPS];

static const struct usb_dcd_events *events;
static uint8_t connected, address;

static int idx(uint8_t ep) { return (ep & 0x0f) * 2 + (ep >> 7); }

void usb_dcd_init(const struct usb_dcd_events *ev)
{
    events = ev;
    memset(eps, 0, sizeof eps);
}

void usb_dcd_connect(int on) { connected = (uint8_t)on; }
int usb_dcd_high_speed(void) { return 1; }
void usb_dcd_set_address(uint8_t addr) { address = addr; }
uint32_t usb_dcd_frame(void) { return 0; }
void usb_dcd_lock(void) {}
void usb_dcd_unlock(void) {}

void usb_dcd_counts(struct usb_dcd_counts *out)
{
    memset(out, 0, sizeof *out);
    out->high_speed = 1;
}

void usb_dcd_wake(void)
{
    if (events && events->service)
        events->service();
}

int usb_dcd_ep_open(uint8_t ep, enum usb_ep_type type, uint16_t max_packet, int depth)
{
    (void)type;
    (void)max_packet;
    int i = idx(ep);
    eps[i].open = 1;
    eps[i].depth = (uint8_t)(depth > DEPTH ? DEPTH : depth);
    eps[i].head = eps[i].count = eps[i].stalled = 0;
    return 0;
}

void usb_dcd_ep_close(uint8_t ep)
{
    int i = idx(ep);
    eps[i].open = 0;
    eps[i].head = eps[i].count = 0;
}

int usb_dcd_pending(uint8_t ep)
{
    return eps[idx(ep)].count;
}

int usb_dcd_xfer(uint8_t ep, const void *data, uint32_t len)
{
    int i = idx(ep);
    if (!eps[i].open || eps[i].count >= eps[i].depth || len > USB_XFER_MAX)
        return -1;
    struct xfer *x = &eps[i].q[(eps[i].head + eps[i].count) % eps[i].depth];
    x->len = len;
    if ((ep & 0x80) && len)
        memcpy(x->data, data, len);
    eps[i].count++;
    return 0;
}

void usb_dcd_stall(uint8_t ep, int on)
{
    eps[idx(ep)].stalled = (uint8_t)on;
}

/* ---------------------------------------------------------------- the test's side */

void host_usb_reset(void)
{
    for (int i = 2; i < EPS; i++)
        eps[i].open = eps[i].count = 0;
    eps[0].count = eps[1].count = 0;
    if (events && events->bus_reset)
        events->bus_reset();
}

int host_usb_connected(void)
{
    return connected;
}

/* the oldest queued IN transfer: its bytes into buf; -1 = nothing queued */
int host_usb_in(uint8_t ep, uint8_t *buf, int max)
{
    int i = idx(ep | 0x80);
    if (!eps[i].count)
        return -1;
    struct xfer *x = &eps[i].q[eps[i].head];
    int n = (int)x->len < max ? (int)x->len : max;
    if (buf && n)
        memcpy(buf, x->data, (size_t)n);
    uint8_t data[USB_XFER_MAX];
    memcpy(data, x->data, x->len);
    uint32_t len = x->len;
    eps[i].head = (uint8_t)((eps[i].head + 1) % eps[i].depth);
    eps[i].count--;
    if (events && events->done)
        events->done((uint8_t)(ep | 0x80), data, len, 1);
    return n;
}

/* data into the oldest queued OUT transfer; -1 = none queued (the device would NAK) */
int host_usb_out(uint8_t ep, const uint8_t *data, int len)
{
    int i = idx(ep & 0x7f);
    if (!eps[i].count)
        return -1;
    struct xfer *x = &eps[i].q[eps[i].head];
    uint32_t n = (uint32_t)len < x->len ? (uint32_t)len : x->len;
    uint8_t copy[USB_XFER_MAX];
    if (n)
        memcpy(copy, data, n);
    eps[i].head = (uint8_t)((eps[i].head + 1) % eps[i].depth);
    eps[i].count--;
    if (events && events->done)
        events->done((uint8_t)(ep & 0x7f), copy, n, 1);
    return (int)n;
}

int host_usb_control(const uint8_t setup[8], const uint8_t *out, uint8_t *in, int max)
{
    eps[0].stalled = eps[1].stalled = 0;
    eps[0].count = eps[1].count = 0;
    if (events && events->setup)
        events->setup(setup);
    if (eps[0].stalled || eps[1].stalled)
        return -1;
    int len = setup[6] | setup[7] << 8, got = 0;
    if (setup[0] & 0x80) {                  /* data IN, then status OUT */
        while (eps[1].count) {
            int n = host_usb_in(0, in ? in + got : 0, max - got);
            if (n > 0)
                got += n;
        }
        if (eps[0].stalled || eps[1].stalled || host_usb_out(0, 0, 0) < 0)
            return -1;
        return got;
    }
    if (len && host_usb_out(0, out, len) < 0)
        return -1;
    if (eps[0].stalled || eps[1].stalled || host_usb_in(0, 0, 0) < 0)
        return -1;
    return 0;
}

uint8_t host_usb_address(void)
{
    return address;
}
