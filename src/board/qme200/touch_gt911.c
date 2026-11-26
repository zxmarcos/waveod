/*
 * Capacitive touch: Goodix GT911 on its own software I2C bus, as found in the original firmware
 * (FUN_0001E0A8 set-up, FUN_0001E290 read, GPIO3 interrupt FUN_00026D40):
 *   - SCL GPIO3[4], SDA GPIO3[5], about 300 kHz, 7-bit address 0x14 (INT is pulled up during reset);
 *   - reset GPIO1[3] (low 1 ms, then 70 ms before the first transfer);
 *   - INT GPIO3[3] (pad GPIO_SD_B1_03), falling edge on every new report;
 *   - a report is read as 6 bytes from 0x814E (status, track id, x, y little endian) and acknowledged by
 *     writing 0 to 0x814E. Status bit 7 = report ready, bits 0-3 = number of points (0 = released).
 * The original passes x / y to its UI unchanged, so the controller is configured for the 480 x 272 panel.
 *
 * Here the controller is polled from the main loop instead of the interrupt: an INT edge is only latched
 * in the GPIO status register (no IRQ) and tells when to read; while a finger is down, or when no edge
 * has ever been seen, the report register is also polled on a timer.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/display.h"
#include "platform/time.h"
#include "platform/touch.h"

#define ADDR_PRIMARY    0x14
#define ADDR_ALTERNATE  0x5d            /* INT low during reset */

#define REG_PRODUCT_ID  0x8140          /* 4 ASCII bytes, "911\0" */
#define REG_RESOLUTION  0x8048          /* x max, y max (configuration) */
#define REG_SWITCH1     0x804d          /* bit 3 = x / y swapped, bits 0-1 = INT trigger */
#define REG_STATUS      0x814e

#define RST_GPIO        GPIO1
#define RST_PIN         3
#define INT_GPIO        GPIO3
#define INT_PIN         3
#define INT_MUX         0x401f81e0u     /* SW_MUX_CTL_PAD_GPIO_SD_B1_03 */
#define INT_PAD         0x401f83d0u     /* SW_PAD_CTL_PAD_GPIO_SD_B1_03 */

#define GPIO_ICR1       0x0c
#define GPIO_ISR        0x18
#define GPIO_EDGE_SEL   0x1c

#define POLL_DOWN_MS    10              /* while pressed: the GT911 reports about every 10 ms */
#define POLL_IDLE_MS    100             /* fallback when the INT line stays quiet */
#define RELEASE_MS      300             /* pressed but no report for this long: treat as released */

/* Orientation of the reported coordinates; the original uses them as they come. */
#define SWAP_XY         0
#define MIRROR_X        0
#define MIRROR_Y        0

static const struct i2c_bus touch_bus = { .scl_gpio = GPIO3, .scl = 4, .sda_gpio = GPIO3, .sda = 5, .half_us = 2 };

static struct {
    uint8_t addr;                       /* 0 = no controller */
    uint8_t edges_seen;
    uint16_t res_x, res_y;
    uint32_t polled_at, report_at;
    struct touch_point point;
    char info[40];
} t;

