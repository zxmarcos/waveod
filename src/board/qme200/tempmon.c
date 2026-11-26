/*
 * The CPU die's temperature: the i.MX RT1062's on-chip temperature monitor (TEMPMON, in the analog block),
 * calibrated by the two points NXP fuses at the factory (OCOTP ANA1: the count at 25 C and at a hot temperature).
 * As NXP's fsl_tempmon.c does it: powered up, measuring on its own every half second (TEMPSENSE1, in periods of
 * the 32.768 kHz clock); the count read is the last measurement. The count falls as the die warms:
 *
 *     T = hot_temp - (count - hot_count) * (hot_temp - 25) / (room_count - hot_count)
 *
 * No alarm is set (TEMPSENSE0 / 2 keep their reset values, the panic alarm off): this only reads.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/platform.h"

#define TEMPSENSE0      (TEMPMON + 0x00)
#define TEMPSENSE0_SET  (TEMPMON + 0x04)
#define TEMPSENSE0_CLR  (TEMPMON + 0x08)
#define TEMPSENSE1      (TEMPMON + 0x10)
#define TS0_POWER_DOWN  (1u << 0)
#define TS0_MEASURE     (1u << 1)
#define TS0_FINISHED    (1u << 2)
#define TS0_COUNT(v)    (((v) >> 8) & 0xfffu)
#define MEASURE_EVERY   16384u                  /* 32.768 kHz periods: 0.5 s */
#define ROOM_TEMP       25

static int hot_temp, hot_count, room_count;     /* 0 = no usable calibration */

void tempmon_init(void)
{
    uint32_t cal = R32(OCOTP_ANA1);
    hot_temp = (int)(cal & 0xffu);
    hot_count = (int)((cal >> 8) & 0xfffu);
    room_count = (int)((cal >> 20) & 0xfffu);
    if (hot_temp <= ROOM_TEMP || hot_temp > 150 || room_count <= hot_count) {
        hot_temp = 0;                           /* an unfused or odd part: say nothing rather than a wrong number */
        return;
    }
    R32(TEMPSENSE0_CLR) = TS0_POWER_DOWN;
    R32(TEMPSENSE1) = MEASURE_EVERY;
    R32(TEMPSENSE0_SET) = TS0_MEASURE;          /* from here on it measures by itself */
}

int plat_cpu_temp(void)
{
    if (!hot_temp)
        return PLAT_TEMP_NONE;
    uint32_t v = R32(TEMPSENSE0);
    if (!(v & TS0_FINISHED))                    /* the first measurement is not in yet */
        return PLAT_TEMP_NONE;
    int n = (int)TS0_COUNT(v);
    /* tenths of a degree, rounded */
    int num = (n - hot_count) * (hot_temp - ROOM_TEMP) * 10, den = room_count - hot_count;
    int t = hot_temp * 10 - (num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
    return t < -500 || t > 1500 ? PLAT_TEMP_NONE : t;
}
