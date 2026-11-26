/*
 * Time base: SysTick interrupt every millisecond, reloading at core_hz / 1000 like the original app (the NAM
 * Clone module reads SYST_CVR to time itself), and the DWT cycle counter for short delays and profiling.
 * SysTick has the highest priority, so the software watchdog (safety.c) runs in it even while the audio
 * interrupt does not return.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/time.h"

#define SCB_SHPR3 0xe000ed20u

volatile uint32_t ms_count;
static uint32_t cycles_per_us = 600;

/* the exception frame of the interrupted code goes to the watchdog (only the MSP is used) */
__attribute__((naked)) void systick_handler(void)
{
    __asm volatile("mrs r0, msp\n"
                   "b systick_tick\n");
}

__attribute__((used)) void systick_tick(const uint32_t *frame)
{
    uint32_t now = ++ms_count;
    safety_tick(frame, now);
}

void time_init(uint32_t core_hz)
{
    cycles_per_us = core_hz / 1000000u;
    if (!cycles_per_us)
        cycles_per_us = 600;
    R32(SCB_SHPR3) &= 0x00ffffffu;              /* SysTick priority 0 */
    R32(SYST_RVR) = core_hz / 1000u - 1u;
    R32(SYST_CVR) = 0;
    R32(SYST_CSR) = 7;                          /* core clock, interrupt, enable */
}

uint32_t time_ms(void)
{
    return ms_count;
}

uint32_t time_cycles(void)
{
    return R32(DWT_CYCCNT);
}

void time_delay_us(uint32_t us)
{
    uint32_t t0 = R32(DWT_CYCCNT), n = us * cycles_per_us;
    while (R32(DWT_CYCCNT) - t0 < n) {}
}

void time_delay_ms(uint32_t ms)
{
    while (ms--)
        time_delay_us(1000);
}
