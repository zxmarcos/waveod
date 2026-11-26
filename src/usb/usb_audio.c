#include "usb/usb_audio.h"
#include "platform/usb.h"
#include "usb/usb_desc.h"
#include <string.h>

#define RING            256                 /* frames, power of two */
#define ISO_DEPTH       8                   /* transfers queued per endpoint: 1 ms of microframes */
#define ISO_MAX         256                 /* wMaxPacketSize of both endpoints */
#define REC_TARGET      48                  /* frames the recording ring keeps on average */
#define PLAY_START      80                  /* frames buffered before the playback starts */
#define RATE_NOMINAL    361267u             /* 44100 / 8000 frames per microframe, 16.16 */

/* UAC2 */
#define REQ_CUR         1
#define REQ_RANGE       2
#define CS_FREQ         1
#define CS_VALID        2
#define CS_MUTE         1
#define CS_VOLUME       2

struct feature {                            /* FUN_0003880C: per channel (0 = master) mute and volume 1..100 */
    uint8_t mute[7];
    uint16_t volume[7];
};

static struct {
    /* recording: audio interrupt writes, USB interrupt reads */
    int32_t rec[RING][USB_REC_CHANNELS];
    volatile uint32_t rec_head, rec_tail;
    uint8_t rec_alt, rec_fmt;
    uint32_t rate_acc, avg_fill;            /* 16.16 */
    /* playback: USB interrupt writes, audio interrupt reads */
    int32_t play[RING][USB_PLAY_CHANNELS];
    volatile uint32_t play_head, play_tail;
    uint8_t play_alt, play_fmt, play_started;
    int32_t play_last[USB_PLAY_CHANNELS];
    struct feature rec_fu, play_fu;
    volatile float rec_gain[USB_REC_CHANNELS], play_gain[USB_PLAY_CHANNELS];
    uint32_t freq;
    struct usb_audio_stats st;
} ua;

enum { FMT_NONE, FMT_STEREO32, FMT_16, FMT_32 };    /* the original's modes 1..3 */

static int format_of(int alt)
{
    if (!alt)
        return FMT_NONE;
    if (usb_device_mode() == USB_MODE_2X2)
        return FMT_STEREO32;
    return alt == 1 ? FMT_16 : FMT_32;
}

static float fu_gain(const struct feature *f, int ch)
{
    /* FUN_00036D54 */
    if (f->mute[0] || f->mute[ch])
        return 0.0f;
    return ((float)f->volume[0] / 100.0f) * ((float)f->volume[ch] / 100.0f);
}

static void update_gains(void)
{
    for (int k = 0; k < USB_REC_CHANNELS; k++)
        ua.rec_gain[k] = fu_gain(&ua.rec_fu, k + 1);
    for (int k = 0; k < USB_PLAY_CHANNELS; k++)
        ua.play_gain[k] = fu_gain(&ua.play_fu, k + 1);
}

void usb_audio_init(void)
{
    memset(&ua, 0, sizeof ua);
    for (int c = 0; c < 7; c++)
        ua.rec_fu.volume[c] = ua.play_fu.volume[c] = 100;
    ua.freq = 44100;
    update_gains();
}

void usb_audio_reset(void)
{
    usb_audio_set_interface(USB_IF_AUDIO_REC, 0);
    usb_audio_set_interface(USB_IF_AUDIO_PLAY, 0);
}

/* ---------------------------------------------------------------- recording (IN 0x81) */

static int rec_frame_bytes(void)
{
    return ua.rec_fmt == FMT_16 ? 2 * USB_REC_CHANNELS : ua.rec_fmt == FMT_32 ? 4 * USB_REC_CHANNELS : 8;
}

/* one microframe's packet (FUN_000369C0) */
static void rec_queue(void)
{
    uint8_t pkt[ISO_MAX];
    uint32_t head = ua.rec_head, fill = head - ua.rec_tail;
    if (fill > RING) {                      /* the reader fell behind: keep the newest */
        ua.rec_tail = head - REC_TARGET;
        fill = REC_TARGET;
    }
    ua.avg_fill += (int32_t)((fill << 16) - ua.avg_fill) >> 7;
    int32_t err = (int32_t)ua.avg_fill - (REC_TARGET << 16);
    ua.rate_acc += RATE_NOMINAL + (uint32_t)(err >> 10);
    uint32_t n = ua.rate_acc >> 16;
    ua.rate_acc &= 0xffff;
    if (n > fill) {
        n = fill;
        ua.st.rec_short++;
    }
    int bytes = 0;
    for (uint32_t i = 0; i < n; i++) {
        const int32_t *f = ua.rec[(ua.rec_tail + i) & (RING - 1)];
        if (ua.rec_fmt == FMT_16) {
            for (int c = 0; c < USB_REC_CHANNELS; c++) {
                int16_t v = (int16_t)(f[c] / 0xffff);
                memcpy(pkt + bytes, &v, 2);
                bytes += 2;
            }
        } else {
            int nc = ua.rec_fmt == FMT_32 ? USB_REC_CHANNELS : 2;
            memcpy(pkt + bytes, f, 4u * (unsigned)nc);
            bytes += 4 * nc;
        }
    }
    ua.rec_tail += n;
    usb_dcd_xfer(USB_EP_AUDIO_REC, pkt, (uint32_t)bytes);
    ua.st.rec_packets++;
}

