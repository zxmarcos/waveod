#include "model/sysmon.h"
#include "platform/platform.h"
#include "platform/time.h"
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define EVERY_MS 1000u

static struct sysmon m = { PLAT_TEMP_NONE, PLAT_TEMP_NONE, PLAT_TEMP_NONE, 0 };
static uint32_t last_ms;
static int sampled;

void sysmon_poll(void)
{
    uint32_t now = time_ms();
    if (sampled && now - last_ms < EVERY_MS)
        return;
    sampled = 1;
    last_ms = now;
    int t = plat_cpu_temp();
    if (t == PLAT_TEMP_NONE)
        return;
    if (m.temp_max == PLAT_TEMP_NONE || t > m.temp_max)
        m.temp_max = t;
    if (m.temp_min == PLAT_TEMP_NONE || t < m.temp_min)
        m.temp_min = t;
    if (t != m.temp) {
        m.temp = t;
        m.seq++;
    }
}

const struct sysmon *sysmon_get(void)
{
    return &m;
}

void sysmon_temp_text(int tenths, char *out, unsigned size)
{
    if (!size)
        return;
    if (tenths == PLAT_TEMP_NONE) {
        const char *dash = "\xE2\x80\x94";
        unsigned i = 0;
        for (; dash[i] && i + 1 < size; i++)
            out[i] = dash[i];
        out[i] = 0;
        return;
    }
    char buf[16];
    unsigned n = 0;
    int v = tenths;
    if (v < 0) {
        buf[n++] = '-';
        v = -v;
    }
    char digits[8];
    int k = 0, whole = v / 10;
    do {
        digits[k++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole && k < 7);
    while (k)
        buf[n++] = digits[--k];
    buf[n++] = '.';
    buf[n++] = (char)('0' + v % 10);
    const char *unit = " \xC2\xB0" "C";
    for (int i = 0; unit[i]; i++)
        buf[n++] = unit[i];
    unsigned i = 0;
    for (; i < n && i + 1 < size; i++)
        out[i] = buf[i];
    out[i] = 0;
}
