/* What a measurement needs from the CPU (engine/bench.c). */
#pragma once

#include <stdint.h>

/* Interrupts off (PRIMASK set); returns what cpu_irq_restore takes. */
uint32_t cpu_irq_disable(void);
void cpu_irq_restore(uint32_t state);

/* The whole data cache cleaned and invalidated: the next accesses miss (nothing without a cache). */
void cpu_dcache_flush(void);
