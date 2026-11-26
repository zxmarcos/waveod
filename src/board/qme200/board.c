/* Bring-up order of the QME-200, following the original main (FUN_000424D4) as far as it is needed. */
#include "board/qme200/board.h"
#include "board/qme200/board_v122.h"
#include "board/qme200/regs.h"
#include "platform/flash.h"
#include "platform/platform.h"
#include "platform/time.h"

#include "platform/safety.h"

static uint32_t core_hz, boot_hz;

const char *plat_name(void)
{
    return "QME-200";
}

uint32_t plat_core_hz(void)
{
    return core_hz;
}

uint32_t plat_boot_core_hz(void)
{
    return boot_hz;
}

void plat_init(void)
{
    for (int i = 0; i < 8; i++)
        R32(CCM_CCGR(i)) = 0xffffffffu;         /* every clock gate on, as the original app does */
    R32(DMA0_ERQ) = 0;                          /* no DMA request left running by the bootloader */
    safety_boot();                              /* the previous run's crash record, before anything else */
    system_memory_init();
    boot_hz = core_hz = system_measure_core_hz();
    if (clock_init())                           /* the original's 600 MHz, when the bootloader left less */
        core_hz = system_measure_core_hz();
    time_init(core_hz);
    tempmon_init();
    __asm volatile("msr basepri, %0" :: "r"(0u) : "memory");    /* the bootloader may leave a mask */
    __asm volatile("cpsie i" ::: "memory");                     /* SysTick from here on */
    flexspi_init();
    /* The bootloader (0x000000-0x01FFFF, with the unit's license block at 0x4000) write-protected in the chip until
       the power goes, before any effect module can run: a module drives the flash controller as it likes, so
       only the chip can refuse. Here, before pins_init gives the FlexSPI pads the original's settings: status
       register reads after that misread on the pedal (SR2 02 -> 03, SR3 60 -> 70). The lock test build locks the
       top 4 KB instead. */
#ifdef WAVEOD_LOCK_TEST
    flexspi_lock(FLASH_PROTECT_TOP4K);
#else
    flexspi_lock(FLASH_PROTECT_BOOT);
#endif
    pins_init();
    gpio_write(GPIO2, 22, 1);                   /* analog section enable: without it the inputs read silence */
    shift_reg_xfer(shift_boot_v122, 0);         /* virtual pin 27 stays 0 until the audio is up */
    time_delay_ms(10);
    controls_init();
    lcd_init();
    touch_init();
}

void plat_enter_bootloader(uint32_t delay_ms)
{
    safety_disarm();                        /* the bootloader does not feed a watchdog */
    time_delay_ms(delay_ms);                /* the reply leaves, the output fades */
    system_reset_to_bootloader();
}

/* filled by tools/mkfw.py (qme200.ld: .drum_pack) */
extern const uint8_t __drum_pack_start[];

const void *plat_drum_pack(void)
{
    return __drum_pack_start;
}
