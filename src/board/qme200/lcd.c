/*
 * 480 x 272 RGB panel (Sitronix ST7701S) on the eLCDIF, XRGB8888 frame buffers at 0x81F00000 and +0x7F800
 * (the original app's), backlight on FlexPWM1 submodule 2 output A at 2 kHz.
 */
#include "board/qme200/board.h"
#include "board/qme200/board_v122.h"
#include "board/qme200/regs.h"
#include "platform/display.h"
#include "platform/time.h"

#define FB0 0x81f00000u
#define FB_BYTES (DISPLAY_W * DISPLAY_H * 4)

uint32_t *display_fb(int index)
{
    return (uint32_t *)(uintptr_t)(FB0 + (index ? FB_BYTES : 0));
}

/* pixel clock 9 MHz: PLL5 = 24 MHz x 45 / 2 / 4 = 135 MHz, LCDIF pre-divider /5, post-divider /3 */
static void lcd_clock(void)
{
    R32(PLL_VIDEO) = (R32(PLL_VIDEO) & ~(3u << 14)) | (1u << 16);              /* bypass from 24 MHz */
    R32(PLL_VIDEO_NUM) = 0;
    R32(PLL_VIDEO_DENOM) = 1;
    R32(ANATOP_MISC2) = (R32(ANATOP_MISC2) & ~(3u << 30)) | (3u << 30);       /* video_div /4 */
    R32(PLL_VIDEO) = (R32(PLL_VIDEO) & ~(0x7fu | (1u << 12) | (3u << 19)))
                     | 45u | (1u << 13) | (1u << 19);                           /* div 45, enable, post /2 */
    for (uint32_t t = 0; !(R32(PLL_VIDEO) & (1u << 31)) && t < 100000; t++) {}
    R32(PLL_VIDEO) &= ~(1u << 16);
    R32(CCM_CSCDR2) = (R32(CCM_CSCDR2) & ~(0x1ffu << 9)) | (2u << 15) | (4u << 12); /* PLL5, /5 */
    R32(CCM_CBCMR) = (R32(CCM_CBCMR) & ~(7u << 23)) | (2u << 23);                   /* /3 */
}

static void lcdif_init(void)
{
    R32(LCDIF_CTRL_CLR) = 1u << 0;
    R32(LCDIF_CTRL_CLR) = 1u << 30;                                /* CLKGATE */
    R32(LCDIF_CTRL_SET) = 1u << 31;                                /* SFTRST */
    for (uint32_t t = 0; !(R32(LCDIF_CTRL) & (1u << 30)) && t < 100000; t++) {}
    time_delay_us(10);
    R32(LCDIF_CTRL_CLR) = (1u << 31) | (1u << 30);
    /* the original app's ELCDIF_RgbModeInit values: 480 x 272, XRGB8888 -> 16-bit bus */
    R32(LCDIF_CTRL) = 0x000a0320u;
    R32(LCDIF_CTRL1) = 0x00070000u;
    R32(LCDIF_TRANSFER) = (DISPLAY_H << 16) | DISPLAY_W;
    R32(LCDIF_VDCTRL(0)) = 0x11300004u;
    R32(LCDIF_VDCTRL(1)) = 0x00000128u;
    R32(LCDIF_VDCTRL(2)) = 0x00100217u;
    R32(LCDIF_VDCTRL(3)) = 0x002f0010u;
    R32(LCDIF_VDCTRL(4)) = 0x000401e0u;
    R32(LCDIF_CUR_BUF) = FB0;
    R32(LCDIF_NEXT_BUF) = FB0;
    R32(LCDIF_CTRL1_CLR) = 0x0400f000u;
    R32(LCDIF_CTRL1_CLR) = 0x02000f00u;
    R32(LCDIF_CTRL_SET) = 0x00020001u;                             /* DOTCLK mode, RUN */
}

/* ST7701S: 9-bit SPI, CS GPIO2[30], SCL GPIO2[29] (rising edge), SDA GPIO2[28] */
static void panel_word(uint16_t w)
{
    gpio_write(GPIO2, 30, 1);
    gpio_write(GPIO2, 29, 0);
    gpio_write(GPIO2, 30, 0);
    time_delay_us(1);
    for (int k = 8; k >= 0; k--) {
        gpio_write(GPIO2, 29, 0);
        gpio_write(GPIO2, 28, (w >> k) & 1);
        time_delay_us(1);
        gpio_write(GPIO2, 29, 1);
        time_delay_us(1);
    }
    gpio_write(GPIO2, 30, 1);
    time_delay_us(1);
}

static void panel_init(void)
{
    gpio_write(GPIO2, 31, 1);
    time_delay_ms(10);
    gpio_write(GPIO2, 31, 0);
    time_delay_ms(10);
    gpio_write(GPIO2, 31, 1);
    time_delay_ms(120);
    for (const uint16_t *p = panel_v122; *p != PANEL_END; p++) {
        if (*p & PANEL_DELAY)
            time_delay_ms(*p & 0x7fffu);
        else
            panel_word(*p);
    }
    time_delay_ms(20);
}

void display_backlight(unsigned percent)
{
    const uint32_t sm = PWM_SM(2);
    const int32_t half = 18750, pulse = (int32_t)(37500u * (percent > 100 ? 100 : percent) / 100u);
    R16(PWM_MCTRL) &= ~(1u << (8 + 2));         /* RUN off */
    R16(PWM_MCTRL) |= 1u << (4 + 2);            /* CLDOK */
    R16(sm + 0x04) = 0xa000u;                   /* CTRL2: DBGEN, INDEP, IPG clock */
    R16(sm + 0x06) = 0x0410u;                   /* CTRL: full-cycle reload, prescaler /2 */
    R16(sm + 0x02) = (uint16_t)-half;           /* INIT */
    R16(sm + 0x0a) = 0;                         /* VAL0 */
    R16(sm + 0x0e) = (uint16_t)(half - 1);      /* VAL1 */
    R16(sm + 0x12) = (uint16_t)(-pulse / 2);    /* VAL2: A on */
    R16(sm + 0x16) = (uint16_t)(pulse / 2);     /* VAL3: A off */
    R16(sm + 0x22) = 0;                         /* OCTRL: normal polarity */
    R16(sm + 0x2c) = 0;                         /* faults never disable the output */
    R16(sm + 0x2e) = 0;
    R16(PWM_OUTEN) |= 1u << (8 + 2);            /* PWM_A of submodule 2 */
    R16(PWM_MCTRL) |= 1u << 2;                  /* LDOK */
    R16(PWM_MCTRL) |= 1u << (8 + 2);            /* RUN */
}

void display_show(int index)
{
    uint32_t fb = (uint32_t)(uintptr_t)display_fb(index);
    R32(LCDIF_NEXT_BUF) = fb;
    /* the controller latches NEXT_BUF at the end of the frame (~17.6 ms at 56.8 Hz) */
    uint32_t t0 = time_ms();
    while (R32(LCDIF_CUR_BUF) != fb && time_ms() - t0 < 40) {}
}

void lcd_init(void)
{
    lcd_clock();
    uint32_t *fb = display_fb(0);
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++)
        fb[i] = 0x000a0a09u;
    lcdif_init();
    panel_init();
    display_backlight(60);
}