static inline int32_t to_int(float v)
{
    if (v >= 2147483520.0f)
        return 2147483647;
    if (v <= -2147483520.0f)
        return -2147483647 - 1;
    return (int32_t)v;
}

void usb_audio_record(const float *frames, int n)
{
    if (!ua.rec_alt)
        return;
    uint32_t head = ua.rec_head;
    for (int i = 0; i < n; i++) {
        int32_t *f = ua.rec[(head + (uint32_t)i) & (RING - 1)];
        for (int c = 0; c < USB_REC_CHANNELS; c++)
            f[c] = to_int(frames[i * USB_REC_CHANNELS + c] * ua.rec_gain[c]);
    }
    ua.rec_head = head + (uint32_t)n;
}

/* ---------------------------------------------------------------- playback (OUT 0x01) */

static void play_received(const uint8_t *data, uint32_t len)
{
    int fb = ua.play_fmt == FMT_16 ? 2 * USB_PLAY_CHANNELS : ua.play_fmt == FMT_32 ? 4 * USB_PLAY_CHANNELS : 8;
    uint32_t n = len / (uint32_t)fb, head = ua.play_head;
    for (uint32_t i = 0; i < n; i++) {
        if (head - ua.play_tail >= RING) {
            ua.st.play_over++;
            break;
        }
        int32_t *f = ua.play[head & (RING - 1)];
        const uint8_t *p = data + i * (uint32_t)fb;
        if (ua.play_fmt == FMT_16) {
            for (int c = 0; c < USB_PLAY_CHANNELS; c++) {
                uint16_t v;
                memcpy(&v, p + 2 * c, 2);
                f[c] = (int32_t)((uint32_t)v << 16);
            }
        } else if (ua.play_fmt == FMT_32) {
            memcpy(f, p, 16);
        } else {
            memcpy(f, p, 8);
            f[2] = f[3] = 0;
        }
        head++;
    }
    ua.play_head = head;
    ua.st.play_packets++;
}

int usb_audio_play(float *frames, int n)
{
    if (!ua.play_alt) {
        memset(frames, 0, sizeof(float) * USB_PLAY_CHANNELS * (unsigned)n);
        return 0;
    }
    uint32_t tail = ua.play_tail, fill = ua.play_head - tail;
    if (!ua.play_started) {
        if (fill < PLAY_START) {
            memset(frames, 0, sizeof(float) * USB_PLAY_CHANNELS * (unsigned)n);
            return 1;
        }
        ua.play_started = 1;
    }
    for (int i = 0; i < n; i++) {
        if ((uint32_t)i < fill)             /* short of frames: the last one again (FUN_00036284) */
            memcpy(ua.play_last, ua.play[(tail + (uint32_t)i) & (RING - 1)], sizeof ua.play_last);
        for (int c = 0; c < USB_PLAY_CHANNELS; c++)
            frames[i * USB_PLAY_CHANNELS + c] = (float)ua.play_last[c] * ua.play_gain[c];
    }
    if ((uint32_t)n > fill) {
        ua.st.play_under++;
        ua.play_started = 0;
        ua.play_tail = tail + fill;
    } else {
        ua.play_tail = tail + (uint32_t)n;
    }
    return 1;
}

/* ---------------------------------------------------------------- interfaces, transfers */

