/*
 * Audio I/O of the QME-200, reproduced from the original start-up (FUN_0001B780 / FUN_000311DC):
 *   - three ES7243E stereo ADCs on the software I2C bus (0x11 is the clock master, 0x10 / 0x12 slaves);
 *   - two ES8156 stereo DACs on the same bus (0x09 main L / R, 0x08 FX send), left in hardware mode;
 *   - SAI1 as a clock slave, I2S 32-bit, RX on three data lines and TX on two, FIFO combine mode (the
 *     transmitter started by sai_tx_start instead of the original's last writes);
 *   - eDMA channel 0 <- RX (6 words per frame), channel 16 -> TX (4 words per frame), ping-pong buffers of
 *     2 x 64 frames with half / major loop interrupts (IRQ 0), processed in the interrupt.
 * Buffers live in the first 16 KB of the OCRAM, which the MPU keeps non-cacheable.
 */
#include "board/qme200/board.h"
#include "board/qme200/board_v122.h"
#include "board/qme200/regs.h"
#include "platform/audio.h"
#include "platform/time.h"

#define RX_LEN (2 * AUDIO_FRAMES * AUDIO_IN_WORDS)
#define TX_LEN (2 * AUDIO_FRAMES * AUDIO_OUT_WORDS)

__attribute__((section(".dma_buffers"), aligned(32))) static int32_t rx_buf[RX_LEN];
__attribute__((section(".dma_buffers"), aligned(32))) static int32_t tx_buf[TX_LEN];

static const struct i2c_bus codec_bus = { .scl_gpio = GPIO1, .scl = 25, .sda_gpio = GPIO1, .sda = 10, .half_us = 5 };

static audio_callback callback;
static volatile uint32_t blocks, late;

int audio_streaming(void) { return callback != 0; }
uint32_t audio_blocks(void) { return blocks; }
uint32_t audio_late_blocks(void) { return late; }

/* a crash (safety.c): from now on the DMA streams zeros to the outputs */
void audio_silence(void)
{
    callback = 0;
    for (int i = 0; i < TX_LEN; i++)
        tx_buf[i] = 0;
}

void dma0_dma16_irq(void)
{
    uint32_t flags = R32(DMA0_INT);
    R8(DMA0_CINT) = 16;
    if (!(flags & 1u))
        return;
    R8(DMA0_CINT) = 0;
    /* CITER counts down: still in the first half means the second half has just been filled */
    int half = R16(DMA_TCD(0) + 0x16) > AUDIO_FRAMES * AUDIO_IN_WORDS ? 1 : 0;
    const int32_t *in = rx_buf + half * AUDIO_FRAMES * AUDIO_IN_WORDS;
    int32_t *out = tx_buf + half * AUDIO_FRAMES * AUDIO_OUT_WORDS;
    if (callback)
        callback(in, out, AUDIO_FRAMES);
    blocks++;
    if (R32(DMA0_INT) & 1u)                     /* the next half completed while processing */
        late++;
}

static void dma_setup(void)
{
    R32(DMA0_ERQ) = 0;
    /* minor loop mapping + halt on error; keeps GRP1PRI = 1 (writing 0x82 alone gives both channel groups
       the same priority and the eDMA refuses every transfer, seen on the pedal) */
    R32(DMA0_CR) = 0x482u;
    R8(DMA0_CR + 0x1e) = 0x40u;                 /* CERR: clear all errors */
    R32(DMA0_INT) = 0xffffffffu;
    R32(DMA0_ERR) = 0xffffffffu;
    for (int i = 0; i < RX_LEN; i++)
        rx_buf[i] = 0;
    for (int i = 0; i < TX_LEN; i++)
        tx_buf[i] = 0;

    R32(DMAMUX(0)) = 0x13u;                     /* SAI1 RX request */
    R32(DMAMUX(0)) = 0x80000013u;
    uint32_t t = DMA_TCD(0);
    R32(t + 0x00) = SAI1_RDR0;
    R16(t + 0x04) = 0;
    R16(t + 0x06) = 0x0202u;                    /* 32-bit transfers */
    R32(t + 0x08) = 4;
    R32(t + 0x0c) = 0;
    R32(t + 0x10) = (uint32_t)(uintptr_t)rx_buf;
    R16(t + 0x14) = 4;
    R16(t + 0x16) = RX_LEN;
    R32(t + 0x18) = (uint32_t)-(RX_LEN * 4);
    R16(t + 0x1c) = 6;                          /* interrupt at half and at the end */
    R16(t + 0x1e) = RX_LEN;
    R8(DMA0_SERQ) = 0;

    R32(DMAMUX(16)) = 0x14u;                    /* SAI1 TX request */
    R32(DMAMUX(16)) = 0x80000014u;
    t = DMA_TCD(16);
    R32(t + 0x00) = (uint32_t)(uintptr_t)tx_buf;
    R16(t + 0x04) = 4;
    R16(t + 0x06) = 0x0202u;
    R32(t + 0x08) = 4;
    R32(t + 0x0c) = (uint32_t)-(TX_LEN * 4);
    R32(t + 0x10) = SAI1_TDR0;
    R16(t + 0x14) = 0;
    R16(t + 0x16) = TX_LEN;
    R32(t + 0x18) = 0;
    R16(t + 0x1c) = 6;
    R16(t + 0x1e) = TX_LEN;
    R8(DMA0_SERQ) = 16;
}

