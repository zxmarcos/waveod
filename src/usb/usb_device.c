#include "usb/usb_device.h"
#include "platform/time.h"
#include "platform/usb.h"
#include "usb/usb_audio.h"
#include "usb/usb_desc.h"
#include "usb/usb_midi.h"
#include <string.h>

#define EP0_SIZE        64
#define REATTACH_MS     300                 /* off the bus long enough for the host to notice */

/* bmRequestType */
#define DIR_IN          0x80
#define TYPE_MASK       0x60
#define TYPE_STANDARD   0x00
#define TYPE_CLASS      0x20
#define RCPT_MASK       0x1f
#define RCPT_DEVICE     0
#define RCPT_INTERFACE  1
#define RCPT_ENDPOINT   2

enum { GET_STATUS = 0, CLEAR_FEATURE = 1, SET_FEATURE = 3, SET_ADDRESS = 5, GET_DESCRIPTOR = 6,
       GET_CONFIGURATION = 8, SET_CONFIGURATION = 9, GET_INTERFACE = 10, SET_INTERFACE = 11 };
enum { DESC_DEVICE = 1, DESC_CONFIG = 2, DESC_STRING = 3, DESC_QUALIFIER = 6 };

static struct {
    volatile uint8_t mode;                  /* enum usb_mode the descriptors describe */
    volatile uint8_t want_mode;
    volatile uint8_t configured;
    uint8_t alt[4];                         /* alternate setting of each interface */
    uint8_t halted[2];                      /* bit n: endpoint n halted (OUT, IN) */
    uint32_t off_since;                     /* main loop: when the bus was left for a mode change */
    uint8_t reattach;
    struct usb_setup setup;                 /* the control transfer in progress */
    uint8_t out_pending;                    /* waiting for the data stage of a class OUT request */
    uint8_t buf[USB_XFER_MAX];
} dev;

static const uint8_t *config_desc(int *len)
{
    if (dev.mode == USB_MODE_2X2) {
        *len = USB_DESC_CONFIG_2X2_LEN;
        return usb_desc_config_2x2;
    }
    *len = USB_DESC_CONFIG_6X4_LEN;
    return usb_desc_config_6x4;
}

static void stall(void)
{
    usb_dcd_stall(0x00, 1);
}

static void status_in(void)
{
    usb_dcd_xfer(0x80, 0, 0);
}

/* data stage IN, then the host's zero-length OUT as the status stage */
static void reply(const void *data, int len)
{
    if (len > dev.setup.length)
        len = dev.setup.length;
    usb_dcd_xfer(0x80, data, (uint32_t)len);
    if (len < dev.setup.length && len && len % EP0_SIZE == 0)
        usb_dcd_xfer(0x80, 0, 0);           /* a short reply that ends on a packet boundary */
    usb_dcd_xfer(0x00, 0, 0);
}

static int interfaces(void)
{
    return dev.mode == USB_MODE_2X2 ? 3 : 4;
}

static int max_alt(int iface)
{
    if (iface == USB_IF_AUDIO_REC || iface == USB_IF_AUDIO_PLAY)
        return dev.mode == USB_MODE_2X2 ? 1 : 2;
    return 0;
}

static void set_configuration(int on)
{
    if (dev.configured) {
        usb_audio_set_interface(USB_IF_AUDIO_REC, 0);
        usb_audio_set_interface(USB_IF_AUDIO_PLAY, 0);
        usb_midi_stop();
    }
    memset(dev.alt, 0, sizeof dev.alt);
    dev.halted[0] = dev.halted[1] = 0;
    dev.configured = (uint8_t)on;
    if (on && dev.mode == USB_MODE_6X4)
        usb_midi_start();
}

static int endpoint_valid(uint8_t ep)
{
    if (!dev.configured)
        return (ep & 0x7f) == 0;
    switch (ep) {
    case 0x00: case 0x80: case USB_EP_AUDIO_REC: case USB_EP_AUDIO_PLAY:
        return 1;
    case USB_EP_MIDI_OUT: case USB_EP_MIDI_IN:
        return dev.mode == USB_MODE_6X4;
    }
    return 0;
}