void usb_audio_set_interface(int iface, int alt)
{
    if (iface == USB_IF_AUDIO_REC) {
        if (ua.rec_alt)
            usb_dcd_ep_close(USB_EP_AUDIO_REC);
        ua.rec_alt = 0;
        ua.rec_fmt = (uint8_t)format_of(alt);
        if (!alt)
            return;
        ua.rec_tail = ua.rec_head - REC_TARGET;     /* start at the target fill (silence before the start) */
        memset(ua.rec, 0, sizeof ua.rec);
        ua.avg_fill = REC_TARGET << 16;
        ua.rate_acc = 0;
        if (usb_dcd_ep_open(USB_EP_AUDIO_REC, USB_EP_ISO, ISO_MAX, ISO_DEPTH) != 0)
            return;
        ua.rec_alt = (uint8_t)alt;
        for (int i = 0; i < ISO_DEPTH; i++)
            rec_queue();
    } else if (iface == USB_IF_AUDIO_PLAY) {
        if (ua.play_alt)
            usb_dcd_ep_close(USB_EP_AUDIO_PLAY);
        ua.play_alt = 0;
        ua.play_fmt = (uint8_t)format_of(alt);
        ua.play_started = 0;
        ua.play_tail = ua.play_head;
        memset(ua.play_last, 0, sizeof ua.play_last);
        if (!alt)
            return;
        if (usb_dcd_ep_open(USB_EP_AUDIO_PLAY, USB_EP_ISO, ISO_MAX, ISO_DEPTH) != 0)
            return;
        ua.play_alt = (uint8_t)alt;
        for (int i = 0; i < ISO_DEPTH; i++)
            usb_dcd_xfer(USB_EP_AUDIO_PLAY, 0, ISO_MAX);
    }
}

void usb_audio_xfer_done(uint8_t ep, const uint8_t *data, uint32_t len, int ok)
{
    if (ep == USB_EP_AUDIO_REC && ua.rec_alt) {
        rec_queue();
    } else if (ep == USB_EP_AUDIO_PLAY && ua.play_alt) {
        if (ok)
            play_received(data, len);
        usb_dcd_xfer(USB_EP_AUDIO_PLAY, 0, ISO_MAX);
    }
}

/* ---------------------------------------------------------------- class requests (FUN_0003880C) */

static int put(uint8_t *data, int len, const void *src, int n)
{
    if (n > len)
        n = len;
    memcpy(data, src, (unsigned)n);
    return n;
}

int usb_audio_request(const struct usb_setup *s, uint8_t *data, int len)
{
    int in = s->type & 0x80, entity = s->index >> 8, cs = s->value >> 8, ch = s->value & 0xff;
    if (entity == USB_ENT_REC_FEATURE || entity == USB_ENT_PLAY_FEATURE) {
        struct feature *f = entity == USB_ENT_REC_FEATURE ? &ua.rec_fu : &ua.play_fu;
        int channels = entity == USB_ENT_REC_FEATURE ? 7 : 5;
        if (s->request == REQ_RANGE && in) {
            static const uint8_t range[8] = { 1, 0, 1, 0, 100, 0, 1, 0 };     /* 1..100, step 1 */
            return put(data, len, range, sizeof range);
        }
        if (s->request != REQ_CUR || ch >= channels || (cs != CS_MUTE && cs != CS_VOLUME))
            return -1;
        if (in) {
            uint8_t v[2] = { f->mute[ch], 0 };
            if (cs == CS_VOLUME) {
                v[0] = (uint8_t)f->volume[ch];
                v[1] = (uint8_t)(f->volume[ch] >> 8);
            }
            return put(data, len, v, cs == CS_MUTE ? 1 : 2);
        }
        if (cs == CS_MUTE && len >= 1)
            f->mute[ch] = data[0];
        else if (cs == CS_VOLUME && len >= 2)
            f->volume[ch] = (uint16_t)(data[0] | data[1] << 8);
        else
            return -1;
        update_gains();
        return 0;
    }
    if (entity == USB_ENT_CLOCK) {
        if (in && s->request == REQ_CUR && cs == CS_FREQ) {
            uint8_t v[4] = { (uint8_t)ua.freq, (uint8_t)(ua.freq >> 8), (uint8_t)(ua.freq >> 16),
                             (uint8_t)(ua.freq >> 24) };
            return put(data, len, v, 4);
        }
        if (in && s->request == REQ_CUR && cs == CS_VALID) {
            static const uint8_t valid[2] = { 1, 0 };
            return put(data, len, valid, 1);
        }
        if (in && s->request == REQ_RANGE) {
            static const uint8_t range[14] = { 1, 0, 0x44, 0xac, 0, 0, 0x44, 0xac, 0, 0, 0, 0, 0, 0 };
            return put(data, len, range, sizeof range);
        }
        if (!in && s->request == REQ_CUR && cs == CS_FREQ && len >= 4) {   /* kept, the rate stays 44.1 kHz */
            ua.freq = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
                      (uint32_t)data[3] << 24;
            return 0;
        }
    }
    return -1;
}

void usb_audio_stats(struct usb_audio_stats *out)
{
    ua.st.rec_alt = ua.rec_alt;
    ua.st.play_alt = ua.play_alt;
    ua.st.rec_fill = (uint16_t)(ua.rec_head - ua.rec_tail);
    ua.st.play_fill = (uint16_t)(ua.play_head - ua.play_tail);
    *out = ua.st;
}
