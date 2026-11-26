/*
 * Crash protection on the QME-200 (platform/safety.h).
 *
 * Every fault exception and every interrupt without a handler enters fault_entry, which leaves the stack it
 * came from (it may be what failed) and runs crash(): the audio output buffers are zeroed (the DMA keeps
 * streaming silence until the reset), a 4-word record goes to the SNVS low-power general purpose registers
 * (SNVS_LPGPR0-3, kept by a warm reset, lost at power-off), and the chip restarts through AIRCR.SYSRESETREQ like
 * the original's own reset (FUN_00030020) but without the bootloader's update request, so the application
 * starts again.
 *
 * Record (SNVS_LPGPR0-3):
 *   0  0xC5A5 << 16 | cause << 12 | count << 8 | preset
 *   1  PC of the code that was running
 *   2  CFSR (faults), the exception number (CRASH_IRQ)
 *   3  uptime in s << 16 | slot << 12 | in audio << 11 | check (8-bit sum of the other 15 bytes ^ 0xA5)
 *
 * Watchdogs: the software one runs in the SysTick interrupt (time.c, highest priority), so it sees a main loop
 * stopped by the audio interrupt or by itself; the RTWDOG is the backstop. Its clock ("LPO", 32 kHz in the
 * reference manual) is measured against the cycle counter before it is armed, so a different clock can only
 * make the timeout longer or turn the watchdog off, never reset the pedal in a loop. UPDATE stays set, so the
 * bootloader's own start-up can switch it off.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/safety.h"
#include "platform/time.h"

#define SNVS_LPGPR(n)   (0x400d4100u + 4u * (n))
#define SRC_SRSR        0x400f8008u
#define SRSR_WDOG3      (1u << 7)              /* reset by the RTWDOG */

#define SCB_CFSR        0xe000ed28u
#define RECORD_MAGIC    0xc5a5u
#define EARLY_S         60u                     /* a crash this soon after the start-up counts as "in a row" */

#define WD_CS           (RTWDOG + 0x0)
#define WD_CNT          (RTWDOG + 0x4)
#define WD_TOVAL        (RTWDOG + 0x8)
#define WD_UNLOCK       0xd928c520u
#define WD_REFRESH      0xb480a602u
#define CS_UPDATE       (1u << 5)
#define CS_EN           (1u << 7)
#define CS_CLK_LPO      (1u << 8)
#define CS_RCS          (1u << 10)
#define CS_ULK          (1u << 11)
#define CS_PRES         (1u << 12)              /* clock / 256 */
#define CS_CMD32EN      (1u << 13)

volatile uint8_t safety_slot_main = SAFETY_NONE, safety_slot_audio = SAFETY_NONE;

static struct crash_info last;                  /* what the previous run left */
static int last_valid;
static volatile uint8_t preset_now = SAFETY_NONE;
static volatile uint32_t fed_at;
static volatile uint8_t armed;
static uint32_t wd_hz;                          /* RTWDOG clock measured, 0 = not armed */

/* a fresh stack for crash(): the one the fault came from may be broken (8-byte aligned; named in fault_entry) */
__attribute__((used)) uint64_t safety_fault_stack[128];

/* ---------------------------------------------------------------- the record */

static uint8_t check_of(const uint32_t w[4])
{
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++)
            if (i != 3 || b != 0)
                sum += (w[i] >> (8 * b)) & 0xffu;
    return (uint8_t)(sum ^ 0xa5u);
}

static void record_write(const struct crash_info *c)
{
    uint32_t w[4];
    uint32_t up = c->uptime_s > 0xffffu ? 0xffffu : c->uptime_s;
    w[0] = RECORD_MAGIC << 16 | (uint32_t)(c->cause & 0xf) << 12 | (uint32_t)(c->count > 15 ? 15 : c->count) << 8 |
           c->preset;
    w[1] = c->pc;
    w[2] = c->detail;
    w[3] = up << 16 | (uint32_t)(c->slot < 12 ? c->slot : 0xf) << 12 | (uint32_t)(c->in_audio ? 1 : 0) << 11;
    w[3] |= check_of(w);
    for (int i = 0; i < 4; i++)
        R32(SNVS_LPGPR(i)) = w[i];
}

