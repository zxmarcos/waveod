/* Time keeping. */
#pragma once

#include <stdint.h>

/* Milliseconds since start-up (wraps after ~49 days). */
uint32_t time_ms(void);

/* Busy wait. */
void time_delay_ms(uint32_t ms);
void time_delay_us(uint32_t us);

/* Free running cycle counter, for profiling (0 if the platform has none). */
uint32_t time_cycles(void);