static int read_reg(uint16_t reg, uint8_t *data, unsigned n)
{
    const uint8_t cmd[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return i2c_read(&touch_bus, t.addr, cmd, 2, data, n);
}

static int write_reg8(uint16_t reg, uint8_t value)
{
    const uint8_t msg[3] = { (uint8_t)(reg >> 8), (uint8_t)reg, value };
    return i2c_write(&touch_bus, t.addr, msg, 3);
}

static void make_info(const uint8_t id[4])
{
    struct fmt f;
    char name[5] = { 0 };
    for (int i = 0; i < 4 && id[i] >= '0' && id[i] <= 'z'; i++)
        name[i] = (char)id[i];
    fmt_init(&f, t.info, sizeof t.info);
    fmt_str(&f, "GT");
    fmt_str(&f, name);
    fmt_str(&f, " 0x");
    fmt_hex(&f, t.addr, 2);
    fmt_str(&f, " ");
    fmt_dec(&f, t.res_x);
    fmt_str(&f, "x");
    fmt_dec(&f, t.res_y);
}

void touch_init(void)
{
    gpio_init(RST_GPIO, RST_PIN, 1, 0);
    time_delay_ms(1);
    gpio_write(RST_GPIO, RST_PIN, 1);
    time_delay_ms(70);

    i2c_bus_clear(&touch_bus);
    static const uint8_t addrs[2] = { ADDR_PRIMARY, ADDR_ALTERNATE };
    uint8_t id[4] = { 0 };
    for (int i = 0; i < 2 && !t.addr; i++) {
        t.addr = addrs[i];
        if (!read_reg(REG_PRODUCT_ID, id, 4))
            t.addr = 0;
    }
    if (!t.addr) {
        struct fmt f;
        fmt_init(&f, t.info, sizeof t.info);
        fmt_str(&f, "no touch controller");
        return;
    }
    uint8_t res[4];
    if (read_reg(REG_RESOLUTION, res, 4)) {
        t.res_x = (uint16_t)(res[0] | res[1] << 8);
        t.res_y = (uint16_t)(res[2] | res[3] << 8);
    }
    make_info(id);

    /* INT as a plain GPIO input with pull-up, any edge latched in ISR, no interrupt */
    R32(INT_MUX) = 5;
    R32(INT_PAD) = 0x1b008u;
    gpio_init(INT_GPIO, INT_PIN, 0, 0);
    R32(INT_GPIO + GPIO_EDGE_SEL) |= 1u << INT_PIN;
    R32(INT_GPIO + GPIO_ISR) = 1u << INT_PIN;
    write_reg8(REG_STATUS, 0);                  /* drop a report left from the reset */
}

int touch_ready(void)
{
    return t.addr != 0;
}

const char *touch_info(void)
{
    return t.info;
}

static int scale(int v, int from, int to)
{
    if (from <= 0 || from == to)
        return v;
    return v * to / from;
}

static void set_point(int rx, int ry)
{
    int rw = t.res_x, rh = t.res_y;
    if (SWAP_XY) {
        int tmp = rx;
        rx = ry;
        ry = tmp;
        tmp = rw;
        rw = rh;
        rh = tmp;
    }
    int x = scale(rx, rw, DISPLAY_W), y = scale(ry, rh, DISPLAY_H);
    if (MIRROR_X)
        x = DISPLAY_W - 1 - x;
    if (MIRROR_Y)
        y = DISPLAY_H - 1 - y;
    t.point.x = (int16_t)(x < 0 ? 0 : x >= DISPLAY_W ? DISPLAY_W - 1 : x);
    t.point.y = (int16_t)(y < 0 ? 0 : y >= DISPLAY_H ? DISPLAY_H - 1 : y);
}

static void poll(uint32_t now)
{
    uint8_t r[6];
    t.polled_at = now;
    if (!read_reg(REG_STATUS, r, 6) || !(r[0] & 0x80u))
        return;
    t.report_at = now;
    if (r[0] & 0x0fu) {
        t.point.pressed = 1;
        set_point(r[2] | r[3] << 8, r[4] | r[5] << 8);
    } else {
        t.point.pressed = 0;
    }
    write_reg8(REG_STATUS, 0);
}

void touch_read(struct touch_point *out)
{
    if (t.addr) {
        uint32_t now = time_ms();
        int edge = (R32(INT_GPIO + GPIO_ISR) >> INT_PIN) & 1u;
        if (edge) {
            R32(INT_GPIO + GPIO_ISR) = 1u << INT_PIN;
            t.edges_seen = 1;
        }
        uint32_t since = now - t.polled_at;
        if (edge || (t.point.pressed && since >= POLL_DOWN_MS) || (!t.edges_seen && since >= POLL_IDLE_MS))
            poll(now);
        if (t.point.pressed && now - t.report_at >= RELEASE_MS)
            t.point.pressed = 0;
    }
    *out = t.point;
}