static int standard_request(const struct usb_setup *s)
{
    int rcpt = s->type & RCPT_MASK;
    uint8_t ep = (uint8_t)s->index;
    switch (s->request) {
    case GET_STATUS:
        dev.buf[0] = dev.buf[1] = 0;
        if (rcpt == RCPT_DEVICE)
            dev.buf[0] = 1;                 /* self powered */
        else if (rcpt == RCPT_ENDPOINT) {
            if (!endpoint_valid(ep))
                return -1;
            dev.buf[0] = (uint8_t)(dev.halted[ep >> 7] >> (ep & 0x0f) & 1);
        } else if (rcpt != RCPT_INTERFACE || !dev.configured || s->index >= interfaces())
            return -1;
        reply(dev.buf, 2);
        return 0;
    case CLEAR_FEATURE:
    case SET_FEATURE:
        if (rcpt == RCPT_DEVICE && s->value == 1) {            /* remote wake-up: not supported, accepted */
            status_in();
            return 0;
        }
        if (rcpt != RCPT_ENDPOINT || s->value != 0 || !endpoint_valid(ep) || !(ep & 0x0f))
            return -1;
        if (s->request == SET_FEATURE)
            dev.halted[ep >> 7] |= (uint8_t)(1u << (ep & 0x0f));
        else
            dev.halted[ep >> 7] &= (uint8_t)~(1u << (ep & 0x0f));
        usb_dcd_stall(ep, s->request == SET_FEATURE);
        status_in();
        return 0;
    case SET_ADDRESS:
        if (s->value > 127)
            return -1;
        usb_dcd_set_address((uint8_t)s->value);
        status_in();
        return 0;
    case GET_DESCRIPTOR: {
        int type = s->value >> 8, index = s->value & 0xff, len = 0;
        const uint8_t *d = 0;
        if (type == DESC_DEVICE) {
            d = usb_desc_device;
            len = sizeof usb_desc_device;
        } else if (type == DESC_CONFIG && index == 0) {
            d = config_desc(&len);
        } else if (type == DESC_QUALIFIER) {
            d = usb_desc_qualifier;
            len = sizeof usb_desc_qualifier;
        } else if (type == DESC_STRING) {
            len = usb_desc_string(index, dev.buf, sizeof dev.buf);
            d = dev.buf;
        }
        if (!d || len < 0)
            return -1;
        reply(d, len);
        return 0;
    }
    case GET_CONFIGURATION:
        dev.buf[0] = dev.configured;
        reply(dev.buf, 1);
        return 0;
    case SET_CONFIGURATION:
        if (s->value > 1)
            return -1;
        set_configuration(s->value == 1);
        status_in();
        return 0;
    case GET_INTERFACE:
        if (!dev.configured || s->index >= interfaces())
            return -1;
        dev.buf[0] = dev.alt[s->index];
        reply(dev.buf, 1);
        return 0;
    case SET_INTERFACE:
        if (!dev.configured || s->index >= interfaces() || s->value > max_alt(s->index))
            return -1;
        dev.alt[s->index] = (uint8_t)s->value;
        if (s->index == USB_IF_AUDIO_REC || s->index == USB_IF_AUDIO_PLAY)
            usb_audio_set_interface(s->index, s->value);
        status_in();
        return 0;
    }
    return -1;
}

static void on_setup(const uint8_t raw[8])
{
    struct usb_setup *s = &dev.setup;
    s->type = raw[0];
    s->request = raw[1];
    s->value = (uint16_t)(raw[2] | raw[3] << 8);
    s->index = (uint16_t)(raw[4] | raw[5] << 8);
    s->length = (uint16_t)(raw[6] | raw[7] << 8);
    dev.out_pending = 0;
    int r = -1;
    if ((s->type & TYPE_MASK) == TYPE_STANDARD) {
        r = standard_request(s);
    } else if ((s->type & TYPE_MASK) == TYPE_CLASS && (s->type & RCPT_MASK) == RCPT_INTERFACE &&
               (s->index & 0xff) == USB_IF_AUDIO_CONTROL && s->length <= sizeof dev.buf) {
        if (s->type & DIR_IN) {
            int n = usb_audio_request(s, dev.buf, s->length);
            if (n >= 0) {
                reply(dev.buf, n);
                r = 0;
            }
        } else if (s->length) {             /* the data stage first, the request when it arrives */
            dev.out_pending = 1;
            usb_dcd_xfer(0x00, 0, s->length);
            r = 0;
        } else if (usb_audio_request(s, dev.buf, 0) >= 0) {
            status_in();
            r = 0;
        }
    }
    if (r < 0)
        stall();
}

static void on_done(uint8_t ep, const uint8_t *data, uint32_t len, int ok)
{
    if (ep == 0x00) {
        if (dev.out_pending && ok) {        /* the data stage of a class OUT request */
            dev.out_pending = 0;
            memcpy(dev.buf, data, len);
            if (usb_audio_request(&dev.setup, dev.buf, (int)len) >= 0)
                status_in();
            else
                stall();
        }
        return;
    }
    if (ep == 0x80)
        return;
    if (ep == USB_EP_MIDI_OUT || ep == USB_EP_MIDI_IN)
        usb_midi_xfer_done(ep, data, len, ok);
    else
        usb_audio_xfer_done(ep, data, len, ok);
}

static void on_reset(void)
{
    dev.configured = 0;
    dev.out_pending = 0;
    memset(dev.alt, 0, sizeof dev.alt);
    usb_audio_reset();
    usb_midi_stop();
    usb_dcd_ep_open(0x00, USB_EP_CONTROL, EP0_SIZE, 1);
    usb_dcd_ep_open(0x80, USB_EP_CONTROL, EP0_SIZE, 2);
}

static void on_suspend(int suspended)
{
    (void)suspended;
}

static const struct usb_dcd_events events = { on_reset, on_setup, on_done, on_suspend, usb_midi_service };

void usb_device_init(enum usb_mode mode)
{
    memset(&dev, 0, sizeof dev);
    dev.mode = dev.want_mode = (uint8_t)mode;
    usb_audio_init();
    usb_midi_init();
    usb_dcd_init(&events);
    on_reset();
    usb_dcd_connect(1);
}

void usb_device_set_mode(enum usb_mode mode)
{
    if (mode == dev.want_mode)
        return;
    dev.want_mode = (uint8_t)mode;
    usb_dcd_connect(0);
    set_configuration(0);
    dev.off_since = time_ms();
    dev.reattach = 1;
}

void usb_device_poll(void)
{
    if (dev.reattach && time_ms() - dev.off_since >= REATTACH_MS) {
        dev.reattach = 0;
        dev.mode = dev.want_mode;
        usb_dcd_connect(1);
    }
}

int usb_device_configured(void)
{
    return dev.configured;
}

enum usb_mode usb_device_mode(void)
{
    return (enum usb_mode)dev.mode;
}