static int record_read(struct crash_info *c)
{
    uint32_t w[4];
    for (int i = 0; i < 4; i++)
        w[i] = R32(SNVS_LPGPR(i));
    if (w[0] >> 16 != RECORD_MAGIC || (w[3] & 0xffu) != check_of(w))
        return 0;
    c->cause = (uint8_t)((w[0] >> 12) & 0xf);
    c->count = (uint8_t)((w[0] >> 8) & 0xf);
    c->preset = (uint8_t)w[0];
    c->pc = w[1];
    c->detail = w[2];
    c->uptime_s = w[3] >> 16;
    c->slot = (uint8_t)((w[3] >> 12) & 0xf);
    if (c->slot >= 12)
        c->slot = SAFETY_NONE;
    c->in_audio = (uint8_t)((w[3] >> 11) & 1);
    return c->cause != CRASH_NONE;
}

/* start-up, before anything can crash: take the record over and clear it */
void safety_boot(void)
{
    last_valid = record_read(&last);
    for (int i = 0; i < 4; i++)
        R32(SNVS_LPGPR(i)) = 0;
    uint32_t srsr = R32(SRC_SRSR);
    if (!last_valid && (srsr & SRSR_WDOG3)) {
        last = (struct crash_info){ .cause = CRASH_HW_WATCHDOG, .count = 1, .preset = SAFETY_NONE,
                                    .slot = SAFETY_NONE };
        last_valid = 1;
    }
    R32(SRC_SRSR) = srsr;                       /* write 1 to clear: the next start-up sees only its own cause */
}

int safety_last_crash(struct crash_info *out)
{
    *out = last;
    return last_valid;
}

void safety_note_preset(int index)
{
    preset_now = index >= 0 && index < 240 ? (uint8_t)index : SAFETY_NONE;
}

/* ---------------------------------------------------------------- the crash */

static int in_ram(const uint32_t *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;
    if (a & 3u)
        return 0;
    return (a >= 0x20000000u && a <= 0x20008000u - 32u) || (a >= 0x20200000u && a <= 0x20280000u - 32u);
}

__attribute__((noreturn)) static void crash(uint8_t cause, const uint32_t *frame, uint32_t detail)
{
    __asm volatile("cpsid i" ::: "memory");
    audio_silence();
    struct crash_info c = { .cause = cause, .preset = preset_now, .detail = detail };
    if (in_ram(frame)) {
        c.pc = frame[6];
        c.in_audio = (frame[7] & 0x1ffu) == 16u;   /* IPSR of the interrupted code: IRQ 0 = the audio DMA */
    }
    c.slot = c.in_audio ? safety_slot_audio : safety_slot_main;
    c.uptime_s = time_ms() / 1000u;
    uint32_t before = last_valid && last.cause != CRASH_HW_WATCHDOG ? last.count : 0;
    c.count = (uint8_t)(c.uptime_s < EARLY_S ? before + 1 : 1);
    record_write(&c);
    dsb();
    R32(SCB_AIRCR) = 0x05fa0004u | (R32(SCB_AIRCR) & 0x700u);
    dsb();
    for (;;) {}
}

/* fault_entry continues here on fault_stack; ipsr = the exception taken */
__attribute__((used, noreturn)) void fault_c(const uint32_t *frame, uint32_t ipsr)
{
    uint32_t n = ipsr & 0x1ffu;
    uint8_t cause = n == 2 ? CRASH_NMI : n == 3 ? CRASH_HARDFAULT : n == 4 ? CRASH_MEMMANAGE :
                    n == 5 ? CRASH_BUSFAULT : n == 6 ? CRASH_USAGEFAULT : CRASH_IRQ;
    crash(cause, frame, cause == CRASH_IRQ ? n : R32(SCB_CFSR));
}

/* Every fault vector and every unused interrupt. Only the MSP is ever used (CONTROL = 0 at reset). */
__attribute__((naked, noreturn)) void fault_entry(void)
{
    __asm volatile(
        "cpsid i\n"
        "mrs r0, msp\n"
        "mrs r1, ipsr\n"
        "ldr r2, =safety_fault_stack + 1024\n"
        "mov sp, r2\n"
        "b fault_c\n"
        ".ltorg\n");
}

