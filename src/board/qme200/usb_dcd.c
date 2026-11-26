/*
 * USB device controller of the RT1062: USB OTG1, a ChipIdea core in device mode (queue heads and transfer
 * descriptors, EHCI style), brought up like the original app does it (NXP USB stack):
 *   FUN_0001D760  PLL_USB1 at 480 MHz, USBPHY1 out of reset and powered;
 *   FUN_0001D710  controller reset, 3.0 V regulator (PMU_REG_3P0) on;
 *   FUN_00038000  device mode, setup lockout off, queue head list, interrupts UE | UEE | PCE | URE;
 *   FUN_00037ECE  the interrupt: port change, reset, transfer completion (FUN_00037AAC).
 *
 * Queue heads, transfer descriptors and the transfers' bounce buffers live in .usb_dma, the last 32 KB of the
 * OCRAM, which the MPU keeps non-cacheable (system.c), so no cache maintenance is needed. Each endpoint gets
 * a ring of up to MAX_DEPTH descriptors with one bounce buffer each; transfers complete in queue order.
 * Non-cacheable stores to the OCRAM and stores to the controller's registers take different buses: a DSB
 * comes before every prime (the NXP stack has one at 0x382DE), otherwise the controller can read a descriptor
 * before it is written.
 *
 * The interrupt runs above the audio one (audio.c), so isochronous endpoints only need a millisecond of
 * transfers queued although an audio block takes about that long to process.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/usb.h"
#include <string.h>

#define USB1                0x402e0000u
#define USB_USBCMD          (USB1 + 0x140)
#define USB_USBSTS          (USB1 + 0x144)
#define USB_USBINTR         (USB1 + 0x148)
#define USB_FRINDEX         (USB1 + 0x14c)
#define USB_DEVICEADDR      (USB1 + 0x154)
#define USB_ENDPTLISTADDR   (USB1 + 0x158)
#define USB_PORTSC1         (USB1 + 0x184)
#define USB_USBMODE         (USB1 + 0x1a8)
#define USB_ENDPTSETUPSTAT  (USB1 + 0x1ac)
#define USB_ENDPTPRIME      (USB1 + 0x1b0)
#define USB_ENDPTFLUSH      (USB1 + 0x1b4)
#define USB_ENDPTSTAT       (USB1 + 0x1b8)
#define USB_ENDPTCOMPLETE   (USB1 + 0x1bc)
#define USB_ENDPTCTRL(n)    (USB1 + 0x1c0 + 4u * (n))

#define USBCMD_RS           (1u << 0)
#define USBCMD_RST          (1u << 1)
#define USBCMD_SUTW         (1u << 13)
#define USBCMD_ATDTW        (1u << 14)
#define USBCMD_ITC(n)       ((uint32_t)(n) << 16)
#define USBSTS_UI           (1u << 0)
#define USBSTS_UEI          (1u << 1)
#define USBSTS_PCI          (1u << 2)
#define USBSTS_URI          (1u << 6)
#define USBSTS_SLI          (1u << 8)
#define USBMODE_DEVICE      2u
#define USBMODE_SLOM        (1u << 3)
#define PORTSC_SUSP         (1u << 7)
#define PORTSC_PSPD(v)      (((v) >> 26) & 3u)
#define EPCTRL_RXS          (1u << 0)
#define EPCTRL_RXT(t)       ((uint32_t)(t) << 2)
#define EPCTRL_RXR          (1u << 6)
#define EPCTRL_RXE          (1u << 7)

#define USBPHY1             0x400d9000u
#define USBPHY_PWD          (USBPHY1 + 0x00)
#define USBPHY_CTRL         (USBPHY1 + 0x30)
#define PLL_USB1            (ANATOP + 0x10)
#define PMU_REG_3P0         (ANATOP + 0x120)
#define USB_OTG1_IRQ        113

#define QH_IOS              (1u << 15)
#define QH_MPL(n)           ((uint32_t)(n) << 16)
#define QH_ZLT_OFF          (1u << 29)
#define QH_MULT(n)          ((uint32_t)(n) << 30)
#define TD_TERMINATE        1u
#define TD_ACTIVE           0x80u
#define TD_HALTED           0x40u
#define TD_BUFERR           0x20u
#define TD_XACTERR          0x08u
#define TD_IOC              (1u << 15)

#define ENDPOINTS           4               /* 0..3: control, audio (iso), unused, MIDI (bulk) */
#define MAX_DEPTH           8
#define POOL_BYTES          (24 * 1024)

