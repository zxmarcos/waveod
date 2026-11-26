/*
 * Start-up of the QME-200 application.
 *
 * The bootloader copies the ITCM image to 0x0 and the SDRAM image to 0x80000000 (both described in the MCU
 * section header, see tools/mkfw.py), sets SP from the vector table and jumps to the reset vector.
 * Initialised data lives inside those images, so only the zero-initialised areas are cleared here.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include <stdint.h>

extern uint32_t __bss_start[], __bss_end[];             /* OCRAM */
extern uint32_t __sdram_bss_start[], __sdram_bss_end[];
extern uint32_t __host_abi_start[], __host_abi_end[];
extern uint32_t __stack_top[];

int main(void);
void reset_handler(void);
void reset_c(void);

/* safety.c: every fault and every interrupt nobody handles records what happened and restarts the pedal */
void fault_entry(void);

void systick_handler(void);         /* time.c */
void dma0_dma16_irq(void);          /* audio.c */
void usb_otg1_irq(void);            /* usb_dcd.c */
void lpuart1_irq(void);             /* midi_hw.c */
void gpt2_irq(void);                /* midi_hw.c */

/* 16 core exceptions + 160 i.MX RT1062 interrupts */
__attribute__((section(".vectors"), used))
const void *const vectors[176] = {
    (const void *)0x20008000,           /* top of the 32 KB DTCM, like the original app */
    (const void *)reset_handler,
    [2 ... 14] = (const void *)fault_entry,
    [15] = (const void *)systick_handler,
    [16 + 0] = (const void *)dma0_dma16_irq,
    [17 ... 175] = (const void *)fault_entry,
    [16 + 20] = (const void *)lpuart1_irq,
    [16 + 101] = (const void *)gpt2_irq,
    [16 + 113] = (const void *)usb_otg1_irq,
};

/* at 0x470, where the original reset handler lived */
__attribute__((section(".reset_fixed"), naked, used))
void reset_fixed(void)
{
    __asm volatile("b.w reset_handler");
}

__attribute__((naked))
void reset_handler(void)
{
    __asm volatile(
        "cpsid i\n"
        "movw r0, #0x8000\n"
        "movt r0, #0x2000\n"
        "msr msp, r0\n"
        "movs r1, #0\n"
        "msr control, r1\n"
        "isb\n"
        "mov sp, r0\n"
        "b.w reset_c\n");
}

static void zero(uint32_t *p, uint32_t *end)
{
    while (p < end)
        *p++ = 0;
}

__attribute__((used)) void reset_c(void)
{
    system_early_init();
    zero(__bss_start, __bss_end);
    zero(__sdram_bss_start, __sdram_bss_end);
    zero(__host_abi_start, __host_abi_end);
    main();
    for (;;) {}
}
