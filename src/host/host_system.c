/* Simulator: platform identity, virtual time and the RAM words modules expect in the original app. */
#include "engine/host_abi.h"
#include "host/host.h"
#include "platform/cpu.h"
#include "platform/platform.h"
#include "platform/time.h"

struct host_abi_mirror host_abi;


void plat_init(void) {}
const char *plat_name(void) { return "simulator"; }
uint32_t plat_core_hz(void) { return 0; }
uint32_t plat_boot_core_hz(void) { return 0; }
int plat_cpu_temp(void) { return PLAT_TEMP_NONE; }

int host_bootloader_requests;

void plat_enter_bootloader(uint32_t delay_ms)
{
    (void)delay_ms;
    host_bootloader_requests++;
}


static uint32_t now_ms;

uint32_t time_ms(void) { return now_ms; }
void time_delay_ms(uint32_t ms) { now_ms += ms; }
void time_delay_us(uint32_t us) { (void)us; }
uint32_t time_cycles(void) { return 0; }
void host_time_advance(uint32_t ms) { now_ms += ms; }

uint32_t cpu_irq_disable(void) { return 0; }
void cpu_irq_restore(uint32_t state) { (void)state; }
void cpu_dcache_flush(void) {}
