/*
 * USB device core: enumeration (the standard requests of chapter 9) on the control endpoint, for the
 * configuration of the global USB mode (usb_desc.h); audio class requests and streaming go to usb_audio.c,
 * MIDI streaming to usb_midi.c. Interrupt context except where noted.
 */
#pragma once

#include <stdint.h>

enum usb_mode { USB_MODE_6X4, USB_MODE_2X2 };

struct usb_setup {
    uint8_t type;                   /* bmRequestType */
    uint8_t request;
    uint16_t value, index, length;
};

/* main loop: bring the controller up and attach with the configuration of `mode` */
void usb_device_init(enum usb_mode mode);

/* main loop: another mode leaves the bus and comes back (the host enumerates the new configuration) */
void usb_device_set_mode(enum usb_mode mode);

/* main loop: the reattachment after a mode change */
void usb_device_poll(void);

int usb_device_configured(void);
enum usb_mode usb_device_mode(void);
