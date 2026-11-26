/*
 * Core and memory system set-up.
 *
 * The MPU map is the original application's (FUN_0001BEF8, read from the start-up trace), so effect modules
 * see memory exactly as on the stock firmware:
 *   R0 4 GB          background: device memory, execute never
 *   R1 0x80000000    1 GB device (SEMC space, SDRAM refined by R7)
 *   R2 0x60000000    flash XIP, normal non-cacheable
 *   R3 0x00000000    512 MB normal non-cacheable, executable (ITCM, writable: modules keep data there)
 *   R4 0x20000000    DTCM
 *   R5 0x20200000    OCRAM 512 KB write-back
 *   R6 0x20200000    first 16 KB of OCRAM non-cacheable (audio DMA buffers)
 *   R7 0x80000000    SDRAM 32 MB write-back
 *   R8 0x81F00000    1 MB write-through (frame buffers, read by the LCD controller)
 * and one region of waveod's own:
 *   R9 0x20278000    last 32 KB of OCRAM non-cacheable (USB queue heads, descriptors, buffers: usb_dcd.c)
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/cpu.h"

static const uint32_t mpu_map[][2] = {
    { 0x00000010u, 0x1310003fu },
    { 0x80000011u, 0x0310003bu },
    { 0x60000012u, 0x03080039u },
    { 0x00000013u, 0x03080039u },
    { 0x20000014u, 0x03080025u },
    { 0x20200015u, 0x030b0025u },
    { 0x20200016u, 0x0308001bu },
    { 0x80000017u, 0x030b0031u },
    { 0x81f00018u, 0x03020027u },
    { 0x20278019u, 0x0308001du },
};

void system_early_init(void)
{
    R32(SCB_VTOR) = 0;
    R32(SCB_CPACR) |= 0xfu << 20;               /* FPU (CP10/CP11) full access */
    dsb();
    isb();

    R16(WDOG1 + 8) &= ~1u;                      /* WMCR.PDE */
    R16(WDOG2 + 8) &= ~1u;
    if (R16(WDOG1) & 4u) R16(WDOG1) &= ~4u;     /* WCR.WDE */
    if (R16(WDOG2) & 4u) R16(WDOG2) &= ~4u;
    if (R32(RTWDOG) & 0x2000u) {                /* CS.CMD32EN */
        R32(RTWDOG + 4) = 0xd928c520u;
    } else {
        R16(RTWDOG + 4) = 0xc520u;
        R16(RTWDOG + 4) = 0xd928u;
    }
    R32(RTWDOG + 8) = 0xffffu;                  /* TOVAL */
    R32(RTWDOG) = (R32(RTWDOG) & ~0x80u) | 0x20u;

    R32(SYST_CSR) = 0;
    for (int i = 0; i < 8; i++) {
        R32(NVIC_ICER + 4 * i) = 0xffffffffu;
        R32(NVIC_ICPR + 4 * i) = 0xffffffffu;
    }
}

/* clean + invalidate (clean = 1) or only invalidate the whole D-cache, by set/way */
static void dcache_by_set_way(int clean)
{
    R32(SCB_CSSELR) = 0;
    dsb();
    uint32_t ccsidr = R32(SCB_CCSIDR);
    uint32_t sets = (ccsidr >> 13) & 0x7fff, ways = (ccsidr >> 3) & 0x3ff;
    for (uint32_t s = 0; s <= sets; s++)
        for (uint32_t w = 0; w <= ways; w++)
            R32(clean ? SCB_DCCISW : SCB_DCISW) = (s << 5) | (w << 30);
    dsb();
}

void system_memory_init(void)
{
    if (R32(SCB_CCR) & SCB_CCR_DC) {            /* the bootloader may have left the cache on */
        dcache_by_set_way(1);
        R32(SCB_CCR) &= ~SCB_CCR_DC;
        dsb();
        isb();
    }
    if (R32(SCB_CCR) & SCB_CCR_IC) {
        R32(SCB_CCR) &= ~SCB_CCR_IC;
        dsb();
        isb();
    }

    dsb();
    R32(MPU_CTRL) = 0;
    for (unsigned i = 0; i < sizeof mpu_map / sizeof mpu_map[0]; i++) {
        R32(MPU_RBAR) = mpu_map[i][0];
        R32(MPU_RASR) = mpu_map[i][1];
    }
    R32(MPU_CTRL) = 5;                          /* enable, default map for privileged accesses */
    R32(SCB_SHCSR) |= 7u << 16;                 /* MemManage, BusFault, UsageFault reported as such (safety.c) */
    dsb();
    isb();

    R32(SCB_ICIALLU) = 0;                       /* I-cache: invalidate, enable */
    dsb();
    isb();
    R32(SCB_CCR) |= SCB_CCR_IC;
    dsb();
    isb();
    dcache_by_set_way(0);                       /* D-cache: invalidate, enable */
    R32(SCB_CCR) |= SCB_CCR_DC;
    dsb();
    isb();
}

uint32_t cpu_irq_disable(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    return primask;
}

void cpu_irq_restore(uint32_t state)
{
    __asm volatile("msr primask, %0" :: "r"(state) : "memory");
}

void cpu_dcache_flush(void)
{
    if (R32(SCB_CCR) & SCB_CCR_DC) {
        dcache_by_set_way(1);
        isb();
    }
}

/* FUN_00030020: caches off (the data written back first), the bootloader's request word, a system reset */
void system_reset_to_bootloader(void)
{
    __asm volatile("cpsid i" ::: "memory");
    dsb();
    R32(SCB_CCR) &= ~SCB_CCR_DC;
    dsb();
    dcache_by_set_way(1);
    isb();
    R32(SCB_CCR) &= ~SCB_CCR_IC;
    R32(SCB_ICIALLU) = 0;
    dsb();
    isb();
    R32(BOOT_REQUEST) = BOOT_REQUEST_UPDATE;
    dsb();
    R32(SCB_AIRCR) = 0x05fa0004u | (R32(SCB_AIRCR) & 0x700u);
    dsb();
    for (;;) {}
}

/*
 * Core clock from the cycle counter against GPT1 running on the 24 MHz crystal (measured before and after
 * clock.c sets the original's 600 MHz).
 */
uint32_t system_measure_core_hz(void)
{
    R32(DEMCR) |= 1u << 24;
    R32(DWT_LAR) = 0xc5acce55u;
    R32(DWT_CYCCNT) = 0;
    R32(DWT_CTRL) |= 1u;

    R32(GPT_CR) = 0;
    R32(GPT_CR) = 1u << 15;                     /* software reset */
    for (int i = 0; i < 1000 && (R32(GPT_CR) & (1u << 15)); i++) {}
    R32(GPT_PR) = 0;                            /* no prescaler, 24 MHz input not divided */
    R32(GPT_CR) = (1u << 10) | (5u << 6) | (1u << 9) | (1u << 1); /* EN_24M, CLKSRC = crystal, FRR, ENMOD */
    R32(GPT_CR) |= 1u;

    uint32_t g0 = R32(GPT_CNT), c0 = R32(DWT_CYCCNT);
    while (R32(GPT_CNT) - g0 < 240000u) {       /* 10 ms */
        if (R32(DWT_CYCCNT) - c0 > 2000000000u)
            break;                              /* GPT not counting */
    }
    uint32_t ticks = R32(GPT_CNT) - g0, cycles = R32(DWT_CYCCNT) - c0;
    R32(GPT_CR) = 0;
    if (ticks < 200000u || !cycles)
        return 600000000u;                      /* could not measure: assume what the original sets */
    return (uint32_t)((uint64_t)cycles * 24000000u / ticks);
}
