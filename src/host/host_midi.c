/*
 * Host build: the TRS port as two byte queues the script fills and reads (host_midi_in / host_midi_out), the
 * clock timer as a function the script calls (host_midi_clock_tick).
 */
#include "host/host.h"
#include "platform/midi_hw.h"
#include <string.h>

#define RING 1024

static struct {
    uint8_t rx[RING], tx[RING];
    uint32_t rx_head, rx_tail, tx_len;
    uint32_t (*tick)(void);
} hm;

void midi_hw_init(void)
{
    memset(&hm, 0, sizeof hm);
}

int midi_uart_read(uint8_t *buf, int max)
{
    int n = 0;
    while (n < max && hm.rx_tail != hm.rx_head)
        buf[n++] = hm.rx[hm.rx_tail++ % RING];
    return n;
}

int midi_uart_write(const uint8_t *msg, int len)
{
    if (len <= 0 || hm.tx_len + (uint32_t)len > RING)
        return -1;
    memcpy(hm.tx + hm.tx_len, msg, (size_t)len);
    hm.tx_len += (uint32_t)len;
    return 0;
}

void midi_uart_clock(void)
{
    uint8_t b = 0xf8;
    midi_uart_write(&b, 1);
}

void midi_timer_start(uint32_t first_us, uint32_t (*fn)(void))
{
    (void)first_us;
    hm.tick = fn;
}

/* ---------------------------------------------------------------- the script's side */

void host_midi_in(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
        hm.rx[hm.rx_head++ % RING] = data[i];
}

int host_midi_out(uint8_t *buf, int max)
{
    int n = (int)hm.tx_len < max ? (int)hm.tx_len : max;
    memcpy(buf, hm.tx, (size_t)n);
    memmove(hm.tx, hm.tx + n, hm.tx_len - (uint32_t)n);
    hm.tx_len -= (uint32_t)n;
    return n;
}

uint32_t host_midi_clock_tick(void)
{
    return hm.tick ? hm.tick() : 0;
}
