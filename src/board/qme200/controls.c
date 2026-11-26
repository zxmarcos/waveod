/*
 * Physical controls of the QME-200, mapped on the pedal with the proof-of-concept input monitor:
 *   - buttons and footswitches on the shift register chain (bit = 0 while pressed);
 *   - the two switches of the EXP 2 jack on GPIO1[16] / GPIO1[22] (0 while pressed), read by the original into
 *     its key word as keys 0x1A / 0x1B (FUN_00022AE0);
 *   - the big MENU encoder and the three knobs under the display on the quadrature decoders ENC1-4,
 *     wired through XBARA1 like the original;
 *   - MASTER volume and the two expression pedals on ADC1 through ADC_ETC (software triggered here).
 */
#include "board/qme200/board.h"
#include "board/qme200/board_v122.h"
#include "board/qme200/regs.h"
#include "platform/controls.h"
#include "platform/time.h"

#define POLL_MS 5

/* shift register word (in[0] << 24 | ... | in[3]) bit of each button */
static const struct { uint32_t raw; uint32_t button; } button_map[] = {
    { 1u << 31, BTN_MENU },      { 1u << 24, BTN_EXIT },      { 1u << 25, BTN_EFX },
    { 1u << 26, BTN_SAVE },      { 1u << 27, BTN_DRUM },      { 1u << 28, BTN_GLOBAL },
    { 1u << 29, BTN_CTRL },      { 1u << 30, BTN_EXP },       { 1u << 21, BTN_FS_A },
    { 1u << 20, BTN_FS_B },      { 1u << 23, BTN_FS_C },      { 1u << 22, BTN_FS_D },
    { 1u << 17, BTN_BANK_UP },   { 1u << 16, BTN_BANK_DOWN },
};

/* decoder of each encoder, and its direction (+1 = clockwise counts up) */
static const uint8_t enc_unit[ENC_COUNT] = { 0, 1, 2, 3 };
static const int8_t enc_sign[ENC_COUNT] = { 1, 1, 1, 1 };

/* ADC_ETC result slot of each potentiometer */
static const uint8_t pot_channel[POT_COUNT] = { 13, 15, 11 };

#define ADC1_HC(n) (ADC1 + 4 * (n))
#define ADC1_CFG   (ADC1 + 0x44)
#define ADC1_GC    (ADC1 + 0x48)
#define ADC1_GS    (ADC1 + 0x4c)
#define ETC_CTRL          (ADC_ETC + 0x00)
#define ETC_DONE01        (ADC_ETC + 0x04)
#define ETC_TRIG_CTRL(t)  (ADC_ETC + 0x10 + 0x28 * (t))
#define ETC_TRIG_CNT(t)   (ADC_ETC + 0x14 + 0x28 * (t))
#define ETC_CHAIN(t, k)   (ADC_ETC + 0x18 + 0x28 * (t) + 4 * (k))
#define ETC_RESULT(t, k)  (ADC_ETC + 0x28 + 0x28 * (t) + 4 * (k))

static struct controls_raw last;
static uint32_t polled_at;
static int adc_started;

static void adc_init(void)
{
    R32(ADC1_CFG) = 0xc338u;                    /* 12 bit, long sample, 32-sample average */
    R32(ADC1_GC) = 0x21u;
    R32(ADC1_GS) = 2u;
    R32(ADC1_GC) = 0xa1u;                       /* calibrate */
    for (uint32_t t = 0; (R32(ADC1_GC) & 0x80u) && t < 1000; t++)
        time_delay_us(10);
    /* conversions come from ADC_ETC: two 8-long chains cover channels 0-15 */
    R32(ADC1_CFG) = 0xe338u;
    R32(ADC1_HC(0)) = 16;
    R32(ADC1_HC(1)) = 16;
    R32(ETC_CTRL) = 3u;
    for (int t = 0; t < 2; t++) {
        R32(ETC_TRIG_CTRL(t)) = (7u << 8) | (1u << 4);
        R32(ETC_TRIG_CNT(t)) = 0;
        uint32_t hwts = t ? 2u : 1u;
        for (int k = 0; k < 4; k++) {
            uint32_t c0 = (uint32_t)(t * 8 + 2 * k), c1 = c0 + 1;
            uint32_t ie = k == 3 ? 1u << 13 : 0;
            R32(ETC_CHAIN(t, k)) = (c0 | hwts << 4 | 1u << 12) | (c1 | hwts << 4 | 1u << 12 | ie) << 16;
        }
    }
}

/* start both chains (non-blocking); results are collected on the next poll */
static void adc_start(void)
{
    for (int t = 0; t < 2; t++) {
        R32(ETC_DONE01) = 1u << t;
        R32(ETC_TRIG_CTRL(t)) |= 1u;
    }
    adc_started = 1;
}

static void adc_collect(uint16_t *pots)
{
    if (!adc_started || (R32(ETC_DONE01) & 3u) != 3u)
        return;
    for (int p = 0; p < POT_COUNT; p++) {
        unsigned ch = pot_channel[p], t = ch / 8, k = (ch % 8) / 2;
        uint32_t r = R32(ETC_RESULT(t, k));
        pots[p] = (uint16_t)((ch & 1 ? r >> 16 : r) & 0xfffu);
    }
}

void controls_init(void)
{
    adc_init();
    for (unsigned i = 0; i < sizeof xbar_v122 / sizeof xbar_v122[0]; i++)
        R16(xbar_v122[i][0]) = (uint16_t)xbar_v122[i][1];
    for (unsigned i = 0; i < sizeof enc_v122 / sizeof enc_v122[0]; i++)
        R16(enc_v122[i][0]) = (uint16_t)enc_v122[i][1];
    adc_start();
}

static int32_t enc_position(int unit)
{
    uint32_t hi = R16(ENC(unit) + 0x0e);        /* UPOS (reading it latches LPOS) */
    uint32_t lo = R16(ENC(unit) + 0x10);
    return (int32_t)(hi << 16 | lo);
}

void controls_read(struct controls_raw *out)
{
    uint32_t now = time_ms();
    if (now - polled_at >= POLL_MS || !polled_at) {
        polled_at = now ? now : 1;
        uint8_t keys[4];
        shift_reg_xfer(shift_reg_out, keys);
        uint32_t word = (uint32_t)keys[0] << 24 | (uint32_t)keys[1] << 16 | (uint32_t)keys[2] << 8 | keys[3];
        uint32_t buttons = 0;
        for (unsigned i = 0; i < sizeof button_map / sizeof button_map[0]; i++)
            if (!(word & button_map[i].raw))
                buttons |= button_map[i].button;
        uint32_t gpio1 = R32(GPIO1);            /* DR reads the pad of an input pin */
        if (!(gpio1 & (1u << 16)))
            buttons |= BTN_EXT1;
        if (!(gpio1 & (1u << 22)))
            buttons |= BTN_EXT2;
        last.buttons = buttons;
        adc_collect(last.pot);
        adc_start();
    }
    for (int e = 0; e < ENC_COUNT; e++)
        last.enc[e] = enc_sign[e] * enc_position(enc_unit[e]);
    *out = last;
}
