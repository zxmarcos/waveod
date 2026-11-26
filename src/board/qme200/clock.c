/*
 * Core clock at 600 MHz, as the original application sets it at start-up (FUN_0001BCA4, read from its MMIO
 * trace: research/extracted/mmio_trace.txt). The stock effects are written for it: the CPU overload rule
 * counts 1.25 ms per 64-frame block, and the NAM module times itself on SysTick at 600 000 ticks per ms.
 *
 * Only the part that sets the core is reproduced, in the original's order:
 *   1. periph_clk on the 24 MHz crystal (periph_clk2) while the ARM PLL changes;
 *   2. VDD_SOC 1.275 V (DCDC_REG3.TRG = 0x13);
 *   3. ARM PLL (PLL1) 24 MHz x 100 / 2 = 1200 MHz, bypassed until it locks;
 *   4. ARM_PODF /2 = 600 MHz core, AHB /1, IPG /4 = 150 MHz;
 *   5. pre_periph_clk = PLL1 / ARM_PODF, periph_clk back on it.
 * Everything that clock function also does is left as it is: the bootloader sets the SDRAM, the FlexSPI and
 * the system PLL's fractional dividers (the SDRAM and the flash run while this code runs), the other users
 * set their own clocks (lcd.c the video PLL, usb_dcd.c the USB PLL, midi_hw.c reads the UART clock it gets),
 * and the GPTs count the crystal directly.
 *
 * When the bootloader already left these settings, nothing is written. Runs from ITCM with interrupts off
 * and touches no SDRAM while the clocks move.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"

#define CCM_CACRR   (CCM + 0x10)
#define CCM_CBCDR   (CCM + 0x14)
#define CCM_CDHIPR  (CCM + 0x48)
#define PLL_ARM     (ANATOP + 0x00)
#define DCDC_REG0   0x40080000u
#define DCDC_REG3   0x4008000cu

#define CBCDR_PERIPH_CLK_SEL  (1u << 25)
#define CBCDR_AHB_PODF        (7u << 10)
#define CBCDR_IPG_PODF        (3u << 8)
#define CBCMR_PERIPH_CLK2_SEL (3u << 12)
#define CBCMR_PRE_PERIPH_SEL  (3u << 18)
#define PLL_LOCK      (1u << 31)
#define PLL_BYPASS    (1u << 16)
#define PLL_BYPASS_SRC (3u << 14)
#define PLL_ENABLE    (1u << 13)
#define PLL_POWERDOWN (1u << 12)
#define PLL_DIV       0x7fu
#define ARM_PLL_DIV   100u                      /* 24 MHz x 100 / 2 = 1200 MHz */
#define DCDC_TRG      0x13u                     /* 0.8 V + 19 x 25 mV = 1.275 V */

/* the CCM handshake after a divider or mux change (CDHIPR busy bits) */
static void handshake(void)
{
    for (int i = 0; i < 100000 && (R32(CCM_CDHIPR) & 0x1002bu); i++) {}  /* SEMC, AHB, PERIPH2, PERIPH, ARM */
}

static int at_600(void)
{
    uint32_t pll = R32(PLL_ARM), cbcdr = R32(CCM_CBCDR), cbcmr = R32(CCM_CBCMR);
    return (pll & PLL_LOCK) && !(pll & (PLL_BYPASS | PLL_POWERDOWN)) && (pll & PLL_ENABLE) &&
           (pll & PLL_DIV) == ARM_PLL_DIV && (R32(CCM_CACRR) & 7u) == 1u &&
           !(cbcdr & CBCDR_PERIPH_CLK_SEL) && !(cbcdr & CBCDR_AHB_PODF) && (cbcdr & CBCDR_IPG_PODF) == CBCDR_IPG_PODF &&
           (cbcmr & CBCMR_PRE_PERIPH_SEL) == CBCMR_PRE_PERIPH_SEL && (R32(DCDC_REG3) & 0x1fu) >= DCDC_TRG;
}

int clock_init(void)
{
    if (at_600())
        return 0;

    /* 1. the core on the crystal */
    R32(CCM_CBCMR) = (R32(CCM_CBCMR) & ~CBCMR_PERIPH_CLK2_SEL) | (1u << 12);    /* periph_clk2 = osc */
    R32(CCM_CBCDR) |= CBCDR_PERIPH_CLK_SEL;
    handshake();

    /* 2. the core voltage for 600 MHz, before the frequency goes up */
    if ((R32(DCDC_REG3) & 0x1fu) < DCDC_TRG) {
        R32(DCDC_REG3) = (R32(DCDC_REG3) & ~0x1fu) | DCDC_TRG;
        for (int i = 0; i < 100000 && !(R32(DCDC_REG0) & (1u << 31)); i++) {}  /* STS_DC_OK */
    }

    /* 3. ARM PLL: bypass from the crystal, the divider, wait for the lock, then the PLL itself */
    R32(PLL_ARM) = (R32(PLL_ARM) & ~PLL_BYPASS_SRC) | PLL_BYPASS;
    R32(PLL_ARM) = (R32(PLL_ARM) & ~(PLL_DIV | PLL_POWERDOWN)) | PLL_ENABLE | ARM_PLL_DIV;
    for (int i = 0; i < 1000000 && !(R32(PLL_ARM) & PLL_LOCK); i++) {}
    R32(PLL_ARM) &= ~PLL_BYPASS;

    /* 4. dividers */
    R32(CCM_CACRR) = 1u;                                                        /* ARM_PODF /2 */
    handshake();
    R32(CCM_CBCDR) &= ~CBCDR_AHB_PODF;                                          /* AHB /1 */
    handshake();
    R32(CCM_CBCDR) = (R32(CCM_CBCDR) & ~CBCDR_IPG_PODF) | (3u << 8);            /* IPG /4 */

    /* 5. the core on the ARM PLL */
    R32(CCM_CBCMR) = (R32(CCM_CBCMR) & ~CBCMR_PRE_PERIPH_SEL) | CBCMR_PRE_PERIPH_SEL;
    R32(CCM_CBCDR) &= ~CBCDR_PERIPH_CLK_SEL;
    handshake();
    return 1;
}