struct qh {
    volatile uint32_t caps;
    volatile uint32_t current;
    volatile uint32_t next;
    volatile uint32_t token;
    volatile uint32_t buf[5];
    uint32_t reserved;
    volatile uint32_t setup[2];
    uint32_t pad[4];
};

struct td {
    volatile uint32_t next;
    volatile uint32_t token;
    volatile uint32_t buf[5];
    uint32_t len;                           /* the hardware's reserved word: the length asked for */
};

struct ep {
    uint8_t open, depth, head, count;
    uint16_t bufsize;                       /* per descriptor */
    uint8_t *buf;                           /* depth x bufsize, from the pool (kept across reopenings) */
    uint16_t alloc;                         /* bytes the pool gave this endpoint */
};

__attribute__((section(".usb_dma"), aligned(2048))) static struct qh qh[2 * ENDPOINTS];
__attribute__((section(".usb_dma"), aligned(32))) static struct td td[2 * ENDPOINTS][MAX_DEPTH];
__attribute__((section(".usb_dma"), aligned(32))) static uint8_t pool[POOL_BYTES];

static const struct usb_dcd_events *events;
static struct ep eps[2 * ENDPOINTS];
static uint32_t pool_used;
static uint8_t high_speed;
static struct usb_dcd_counts counts;

static int ep_index(uint8_t ep) { return (ep & 0x0f) * 2 + (ep >> 7); }
static uint32_t ep_bit(uint8_t ep) { return (ep & 0x80 ? 1u << 16 : 1u) << (ep & 0x0f); }

static void delay_cycles(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) {}
}

/* bounded: a controller that stopped answering must not hang the pedal */
static void flush(uint32_t bits)
{
    for (int tries = 0; tries < 100; tries++) {
        R32(USB_ENDPTFLUSH) = bits;
        for (uint32_t t = 0; (R32(USB_ENDPTFLUSH) & bits) && t < 100000u; t++) {}
        if (!(R32(USB_ENDPTSTAT) & bits))
            return;
    }
}

static void ep_reset_queue(int i)
{
    eps[i].head = eps[i].count = 0;
    qh[i].next = TD_TERMINATE;
    qh[i].token = 0;
}

void usb_dcd_init(const struct usb_dcd_events *ev)
{
    events = ev;
    /* FUN_0001D760: PLL_USB1 (480 MHz, USB clocks on), USBPHY1 out of soft reset and clock gating */
    if (!(R32(PLL_USB1) & (1u << 13))) {
        R32(PLL_USB1) = (R32(PLL_USB1) & ~0xc000u) | 0x10000u;         /* bypass, from the 24 MHz crystal */
        R32(PLL_USB1) = (R32(PLL_USB1) & ~2u) | 0x3040u;               /* power, enable, USB clocks */
        for (uint32_t t = 0; !(R32(PLL_USB1) & 0x80000000u) && t < 10000000u; t++) {}
        R32(PLL_USB1) &= ~0x10000u;
    } else {
        R32(PLL_USB1) |= 0x40u;
    }
    R32(USBPHY_CTRL) &= ~0x80000000u;       /* SFTRST */
    R32(USBPHY_CTRL) &= ~0x40000000u;       /* CLKGATE */
    R32(USBPHY_PWD) = 0;
    R32(USBPHY_CTRL) |= 0x18c000u;          /* UTMI levels 2 / 3, automatic clock gate / power down clear */

    /* FUN_0001D710: the bootloader may have left the controller running its own update device */
    R32(USB_USBCMD) |= USBCMD_RST;
    for (uint32_t t = 0; (R32(USB_USBCMD) & USBCMD_RST) && t < 10000000u; t++) {}
    delay_cycles(1000);
    R32(PMU_REG_3P0) = (R32(PMU_REG_3P0) & ~0x1f00u) | 0x1701u;

    /* FUN_00038000 */
    R32(USB_USBMODE) = USBMODE_DEVICE | USBMODE_SLOM;
    memset((void *)qh, 0, sizeof qh);
    memset(eps, 0, sizeof eps);
    for (int i = 0; i < 2 * ENDPOINTS; i++)
        ep_reset_queue(i);
    R32(USB_ENDPTLISTADDR) = (uint32_t)(uintptr_t)qh;
    R32(USB_USBCMD) = (R32(USB_USBCMD) & ~USBCMD_ITC(0xff)) | USBCMD_ITC(1);
    R32(USB_USBSTS) = R32(USB_USBSTS);
    R32(USB_USBINTR) = USBSTS_UI | USBSTS_UEI | USBSTS_PCI | USBSTS_URI | USBSTS_SLI;

    R8(NVIC_IPR + USB_OTG1_IRQ) = 0x00;     /* above the audio interrupt */
    R32(NVIC_ICPR + 4 * (USB_OTG1_IRQ / 32)) = 1u << (USB_OTG1_IRQ % 32);
    R32(NVIC_ISER + 4 * (USB_OTG1_IRQ / 32)) = 1u << (USB_OTG1_IRQ % 32);
}

