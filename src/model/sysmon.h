/*
 * What the system tells about itself while it runs: the CPU's temperature, sampled once a second in the main loop
 * (plat_cpu_temp), with the highest and lowest since start-up. Shown in GLOBAL > SISTEMA and sent to waveod Studio
 * (editor/wv_settings_msgs.c, read-only settings).
 */
#pragma once

#include <stdint.h>

struct sysmon {
    int temp, temp_max, temp_min;   /* tenths of a degree C; PLAT_TEMP_NONE when unknown */
    uint32_t seq;                   /* counts the samples whose value changed (screens redraw on it) */
};

void sysmon_poll(void);
const struct sysmon *sysmon_get(void);

/* "47.3 °C" (UTF-8), or "—" when unknown */
void sysmon_temp_text(int tenths, char *out, unsigned size);
