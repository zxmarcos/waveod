/*
 * USB device controller: the hardware side of the USB stack (src/usb). The stack above is the same on the
 * pedal (board/qme200/usb_dcd.c, the RT1062's ChipIdea controller) and in the host build (host/host_usb.c, a
 * scripted host for the tests).
 *
 * Endpoint addresses are USB ones: bit 7 = IN (device to host). Transfers are queued per endpoint and
 * complete in order; buffers are copied by the controller driver, so the caller's memory can be reused at
 * once (IN) or is filled on completion (OUT). Every callback runs in the USB interrupt.
 */
#pragma once

#include <stdint.h>

enum usb_ep_type { USB_EP_CONTROL, USB_EP_ISO, USB_EP_BULK, USB_EP_INTERRUPT };

/* the most a single transfer may carry: the control data stage, a bulk packet */
#define USB_XFER_MAX   512

struct usb_dcd_events {
    void (*bus_reset)(void);                                /* address 0, only endpoint 0 open */
    void (*setup)(const uint8_t setup[8]);
    /* a queued transfer finished: `len` bytes moved (OUT: `data` holds them), ok = 0 on an error */
    void (*done)(uint8_t ep, const uint8_t *data, uint32_t len, int ok);
    void (*suspend)(int suspended);
    void (*service)(void);                                  /* after every interrupt, usb_dcd_wake included */
};

void usb_dcd_init(const struct usb_dcd_events *ev);

/* attach to the bus (pull-up on) or leave it */
void usb_dcd_connect(int on);

/* 1 once a high-speed host has reset the port */
int usb_dcd_high_speed(void);

/* the address takes effect after the status stage of SET_ADDRESS */
void usb_dcd_set_address(uint8_t addr);

/* open an endpoint (data toggles reset); `depth` transfers can be queued at once */
int usb_dcd_ep_open(uint8_t ep, enum usb_ep_type type, uint16_t max_packet, int depth);
void usb_dcd_ep_close(uint8_t ep);

/* queue one transfer (IN: `len` bytes of `data`; OUT: up to `len` bytes); 0 = queued, -1 = queue full */
int usb_dcd_xfer(uint8_t ep, const void *data, uint32_t len);

/* transfers still queued on an endpoint */
int usb_dcd_pending(uint8_t ep);

/* protocol stall of endpoint 0 (cleared by the next SETUP) or functional halt of another endpoint */
void usb_dcd_stall(uint8_t ep, int on);

/* the bus frame / microframe counter (FRINDEX), for diagnostics */
uint32_t usb_dcd_frame(void);

/* main loop code calling the functions above holds the USB interrupt off meanwhile (nests) */
void usb_dcd_lock(void);
void usb_dcd_unlock(void);

/* any context: run the USB interrupt (its `service` event) soon, e.g. for data a lower interrupt left */
void usb_dcd_wake(void);

/* for the diagnostics: what the controller saw since start-up */
struct usb_dcd_counts {
    uint32_t resets, setups, completions, errors;
    uint32_t last_setup;                    /* first 4 bytes of the last SETUP (type, request, value) */
    uint8_t high_speed;
};
void usb_dcd_counts(struct usb_dcd_counts *out);