void usb_dcd_connect(int on)
{
    if (on)
        R32(USB_USBCMD) |= USBCMD_RS;
    else
        R32(USB_USBCMD) &= ~USBCMD_RS;
}

int usb_dcd_high_speed(void)
{
    return high_speed;
}

void usb_dcd_set_address(uint8_t addr)
{
    R32(USB_DEVICEADDR) = (uint32_t)addr << 25 | 1u << 24;     /* USBADRA: applied after the status stage */
}

uint32_t usb_dcd_frame(void)
{
    return R32(USB_FRINDEX) & 0x3fff;
}

static int lock_depth;

void usb_dcd_lock(void)
{
    R32(NVIC_ICER + 4 * (USB_OTG1_IRQ / 32)) = 1u << (USB_OTG1_IRQ % 32);
    __asm volatile("dsb\n isb" ::: "memory");
    lock_depth++;
}

void usb_dcd_unlock(void)
{
    if (lock_depth && --lock_depth == 0)
        R32(NVIC_ISER + 4 * (USB_OTG1_IRQ / 32)) = 1u << (USB_OTG1_IRQ % 32);
}

void usb_dcd_counts(struct usb_dcd_counts *out)
{
    *out = counts;
    out->high_speed = high_speed;
}

void usb_dcd_wake(void)
{
    R32(NVIC_ISPR + 4 * (USB_OTG1_IRQ / 32)) = 1u << (USB_OTG1_IRQ % 32);
}

int usb_dcd_ep_open(uint8_t ep, enum usb_ep_type type, uint16_t max_packet, int depth)
{
    int num = ep & 0x0f, i = ep_index(ep);
    if (num >= ENDPOINTS || depth < 1)
        return -1;
    if (depth > MAX_DEPTH)
        depth = MAX_DEPTH;
    struct ep *e = &eps[i];
    uint32_t bufsize = type == USB_EP_CONTROL || type == USB_EP_BULK ? USB_XFER_MAX : max_packet;
    bufsize = (bufsize + 31u) & ~31u;
    if (bufsize * (uint32_t)depth > e->alloc) {      /* first opening (or a bigger one): carve from the pool */
        if (pool_used + bufsize * (uint32_t)depth > POOL_BYTES)
            return -1;
        e->buf = pool + pool_used;
        e->alloc = (uint16_t)(bufsize * (uint32_t)depth);
        pool_used += e->alloc;
    }
    e->bufsize = (uint16_t)bufsize;
    e->depth = (uint8_t)depth;
    ep_reset_queue(i);
    uint32_t caps = QH_MPL(max_packet) | QH_ZLT_OFF;
    if (type == USB_EP_CONTROL && !(ep & 0x80))
        caps |= QH_IOS;
    if (type == USB_EP_ISO)
        caps |= QH_MULT(1);
    qh[i].caps = caps;
    dsb();
    e->open = 1;
    if (num) {
        uint32_t sh = ep & 0x80 ? 16 : 0, ctrl = R32(USB_ENDPTCTRL(num));
        ctrl &= ~(0xffffu << sh);
        ctrl |= (EPCTRL_RXT(type) | EPCTRL_RXR | EPCTRL_RXE) << sh;
        /* an unused direction must not stay of the control type (reference manual, ENDPTCTRLn) */
        uint32_t other = sh ? 0 : 16;
        if (!(ctrl & (EPCTRL_RXE << other)))
            ctrl = (ctrl & ~(EPCTRL_RXT(3) << other)) | EPCTRL_RXT(USB_EP_BULK) << other;
        R32(USB_ENDPTCTRL(num)) = ctrl;
    }
    return 0;
}

