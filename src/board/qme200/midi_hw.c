/*
 * MIDI hardware of the QME-200 (platform/midi_hw.h): the TRS jacks on LPUART1 (pins GPIO_AD_B0_12 / 13,
 * ALT2, set up with the others in pins.c) and the MIDI clock on GPT2.
 *
 * LPUART1 as the original sets it (mmio trace of FUN_000311DC): 32x oversampling on the 80 MHz UART clock
 * (SBR 80 = 31250 baud), FIFOs on. The rings below take the place of the SDK transfer handle.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/midi_hw.h"

#define CCM_CSCDR1      (CCM + 0x24)

#define UART            0x40184000u         /* LPUART1 */
#define UART_GLOBAL     (UART + 0x08)
#define UART_BAUD       (UART + 0x10)
#define UART_STAT       (UART + 0x14)
#define UART_CTRL       (UART + 0x18)
#define UART_DATA       (UART + 0x1c)
#define UART_FIFO       (UART + 0x28)
#define UART_WATER      (UART + 0x2c)
#define UART_IRQ        20
#define STAT_ERRORS     0x000f0000u         /* OR, NF, FE, PF (write 1 to clear) */
#define CTRL_RE         (1u << 18)
#define CTRL_TE         (1u << 19)
#define CTRL_RIE        (1u << 21)
#define CTRL_TIE        (1u << 23)
#define FIFO_DEPTH      4

#define GPT2            0x401f0000u
#define GPT2_CR         (GPT2 + 0x00)
#define GPT2_PR         (GPT2 + 0x04)
#define GPT2_SR         (GPT2 + 0x08)
#define GPT2_IR         (GPT2 + 0x0c)
#define GPT2_OCR1       (GPT2 + 0x10)
#define GPT2_CNT        (GPT2 + 0x24)
#define GPT2_IRQ        101
#define TICKS_PER_US    24u

#define IRQ_PRIORITY    0x10                /* below USB (0x00), above audio (0x20) */

#define RX_RING 512                         /* powers of two */
#define TX_RING 512

static struct {
    uint8_t rx[RX_RING];
    volatile uint32_t rx_head, rx_tail;     /* head: interrupt, tail: main loop */
    uint8_t tx[TX_RING];
    volatile uint32_t tx_head, tx_tail;     /* head: main loop, tail: interrupt */
    volatile uint32_t clocks;               /* timing clocks waiting (clock and UART interrupts only) */
    uint32_t (*tick)(void);
} mh;

static void irq_enable(int n)
{
    R8(NVIC_IPR + n) = IRQ_PRIORITY;
    R32(NVIC_ICPR + 4 * (n / 32)) = 1u << (n % 32);
    R32(NVIC_ISER + 4 * (n / 32)) = 1u << (n % 32);
}

/* the UART clock root: 80 MHz (PLL3 / 6) or the 24 MHz crystal, then its divider (CSCDR1) */
static uint32_t uart_clock(void)
{
    uint32_t cscdr1 = R32(CCM_CSCDR1);
    return ((cscdr1 & 0x40u) ? 24000000u : 80000000u) / ((cscdr1 & 0x3fu) + 1u);
}

void midi_hw_init(void)
{
    R32(UART_GLOBAL) = 2;                   /* software reset */
    R32(UART_GLOBAL) = 0;
    uint32_t sbr = (uart_clock() + 31250u * 16u) / (31250u * 32u);
    R32(UART_BAUD) = 31u << 24 | (sbr & 0x1fffu);          /* OSR 32 */
    R32(UART_CTRL) = 0;                     /* 8 data bits, no parity */
    R32(UART_FIFO) = 0xc088u;               /* both FIFOs on and flushed */
    R32(UART_WATER) = 0;                    /* receive interrupt at the first byte, transmit when empty */
    R32(UART_STAT) = STAT_ERRORS;
    R32(UART_CTRL) = CTRL_TE | CTRL_RE | CTRL_RIE;
    irq_enable(UART_IRQ);
}

static void fill_tx(void)
{
    for (int n = 0; n < FIFO_DEPTH && ((R32(UART_WATER) >> 8) & 7u) < FIFO_DEPTH; n++) {
        if (mh.clocks) {
            mh.clocks--;
            R32(UART_DATA) = 0xf8u;
        } else if (mh.tx_tail != mh.tx_head) {
            R32(UART_DATA) = mh.tx[mh.tx_tail & (TX_RING - 1)];
            mh.tx_tail++;
        } else {
            R32(UART_CTRL) &= ~CTRL_TIE;
            return;
        }
    }
}

void lpuart1_irq(void)
{
    uint32_t stat = R32(UART_STAT);
    if (stat & STAT_ERRORS)
        R32(UART_STAT) = stat & STAT_ERRORS;    /* an overrun stops the receiver until cleared */
    uint32_t head = mh.rx_head;
    for (int n = 0; n < FIFO_DEPTH && ((R32(UART_WATER) >> 24) & 7u); n++) {
        uint8_t b = (uint8_t)R32(UART_DATA);
        if (head - mh.rx_tail < RX_RING)
            mh.rx[head++ & (RX_RING - 1)] = b;
    }
    mh.rx_head = head;
    if (R32(UART_CTRL) & CTRL_TIE)
        fill_tx();
}

int midi_uart_read(uint8_t *buf, int max)
{
    int n = 0;
    uint32_t tail = mh.rx_tail;
    while (n < max && tail != mh.rx_head)
        buf[n++] = mh.rx[tail++ & (RX_RING - 1)];
    mh.rx_tail = tail;
    return n;
}

int midi_uart_write(const uint8_t *msg, int len)
{
    if (len <= 0 || (uint32_t)len > TX_RING - (mh.tx_head - mh.tx_tail))
        return -1;
    uint32_t head = mh.tx_head;
    for (int i = 0; i < len; i++)
        mh.tx[head++ & (TX_RING - 1)] = msg[i];
    mh.tx_head = head;
    R32(UART_CTRL) |= CTRL_TIE;             /* the interrupt only ever clears TIE: no lock needed */
    return 0;
}

void midi_uart_clock(void)
{
    mh.clocks++;
    R32(UART_CTRL) |= CTRL_TIE;
}

/* ---------------------------------------------------------------- clock timer */

void midi_timer_start(uint32_t first_us, uint32_t (*fn)(void))
{
    mh.tick = fn;
    R32(GPT2_CR) = 0;
    R32(GPT2_CR) = 1u << 15;                /* software reset */
    for (int i = 0; i < 1000 && (R32(GPT2_CR) & (1u << 15)); i++) {}
    R32(GPT2_PR) = 0;                       /* the 24 MHz crystal, not divided */
    R32(GPT2_CR) = (1u << 10) | (5u << 6) | (1u << 9) | (1u << 1);  /* EN_24M, CLKSRC = crystal, free run, ENMOD */
    R32(GPT2_OCR1) = first_us * TICKS_PER_US;
    R32(GPT2_SR) = 0x3fu;
    R32(GPT2_IR) = 1u;                      /* output compare 1 */
    R32(GPT2_CR) |= 1u;
    irq_enable(GPT2_IRQ);
}

/* compares are spaced on the free-running counter, so the interrupt's latency does not add up */
void gpt2_irq(void)
{
    R32(GPT2_SR) = 1u;
    uint32_t us = mh.tick ? mh.tick() : 0;
    R32(GPT2_OCR1) += (us ? us : 1000u) * TICKS_PER_US;
}
