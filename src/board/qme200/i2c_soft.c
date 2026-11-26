/*
 * Bit-banged I2C master, as the original firmware does it on its two buses (the codecs on GPIO1[25] /
 * GPIO1[10], the touch controller on GPIO3[4] / GPIO3[5]): both lines push-pull with the pad pull-ups,
 * SDA switched to input to read the acknowledge and the data bits.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/time.h"

static void wait(const struct i2c_bus *b)
{
    time_delay_us(b->half_us);
}

static void scl(const struct i2c_bus *b, int level)
{
    gpio_write(b->scl_gpio, b->scl, level);
}

static void sda(const struct i2c_bus *b, int level)
{
    gpio_write(b->sda_gpio, b->sda, level);
}

static void sda_output(const struct i2c_bus *b, int out)
{
    if (out)
        R32(b->sda_gpio + 4) |= 1u << b->sda;
    else
        R32(b->sda_gpio + 4) &= ~(1u << b->sda);
}

static int sda_level(const struct i2c_bus *b)
{
    return (R32(b->sda_gpio + 8) >> b->sda) & 1u;
}

/* START, or repeated START when the bus is already ours */
static void start(const struct i2c_bus *b)
{
    sda_output(b, 1);
    sda(b, 1);
    wait(b);
    scl(b, 1);
    wait(b);
    sda(b, 0);
    wait(b);
    scl(b, 0);
    wait(b);
}

static void stop(const struct i2c_bus *b)
{
    sda_output(b, 1);
    sda(b, 0);
    wait(b);
    scl(b, 1);
    wait(b);
    sda(b, 1);
    wait(b);
}

/* returns 1 when the device acknowledged */
static int write_byte(const struct i2c_bus *b, uint8_t v)
{
    for (int k = 7; k >= 0; k--) {
        sda(b, (v >> k) & 1);
        wait(b);
        scl(b, 1);
        wait(b);
        scl(b, 0);
    }
    sda_output(b, 0);
    wait(b);
    scl(b, 1);
    wait(b);
    int ack = !sda_level(b);
    scl(b, 0);
    sda_output(b, 1);
    return ack;
}

static uint8_t read_byte(const struct i2c_bus *b, int ack)
{
    uint8_t v = 0;
    sda_output(b, 0);
    for (int k = 0; k < 8; k++) {
        wait(b);
        scl(b, 1);
        wait(b);
        v = (uint8_t)(v << 1 | sda_level(b));
        scl(b, 0);
    }
    sda(b, !ack);
    sda_output(b, 1);
    wait(b);
    scl(b, 1);
    wait(b);
    scl(b, 0);
    return v;
}

int i2c_write(const struct i2c_bus *b, uint8_t addr7, const uint8_t *data, unsigned n)
{
    start(b);
    int ok = write_byte(b, (uint8_t)(addr7 << 1));
    for (unsigned i = 0; ok && i < n; i++)
        ok = write_byte(b, data[i]);
    stop(b);
    return ok;
}

int i2c_read(const struct i2c_bus *b, uint8_t addr7, const uint8_t *cmd, unsigned ncmd, uint8_t *data,
             unsigned n)
{
    start(b);
    int ok = write_byte(b, (uint8_t)(addr7 << 1));
    for (unsigned i = 0; ok && i < ncmd; i++)
        ok = write_byte(b, cmd[i]);
    if (ok) {
        start(b);
        ok = write_byte(b, (uint8_t)(addr7 << 1 | 1));
    }
    for (unsigned i = 0; ok && i < n; i++)
        data[i] = read_byte(b, i + 1 < n);
    stop(b);
    return ok;
}

/* free a slave left in the middle of a byte: 9 clocks with SDA high, then STOP */
void i2c_bus_clear(const struct i2c_bus *b)
{
    sda_output(b, 1);
    sda(b, 1);
    for (int i = 0; i < 9; i++) {
        scl(b, 0);
        wait(b);
        scl(b, 1);
        wait(b);
    }
    stop(b);
}
