/* i.MX RT1062 registers used by the QME-200 board code. */
#pragma once

#include <stdint.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

/* core */
#define SCB_VTOR    0xe000ed08u
#define SCB_AIRCR   0xe000ed0cu
#define SCB_CCR     0xe000ed14u
#define SCB_SHCSR   0xe000ed24u
#define SCB_CCSIDR  0xe000ed80u
#define SCB_CSSELR  0xe000ed84u
#define SCB_CPACR   0xe000ed88u
#define SCB_ICIALLU 0xe000ef50u
#define SCB_DCISW   0xe000ef60u
#define SCB_DCCISW  0xe000ef74u
#define MPU_CTRL    0xe000ed94u
#define MPU_RBAR    0xe000ed9cu
#define MPU_RASR    0xe000eda0u
#define SYST_CSR    0xe000e010u
#define SYST_RVR    0xe000e014u
#define SYST_CVR    0xe000e018u
#define NVIC_ISER   0xe000e100u
#define NVIC_ICER   0xe000e180u
#define NVIC_ISPR   0xe000e200u
#define NVIC_ICPR   0xe000e280u
#define NVIC_IPR    0xe000e400u
#define DEMCR       0xe000edfcu
#define DWT_CTRL    0xe0001000u
#define DWT_CYCCNT  0xe0001004u
#define DWT_LAR     0xe0001fb0u

#define SCB_CCR_DC  (1u << 16)
#define SCB_CCR_IC  (1u << 17)

/* the word the bootloader reads after a reset: 0x5A5A5A5A = stay in the update mode (FUN_00030020) */
#define BOOT_REQUEST         0x400a4000u
#define BOOT_REQUEST_UPDATE  0x5a5a5a5au

/* watchdogs */
#define WDOG1       0x400b8000u
#define WDOG2       0x400d0000u
#define RTWDOG      0x400bc000u

/* clocks */
#define CCM         0x400fc000u
#define CCM_CBCMR   (CCM + 0x18)
#define CCM_CSCDR2  (CCM + 0x38)
#define CCM_CCGR(n) (CCM + 0x68 + 4 * (n))
#define ANATOP      0x400d8000u
#define PLL_VIDEO        (ANATOP + 0xa0)
#define PLL_VIDEO_NUM    (ANATOP + 0xb0)
#define PLL_VIDEO_DENOM  (ANATOP + 0xc0)
#define ANATOP_MISC2     (ANATOP + 0x170)
#define TEMPMON          (ANATOP + 0x180)       /* the temperature monitor (tempmon.c) */
#define OCOTP_ANA1       0x401f44e0u            /* its factory calibration (fuse shadow) */

/* GPT1: free-running counter on the 24 MHz crystal, used to measure the core clock */
#define GPT1        0x401ec000u
#define GPT_CR      (GPT1 + 0x00)
#define GPT_PR      (GPT1 + 0x04)
#define GPT_CNT     (GPT1 + 0x24)

/* GPIO (DR at +0, GDIR +4, PSR +8, IMR +0x14) */
#define GPIO1       0x401b8000u
#define GPIO2       0x401bc000u
#define GPIO3       0x401c0000u

/* ADC1 */
#define ADC1        0x400c4000u
#define ADC_ETC     0x403b0000u

/* eDMA + DMAMUX */
#define DMA0_CR     0x400e8000u
#define DMA0_ES     0x400e8004u
#define DMA0_ERQ    0x400e800cu
#define DMA0_SERQ   0x400e801bu
#define DMA0_CINT   0x400e801fu
#define DMA0_INT    0x400e8024u
#define DMA0_ERR    0x400e802cu
#define DMA_TCD(ch) (0x400e9000u + 0x20u * (ch))
#define DMAMUX(ch)  (0x400ec000u + 4u * (ch))

/* SAI1 */
#define SAI1        0x40384000u
#define SAI1_TCSR   (SAI1 + 0x08)
#define SAI1_TDR0   (SAI1 + 0x20)
#define SAI1_RCSR   (SAI1 + 0x88)
#define SAI1_RDR0   (SAI1 + 0xa0)

/* eLCDIF */
#define LCDIF            0x402b8000u
#define LCDIF_CTRL       (LCDIF + 0x00)
#define LCDIF_CTRL_SET   (LCDIF + 0x04)
#define LCDIF_CTRL_CLR   (LCDIF + 0x08)
#define LCDIF_CTRL1      (LCDIF + 0x10)
#define LCDIF_CTRL1_CLR  (LCDIF + 0x18)
#define LCDIF_TRANSFER   (LCDIF + 0x30)
#define LCDIF_CUR_BUF    (LCDIF + 0x40)
#define LCDIF_NEXT_BUF   (LCDIF + 0x50)
#define LCDIF_VDCTRL(n)  (LCDIF + 0x70 + 0x10 * (n))

/* FlexPWM1 */
#define PWM1        0x403dc000u
#define PWM_SM(n)   (PWM1 + 0x60 * (n))
#define PWM_OUTEN   (PWM1 + 0x180)
#define PWM_MCTRL   (PWM1 + 0x188)

/* quadrature decoders ENC1..4 */
#define ENC(n)      (0x403c8000u + 0x4000u * (unsigned)(n))

static inline void dsb(void) { __asm volatile("dsb 0xf" ::: "memory"); }
static inline void isb(void) { __asm volatile("isb 0xf" ::: "memory"); }
