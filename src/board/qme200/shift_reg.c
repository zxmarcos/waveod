/*
 * Button / LED shift register chain (4 bytes), bit-banged like the original (FUN_00035750):
 * clock GPIO1[0], data out GPIO1[1], data in GPIO1[2], parallel load GPIO2[23], output latch GPIO2[24].
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/time.h"

/* Outputs (LEDs, analog switches). Starts at the boot state; audio.c switches to the running state. */
uint8_t shift_reg_out[4];

void shift_reg_xfer(const uint8_t out[4], uint8_t in[4])
{
    gpio_write(GPIO2, 23, 0);
    time_delay_us(1);
    gpio_write(GPIO2, 23, 1);
    time_delay_us(1);
    for (int i = 0; i < 4; i++) {
        uint8_t r = 0;
        for (int k = 7; k >= 0; k--) {
            gpio_write(GPIO1, 1, (out[i] >> k) & 1);
            time_delay_us(1);
            r |= (uint8_t)(((R32(GPIO1) >> 2) & 1u) << k);  /* DR reads the pad of an input pin */
            gpio_write(GPIO1, 0, 1);
            time_delay_us(1);
            gpio_write(GPIO1, 0, 0);
            time_delay_us(1);
        }
        if (in)
            in[i] = r;
    }
    gpio_write(GPIO2, 24, 1);
    time_delay_us(1);
    gpio_write(GPIO2, 24, 0);
}