static void codec_write(uint8_t addr, uint8_t reg, uint8_t value)
{
    const uint8_t msg[2] = { reg, value };
    for (int attempt = 0; attempt < 3; attempt++) {
        if (i2c_write(&codec_bus, addr, msg, 2))
            return;
        time_delay_ms(2);
    }
}

void audio_set_input_mode(int mode)
{
    /* virtual pins 0x19 / 0x1A (byte 3, bits 1 and 2) = inverted bits 0 and 1 of the mode */
    uint8_t b = (uint8_t)(shift_reg_out[3] & ~6u);
    if (!(mode & 1))
        b |= 2u;
    if (!(mode & 2))
        b |= 4u;
    shift_reg_out[3] = b;
}

#define SAI_ENABLED     (1u << 31)              /* TCSR: the transmitter runs */
#define SAI_FIFO_RESET  (1u << 25)
#define SAI_FLAGS       (7u << 18)              /* word start, sync error, FIFO error: written 1 to clear */
#define SAI_DMA         1u                      /* FIFO request to the DMA */

/*
 * The transmitter's start. FIFO combine hands the words the DMA writes to the two transmit FIFOs in turn (main
 * L, FX send, main R, unused). The original's table enables the DMA request and then resets the FIFO again,
 * three times: the words the DMA has put in by then are dropped, and an odd number of them puts every word on
 * the other line. As the pedal is switched on none happens to be dropped, but a second run of the table on the
 * running pedal gave main L the FX send's word every time (the converter clock test, 2026-11-22). So the table's
 * writes to the transmit control register go out without the enable and without the DMA request, and here the
 * FIFO is reset for the last time before the DMA may write, then filled, and only then the transmitter runs:
 * ten starts in a row on the pedal, each with every word on its own output and the same delay as before.
 */
static void sai_tx_start(void)
{
    R32(SAI1_TCSR) = SAI_FIFO_RESET;
    R32(SAI1_TCSR) = SAI_DMA;
    time_delay_ms(1);
    R32(SAI1_TCSR) = SAI_ENABLED | SAI_DMA;
    time_delay_ms(1);
    R32(SAI1_TCSR) = SAI_ENABLED | SAI_DMA | SAI_FLAGS;     /* an underrun at the start, if any: go on from it */
}

int audio_start(audio_callback cb)
{
    callback = cb;
    dma_setup();
    i2c_bus_clear(&codec_bus);
    for (unsigned i = 0; i < sizeof audio_v122 / sizeof audio_v122[0]; i++) {
        const uint32_t *op = audio_v122[i];
        if (op[0] == AUDIO_I2C) {
            codec_write((uint8_t)op[1], (uint8_t)op[2], (uint8_t)op[3]);
            if (op[2] == 0 && (op[3] & 0x80u))
                time_delay_ms(5);               /* after a reset write */
        } else if (op[1] == SAI1_TCSR) {
            R32(op[1]) = op[2] & ~(SAI_ENABLED | SAI_DMA);  /* the transmitter starts in sai_tx_start */
        } else {
            R32(op[1]) = op[2];
        }
    }
    sai_tx_start();
    /* running state of the shift register outputs; byte 3 bits 1-2 = the input mode (audio_set_input_mode) */
    for (int i = 0; i < 4; i++)
        shift_reg_out[i] = shift_final_v122[i];
    audio_set_input_mode(0);
    R8(NVIC_IPR + 0) = 0x20;                    /* below the USB (usb_dcd.c) and MIDI (midi_hw.c) interrupts */
    R32(NVIC_ISER) = 1u;                        /* IRQ 0: eDMA channels 0 / 16 */
    return 0;
}