void usb_dcd_ep_close(uint8_t ep)
{
    int num = ep & 0x0f, i = ep_index(ep);
    if (num >= ENDPOINTS || !eps[i].open)
        return;
    flush(ep_bit(ep));
    if (num) {
        uint32_t sh = ep & 0x80 ? 16 : 0;
        R32(USB_ENDPTCTRL(num)) &= ~(EPCTRL_RXE << sh);
    }
    eps[i].open = 0;
    ep_reset_queue(i);
}

int usb_dcd_pending(uint8_t ep)
{
    return (ep & 0x0f) < ENDPOINTS ? eps[ep_index(ep)].count : 0;
}

int usb_dcd_xfer(uint8_t ep, const void *data, uint32_t len)
{
    if ((ep & 0x0f) >= ENDPOINTS)
        return -1;
    int i = ep_index(ep);
    struct ep *e = &eps[i];
    if (!e->open || e->count >= e->depth || len > e->bufsize)
        return -1;
    int slot = (e->head + e->count) % e->depth;
    struct td *t = &td[i][slot];
    uint8_t *b = e->buf + slot * e->bufsize;
    if ((ep & 0x80) && len)
        memcpy(b, data, len);
    uint32_t a = (uint32_t)(uintptr_t)b;
    t->next = TD_TERMINATE;
    t->buf[0] = a;
    for (int k = 1; k < 5; k++)
        t->buf[k] = (a & ~0xfffu) + 0x1000u * (uint32_t)k;
    t->len = len;
    t->token = len << 16 | TD_IOC | TD_ACTIVE;
    e->count++;

    uint32_t bit = ep_bit(ep);
    if (e->count > 1) {                     /* append to a running list (EHCI "add dTD tripwire") */
        td[i][(slot + e->depth - 1) % e->depth].next = (uint32_t)(uintptr_t)t;
        dsb();
        if (R32(USB_ENDPTPRIME) & bit)
            return 0;
        uint32_t stat, tries = 0;
        do {
            R32(USB_USBCMD) |= USBCMD_ATDTW;
            stat = R32(USB_ENDPTSTAT) & bit;
        } while (!(R32(USB_USBCMD) & USBCMD_ATDTW) && ++tries < 100000u);
        R32(USB_USBCMD) &= ~USBCMD_ATDTW;
        if (stat)
            return 0;
        /* the controller ran off the end: restart it at the first descriptor it has not finished */
        for (int k = 0; k < e->count; k++) {
            struct td *f = &td[i][(e->head + k) % e->depth];
            if (f->token & TD_ACTIVE) {
                t = f;
                break;
            }
        }
    }
    qh[i].next = (uint32_t)(uintptr_t)t;
    qh[i].token &= ~(TD_ACTIVE | TD_HALTED);
    dsb();
    R32(USB_ENDPTPRIME) = bit;
    return 0;
}

void usb_dcd_stall(uint8_t ep, int on)
{
    int num = ep & 0x0f;
    if (num >= ENDPOINTS)
        return;
    if (num == 0) {                         /* both directions; the next SETUP clears it */
        if (on)
            R32(USB_ENDPTCTRL(0)) |= EPCTRL_RXS | EPCTRL_RXS << 16;
        return;
    }
    uint32_t sh = ep & 0x80 ? 16 : 0;
    if (on) {
        R32(USB_ENDPTCTRL(num)) |= EPCTRL_RXS << sh;
    } else {
        R32(USB_ENDPTCTRL(num)) &= ~(EPCTRL_RXS << sh);
        R32(USB_ENDPTCTRL(num)) |= EPCTRL_RXR << sh;     /* CLEAR_FEATURE(HALT) resets the data toggle */
    }
}