/* SysTick, every millisecond (time.c) */
void safety_tick(const uint32_t *frame, uint32_t now)
{
    if (armed && now - fed_at > SAFETY_FEED_MS)
        crash(CRASH_WATCHDOG, frame, 0);
}

/* ---------------------------------------------------------------- the watchdogs */

static void wd_configure(uint32_t cs, uint32_t toval)
{
    __asm volatile("cpsid i" ::: "memory");     /* the new values must follow the unlock at once */
    if (R32(WD_CS) & CS_CMD32EN) {
        R32(WD_CNT) = WD_UNLOCK;
    } else {                                    /* 16-bit commands, as the bootloader may have left it */
        R16(WD_CNT) = (uint16_t)WD_UNLOCK;
        R16(WD_CNT) = (uint16_t)(WD_UNLOCK >> 16);
    }
    for (int i = 0; i < 1000 && !(R32(WD_CS) & CS_ULK); i++) {}
    R32(WD_TOVAL) = toval;
    R32(WD_CS) = cs;
    for (int i = 0; i < 1000 && !(R32(WD_CS) & CS_RCS); i++) {}
    __asm volatile("cpsie i" ::: "memory");
}

/* RTWDOG counts over `ms` with or without the /256 prescaler, from a fresh count */
static uint32_t wd_count_over(uint32_t ms, int pres)
{
    wd_configure(CS_EN | CS_UPDATE | CS_CMD32EN | CS_CLK_LPO | (pres ? CS_PRES : 0), 0xffffu);
    R32(WD_CNT) = WD_REFRESH;
    uint32_t c0 = R32(WD_CNT) & 0xffffu;
    time_delay_ms(ms);
    return ((R32(WD_CNT) & 0xffffu) - c0) & 0xffffu;
}

static void wd_off(void)
{
    wd_configure(CS_UPDATE | CS_CMD32EN | CS_CLK_LPO, 0xffffu);
}

static void hw_arm(void)
{
    /* with /256 a 50 ms count is safe for any clock up to 335 MHz (the bus clock is 150 MHz) */
    uint32_t coarse = wd_count_over(50, 1);
    uint32_t hz;
    if (coarse > 1000) {                        /* above ~5 MHz: not the clock this code expects */
        wd_off();
        return;
    }
    if (coarse < 250)                           /* below ~1.3 MHz: 0xFFFF counts last over 50 ms, count finely */
        hz = wd_count_over(50, 0) * 20u;
    else
        hz = coarse * 256u * 20u;
    if (hz < 500u) {                            /* not counting */
        wd_off();
        return;
    }
    uint64_t ticks = (uint64_t)hz * SAFETY_HW_MS / 1000u;
    int pres = ticks > 0xffffu;
    if (pres)
        ticks = (ticks + 255u) / 256u;
    if (ticks > 0xffffu)
        ticks = 0xffffu;
    wd_configure(CS_EN | CS_UPDATE | CS_CMD32EN | CS_CLK_LPO | (pres ? CS_PRES : 0), (uint32_t)ticks);
    R32(WD_CNT) = WD_REFRESH;
    wd_hz = hz;
}

void safety_arm(void)
{
    hw_arm();
    fed_at = time_ms();
    armed = 1;
}

void safety_feed(void)
{
    fed_at = time_ms();
    if (wd_hz)
        R32(WD_CNT) = WD_REFRESH;
}

void safety_disarm(void)
{
    armed = 0;
    if (wd_hz) {
        wd_off();
        wd_hz = 0;
    }
}

uint32_t safety_watchdog_hz(void)
{
    return wd_hz;
}

/* ---------------------------------------------------------------- tests */

void safety_test(enum safety_test kind)
{
    switch (kind) {
    case SAFETY_TEST_FAULT:
        __builtin_trap();                       /* UDF: UsageFault (undefined instruction) */
        break;
    case SAFETY_TEST_HANG:
        for (;;)
            __asm volatile("" ::: "memory");
    case SAFETY_TEST_HANG_IRQ_OFF:
        __asm volatile("cpsid i" ::: "memory");
        for (;;)
            __asm volatile("" ::: "memory");
    }
}
