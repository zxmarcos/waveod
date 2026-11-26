/* Pin mux and GPIO directions of the original BOARD_InitPins, replayed from board_v122.h. */
#include "board/qme200/board.h"
#include "board/qme200/board_v122.h"
#include "board/qme200/regs.h"

void gpio_write(uint32_t gpio, int pin, int value)
{
    if (value)
        R32(gpio) |= 1u << pin;
    else
        R32(gpio) &= ~(1u << pin);
}

void gpio_init(uint32_t gpio, int pin, int output, int value)
{
    R32(gpio + 0x14) &= ~(1u << pin);           /* IMR: no interrupt */
    if (output) {
        gpio_write(gpio, pin, value);
        R32(gpio + 4) |= 1u << pin;
    } else {
        R32(gpio + 4) &= ~(1u << pin);
    }
}

void pins_init(void)
{
    for (unsigned i = 0; i < sizeof iomux_v122 / sizeof iomux_v122[0]; i++)
        R32(iomux_v122[i][0]) = iomux_v122[i][1];
    for (unsigned i = 0; i < sizeof gpio_v122 / sizeof gpio_v122[0]; i++)
        gpio_init(gpio_v122[i][0], (int)gpio_v122[i][1], (int)gpio_v122[i][2], (int)gpio_v122[i][3]);
    /* panel SPI + reset: the original app relies on the bootloader having made these outputs */
    gpio_init(GPIO2, 30, 1, 1);                 /* CS */
    gpio_init(GPIO2, 29, 1, 0);                 /* SCL */
    gpio_init(GPIO2, 28, 1, 0);                 /* SDA */
    gpio_init(GPIO2, 31, 1, 1);                 /* reset, active low */
}