/* completed descriptors of one endpoint, oldest first */
static void retire(uint8_t ep)
{
    int i = ep_index(ep);
    struct ep *e = &eps[i];
    while (e->count) {
        int slot = e->head;
        struct td *t = &td[i][slot];
        uint32_t tok = t->token;
        if (tok & TD_ACTIVE)
            break;
        uint32_t left = (tok >> 16) & 0x7fffu, n = t->len > left ? t->len - left : 0;
        int ok = !(tok & (TD_HALTED | TD_BUFERR | TD_XACTERR));
        const uint8_t *data = e->buf + slot * e->bufsize;
        e->head = (uint8_t)((slot + 1) % e->depth);
        e->count--;
        if (events && events->done)
            events->done(ep, data, n, ok);
    }
}

static void setup_received(void)
{
    uint32_t s[2], tries = 0;
    R32(USB_ENDPTSETUPSTAT) = 1;
    do {
        R32(USB_USBCMD) |= USBCMD_SUTW;
        s[0] = qh[0].setup[0];
        s[1] = qh[0].setup[1];
    } while (!(R32(USB_USBCMD) & USBCMD_SUTW) && ++tries < 100000u);
    R32(USB_USBCMD) &= ~USBCMD_SUTW;
    counts.setups++;
    counts.last_setup = s[0];
    flush(0x10001u);                        /* a new SETUP ends whatever control transfer was going on */
    ep_reset_queue(0);
    ep_reset_queue(1);
    if (events && events->setup)
        events->setup((const uint8_t *)s);
}

static void bus_reset(void)
{
    counts.resets++;
    R32(USB_ENDPTSETUPSTAT) = R32(USB_ENDPTSETUPSTAT);
    R32(USB_ENDPTCOMPLETE) = R32(USB_ENDPTCOMPLETE);
    for (uint32_t t = 0; R32(USB_ENDPTPRIME) && t < 1000000u; t++) {}
    R32(USB_ENDPTFLUSH) = 0xffffffffu;
    for (uint32_t t = 0; R32(USB_ENDPTFLUSH) && t < 1000000u; t++) {}
    R32(USB_DEVICEADDR) = 0;
    for (int num = 1; num < ENDPOINTS; num++)
        R32(USB_ENDPTCTRL(num)) = 0;
    for (int i = 2; i < 2 * ENDPOINTS; i++) {
        eps[i].open = 0;
        ep_reset_queue(i);
    }
    ep_reset_queue(0);
    ep_reset_queue(1);
    if (events && events->bus_reset)
        events->bus_reset();
}

void usb_otg1_irq(void)
{
    uint32_t sts = R32(USB_USBSTS) & R32(USB_USBINTR);
    R32(USB_USBSTS) = sts;
    if (sts & USBSTS_UEI)
        counts.errors++;
    if (sts & USBSTS_URI)
        bus_reset();
    if (sts & USBSTS_PCI) {
        uint32_t port = R32(USB_PORTSC1);
        high_speed = PORTSC_PSPD(port) == 2;
        if (!(port & PORTSC_SUSP) && events && events->suspend)
            events->suspend(0);
    }
    if (sts & USBSTS_SLI && events && events->suspend)
        events->suspend(1);
    if (sts & (USBSTS_UI | USBSTS_UEI)) {
        if (R32(USB_ENDPTSETUPSTAT) & 1u)
            setup_received();
        uint32_t done = R32(USB_ENDPTCOMPLETE);
        R32(USB_ENDPTCOMPLETE) = done;
        dsb();                              /* then the descriptors the controller wrote */
        counts.completions += (uint32_t)__builtin_popcount(done);
        for (int num = 0; num < ENDPOINTS; num++) {
            if (done & (1u << num))
                retire((uint8_t)num);
            if (done & (1u << (num + 16)))
                retire((uint8_t)(num | 0x80));
        }
    }
    if (events && events->service)
        events->service();
}
