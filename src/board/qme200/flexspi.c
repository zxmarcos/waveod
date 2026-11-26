/*
 * FlexSPI IP commands to the QSPI NOR flash (a 25-series part: the original app's LUT, FUN_00026714, uses
 * 0x06 write enable, 0x05 read status, 0x20 4 KB erase, 0x02 / 0x32 page program, 0x9F-style ids).
 *
 * The bootloader's FlexSPI set-up is kept as it is (it serves the memory-mapped reads); only our LUT
 * sequences are added, in slots its AHB read / write sequences do not use. Everything here runs from ITCM
 * and nothing may read the memory map while a command or the flash itself is busy.
 *
 * Every failure is recorded (step, address, interrupt / status registers) for flexspi_diag.
 */
#include "board/qme200/board.h"
#include "board/qme200/regs.h"
#include "platform/cpu.h"
#include "platform/flash.h"
#include "platform/time.h"

#define FLEXSPI      0x402a8000u
#define MCR0         (FLEXSPI + 0x000)
#define MCR2         (FLEXSPI + 0x008)
#define AHBCR        (FLEXSPI + 0x00c)
#define INTR         (FLEXSPI + 0x014)
#define LUTKEY       (FLEXSPI + 0x018)
#define LUTCR        (FLEXSPI + 0x01c)
#define FLSHA1CR0    (FLEXSPI + 0x060)
#define FLSHA2CR0    (FLEXSPI + 0x064)
#define FLSHB1CR0    (FLEXSPI + 0x068)
#define FLSHA1CR1    (FLEXSPI + 0x070)
#define FLSHA1CR2    (FLEXSPI + 0x080)
#define IPCR0        (FLEXSPI + 0x0a0)
#define IPCR1        (FLEXSPI + 0x0a4)
#define IPCMD        (FLEXSPI + 0x0b0)
#define IPRXFCR      (FLEXSPI + 0x0b8)
#define IPTXFCR      (FLEXSPI + 0x0bc)
#define STS0         (FLEXSPI + 0x0e0)
#define STS1         (FLEXSPI + 0x0e4)
#define IPRXFSTS     (FLEXSPI + 0x0f0)
#define RFDR(n)      (FLEXSPI + 0x100 + 4 * (n))
#define TFDR(n)      (FLEXSPI + 0x180 + 4 * (n))
#define LUT(n)       (FLEXSPI + 0x200 + 4 * (n))

#define INTR_IPCMDDONE  (1u << 0)
#define INTR_IPCMDGE    (1u << 1)
#define INTR_IPCMDERR   (1u << 3)
#define INTR_IPRXWA     (1u << 5)
#define INTR_IPTXWE     (1u << 6)
/* status flags only: writing IPTXWE / IPRXWA is not a clear, it pushes / pops a FIFO watermark */
#define INTR_CLEAR      (INTR_IPCMDDONE | INTR_IPCMDGE | INTR_IPCMDERR)

/* LUT instruction: opcode, pads (0 = 1 line), operand; two per word, the first in the low half */
#define INSTR(op, operand)  ((uint32_t)(op) << 10 | (uint32_t)(operand))
#define OP_CMD    0x01
#define OP_ADDR   0x02
#define OP_WRITE  0x08
#define OP_READ   0x09

enum { SEQ_WREN, SEQ_RDSR, SEQ_ERASE, SEQ_PROGRAM, SEQ_RDID, SEQ_COUNT };

static const uint32_t sequences[SEQ_COUNT][2] = {
    [SEQ_WREN]    = { INSTR(OP_CMD, 0x06), 0 },
    [SEQ_RDSR]    = { INSTR(OP_CMD, 0x05) | INSTR(OP_READ, 0x04) << 16, 0 },
    [SEQ_ERASE]   = { INSTR(OP_CMD, 0x20) | INSTR(OP_ADDR, 24) << 16, 0 },
    [SEQ_PROGRAM] = { INSTR(OP_CMD, 0x02) | INSTR(OP_ADDR, 24) << 16, INSTR(OP_WRITE, 0x04) },
    [SEQ_RDID]    = { INSTR(OP_CMD, 0x9f) | INSTR(OP_READ, 0x04) << 16, 0 },
};

#define CMD_TIMEOUT_MS    20
#define ERASE_TIMEOUT_MS  1000              /* 4 KB erase: 45 ms typical, 400 ms max on 25-series parts */
#define PROGRAM_TIMEOUT_MS 20
#define TX_FIFO           128               /* IP TX FIFO of the RT1062 FlexSPI, bytes */

static int seq_base = -1;                   /* first LUT slot of our sequences, -1 = writing unavailable */

/* diagnostics */
static uint32_t jedec_id;
/* status registers 1-3: as the chip came up (read in flexspi_init, before anything wrote them) and as last read
   (the lock reads them again); -1 = not read */
static int boot_sr[3] = { -1, -1, -1 }, now_sr[3] = { -1, -1, -1 };
static struct {
    const char *step;                       /* 0 = no error so far */
    int seq;
    uint32_t addr, intr, sts0, sts1;
} fail;
static uint32_t commands, failures;

static void record(const char *step, int seq, uint32_t addr)
{
    failures++;
    fail.step = step;
    fail.seq = seq;
    fail.addr = addr;
    fail.intr = R32(INTR);
    fail.sts0 = R32(STS0);
    fail.sts1 = R32(STS1);
}

static int overlaps(int base, int seq, int num)
{
    return seq < base + SEQ_COUNT && seq + num > base;
}

static int wait_bits(uint32_t reg, uint32_t mask, uint32_t want, uint32_t timeout_ms)
{
    uint32_t t0 = time_ms();
    while ((R32(reg) & mask) != want)
        if (time_ms() - t0 > timeout_ms)
            return -1;
    return 0;
}

/* one IP command: sequence `seq`, flash address, optional data out (up to TX_FIFO bytes) or in (up to 8).
   Data going out is loaded into the TX FIFO before the command starts: a FIFO that runs dry in the middle
   of a sequence ends it with a sequence timeout (STS1 code 0xE, seen on the pedal when the data was fed
   while the command ran). */
static int ip_command(int seq, uint32_t addr, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    commands++;
    if (tx && len > TX_FIFO)
        return -1;
    /* take the data first: it may come from the memory map, and a memory-mapped read starts an AHB
       prefetch that must be over before the command */
    uint8_t out[TX_FIFO];
    for (uint32_t i = 0; tx && i < TX_FIFO; i++)
        out[i] = i < len ? tx[i] : 0xff;
    __asm volatile("dsb" ::: "memory");
    if (wait_bits(STS0, 3u, 3u, CMD_TIMEOUT_MS)) {       /* sequence engine and arbitrator idle */
        record("ocupado", seq, addr);
        return -1;
    }
    R32(IPTXFCR) = 1u | (TX_FIFO / 8 - 1) << 2;         /* clear; watermark = the whole FIFO */
    R32(IPRXFCR) = 1;
    R32(INTR) = INTR_CLEAR;
    if (tx) {
        if (wait_bits(INTR, INTR_IPTXWE, INTR_IPTXWE, CMD_TIMEOUT_MS)) {
            record("fifo tx", seq, addr);
            return -1;
        }
        for (uint32_t w = 0; w < TX_FIFO / 4; w++)
            R32(TFDR(w)) = (uint32_t)out[4 * w] | out[4 * w + 1] << 8 | out[4 * w + 2] << 16 |
                           (uint32_t)out[4 * w + 3] << 24;
        R32(INTR) = INTR_IPTXWE;                        /* the FIFO takes the watermark's worth */
    }
    if (wait_bits(STS0, 3u, 3u, CMD_TIMEOUT_MS)) {       /* still idle right before the start */
        record("ocupado", seq, addr);
        return -1;
    }
    R32(IPCR0) = addr;
    R32(IPCR1) = (len & 0xffffu) | (uint32_t)(seq_base + seq) << 16;
    R32(IPCMD) = 1;    uint32_t t0 = time_ms();
    while (!(R32(INTR) & (INTR_IPCMDDONE | INTR_IPCMDERR)))
        if (time_ms() - t0 > CMD_TIMEOUT_MS) {
            record("sem fim", seq, addr);
            return -1;
        }
    if (R32(INTR) & INTR_IPCMDERR) {
        record("erro cmd", seq, addr);
        R32(INTR) = INTR_CLEAR;
        return -1;
    }
    if (rx) {
        /* the data is in the RX FIFO once the command is done; wait for the fill level all the same */
        uint32_t t1 = time_ms();
        while ((R32(IPRXFSTS) & 0xffu) < (len + 7) / 8)
            if (time_ms() - t1 > CMD_TIMEOUT_MS) {
                record("fifo rx", seq, addr);
                return -1;
            }
        uint32_t w0 = R32(RFDR(0)), w1 = R32(RFDR(1));
        for (uint32_t i = 0; i < len && i < 8; i++)
            rx[i] = (uint8_t)((i < 4 ? w0 : w1) >> (8 * (i & 3)));
        R32(INTR) = INTR_IPRXWA;
    }
    R32(INTR) = INTR_CLEAR;
    return 0;
}

static int read_status(void)
{
    uint8_t status = 0xff;
    return ip_command(SEQ_RDSR, 0, 0, &status, 1) ? -1 : status;
}

/* one of our sequences written into the LUT (between an unlock and a lock) */
static void lut_set(int seq, const uint32_t words[2])
{
    R32(LUTKEY) = 0x5af05af0u;
    R32(LUTCR) = 2;                         /* unlock */
    R32(LUT(4 * (seq_base + seq) + 0)) = words[0];
    R32(LUT(4 * (seq_base + seq) + 1)) = words[1];
    R32(LUT(4 * (seq_base + seq) + 2)) = 0;
    R32(LUT(4 * (seq_base + seq) + 3)) = 0;
    R32(LUTKEY) = 0x5af05af0u;
    R32(LUTCR) = 1;                         /* lock */
}

/* A command with any instruction and up to one data byte, out (tx) or in (rx), or none. The id sequence's slot
   lends itself for the moment and gets the id read back afterwards: our five sequences fill the slots the
   bootloader's AHB sequences leave free, so there is no sixth one. */
static int borrowed_command(uint8_t instruction, const uint8_t *tx, uint8_t *rx)
{
    uint32_t data = tx ? INSTR(OP_WRITE, 0x04) : rx ? INSTR(OP_READ, 0x04) : 0;
    const uint32_t words[2] = { INSTR(OP_CMD, instruction) | data << 16, 0 };
    lut_set(SEQ_RDID, words);
    int err = ip_command(SEQ_RDID, 0, tx, rx, tx || rx ? 1 : 0);
    lut_set(SEQ_RDID, sequences[SEQ_RDID]);
    return err;
}

/* a one-byte register read (0x35 status 2, 0x15 status 3) */
static int read_register(uint8_t instruction)
{
    uint8_t v = 0xff;
    return borrowed_command(instruction, 0, &v) ? -1 : v;
}

void flexspi_init(void)
{
    uint32_t cr2 = R32(FLSHA1CR2);
    int ard = cr2 & 0x1f, ardn = ((cr2 >> 5) & 7) + 1;
    int awr = (cr2 >> 8) & 0x1f, awrn = ((cr2 >> 13) & 7) + 1;
    static const int candidates[] = { 11, 6, 1 };
    for (unsigned i = 0; i < sizeof candidates / sizeof candidates[0] && seq_base < 0; i++)
        if (!overlaps(candidates[i], ard, ardn) && !overlaps(candidates[i], awr, awrn))
            seq_base = candidates[i];
    if (seq_base < 0) {
        record("sem LUT livre", -1, cr2);
        return;
    }
    for (int s = 0; s < SEQ_COUNT; s++)
        lut_set(s, sequences[s]);

    /* read-only probes for the diagnostics */
    uint8_t id[3] = { 0 };
    if (ip_command(SEQ_RDID, 0, 0, id, 3) == 0)
        jedec_id = (uint32_t)id[0] << 16 | id[1] << 8 | id[2];
    flexspi_read_status(boot_sr);
    for (int i = 0; i < 3; i++)
        now_sr[i] = boot_sr[i];
}

/* status registers 1-3 now (05h, 35h, 15h); -1 where a read failed */
void flexspi_read_status(int sr[3])
{
    sr[0] = read_status();
    sr[1] = read_register(0x35);
    sr[2] = read_register(0x15);
}

void flexspi_status(struct flash_status *out)
{
    out->jedec = jedec_id;
    for (int i = 0; i < 3; i++) {
        out->sr[i] = (int16_t)boot_sr[i];
        out->sr_now[i] = (int16_t)now_sr[i];
    }
}

int flexspi_ready(void)
{
    return seq_base >= 0;
}

static int wait_ready(uint32_t addr, uint32_t timeout_ms)
{
    uint32_t t0 = time_ms();
    for (;;) {
        int status = read_status();
        if (status < 0)
            return -1;
        if (!(status & 1))                              /* WIP */
            return 0;
        if (time_ms() - t0 > timeout_ms) {
            record("flash ocupada", SEQ_RDSR, addr);
            return -1;
        }
    }
}

/* the memory-mapped reads must not see data the AHB buffers prefetched before the change */
static void flush_ahb(void)
{
    R32(MCR0) |= 1u;                                    /* software reset: clears the AHB buffers */
    wait_bits(MCR0, 1u, 0u, CMD_TIMEOUT_MS);
}

static int write_enable(uint32_t addr)
{
    if (ip_command(SEQ_WREN, addr, 0, 0, 0))
        return -1;
    int status = read_status();
    if (status < 0)
        return -1;
    if (!(status & 2)) {                                /* WEL did not set: the flash ignores writes */
        record("WEL=0", SEQ_WREN, addr | (uint32_t)status << 24);
        return -1;
    }
    return 0;
}

int flexspi_erase_4k(uint32_t addr)
{
    if (seq_base < 0)
        return -1;
    int err = write_enable(addr) || ip_command(SEQ_ERASE, addr, 0, 0, 0) || wait_ready(addr, ERASE_TIMEOUT_MS);
    flush_ahb();
    return err ? -1 : 0;
}

int flexspi_program_page(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (seq_base < 0 || !len || len > 256 || (addr & 0xffu) + len > 256)
        return -1;
    int err = 0;
    for (uint32_t off = 0; off < len && !err; off += TX_FIFO) {    /* one FIFO load per program command */
        uint32_t n = len - off < TX_FIFO ? len - off : TX_FIFO;
        err = write_enable(addr + off) || ip_command(SEQ_PROGRAM, addr + off, data + off, 0, n) ||
              wait_ready(addr + off, PROGRAM_TIMEOUT_MS);
    }
    flush_ahb();
    return err ? -1 : 0;
}

/* ---------------------------------------------------------------- write protection (platform/flash.h) */

#define JEDEC_W25Q64  0xef4017u
#define SR1_PROTECT   0x7cu                 /* BP0-2, TB, SEC */
#define SR1_SRP       0x80u
#define SR1_WRITABLE  0xfcu                 /* BUSY and WEL are not written */
#define SR2_SRL       0x01u                 /* status registers locked until the power goes */
#define SR2_QE        0x02u
#define SR2_CMP       0x40u
#define SR3_WPS       0x04u

static int lock_state = FLASH_LOCK_NONE;

/* A volatile status register write: 50h, then the write instruction with its byte, nothing else between
   (interrupts off over the pair). The value lasts until the power goes; the chip's non-volatile bits are
   never touched, so a power cycle always brings back what the factory left. */
static int write_volatile(uint8_t instruction, uint8_t value)
{
    uint32_t irq = cpu_irq_disable();
    int err = borrowed_command(0x50, 0, 0) || borrowed_command(instruction, &value, 0);
    cpu_irq_restore(irq);
    return err || wait_ready(0, CMD_TIMEOUT_MS) ? -1 : 0;
}

static int lock_apply(uint8_t protect);

int flexspi_lock(uint8_t protect)
{
    lock_state = lock_apply(protect);
    if (seq_base >= 0) {                    /* what the diagnostics and protocol 13 show from now on */
        flexspi_read_status(now_sr);
    }
    return lock_state;
}

static int lock_apply(uint8_t protect)
{
    if (seq_base < 0 || jedec_id != JEDEC_W25Q64 || (protect & ~SR1_PROTECT))
        return FLASH_LOCK_UNSUPPORTED;
    int s1 = read_status(), s2 = read_register(0x35), s3 = read_register(0x15);
    if (s1 < 0 || s2 < 0 || s3 < 0)
        return FLASH_LOCK_FAILED;
    if (s2 & SR2_SRL)                       /* already locked (a warm restart): on if it is our protection */
        return (s1 & SR1_PROTECT) == protect ? FLASH_LOCK_ON : FLASH_LOCK_FAILED;
    /* only from the state the pedal is known to have: no other protection, quad mode on */
    if ((s1 & (SR1_PROTECT | SR1_SRP)) || !(s2 & SR2_QE) || (s2 & SR2_CMP) || (s3 & SR3_WPS))
        return FLASH_LOCK_UNSUPPORTED;

    uint8_t want1 = (uint8_t)((s1 & SR1_WRITABLE & ~SR1_PROTECT) | protect);
    if (write_volatile(0x01, want1) || (read_status() & SR1_WRITABLE) != want1) {
        write_volatile(0x01, (uint8_t)(s1 & SR1_WRITABLE));
        return FLASH_LOCK_FAILED;
    }
    uint8_t want2 = (uint8_t)(s2 | SR2_SRL);    /* QE and the rest as they are */
    if (write_volatile(0x31, want2) || read_register(0x35) != want2)
        return FLASH_LOCK_FAILED;
    /* the lock itself: taking the protection away must not work any more */
    write_volatile(0x01, (uint8_t)(s1 & SR1_WRITABLE));
    if ((read_status() & SR1_WRITABLE) != want1)
        return FLASH_LOCK_FAILED;
    return FLASH_LOCK_ON;
}

int flexspi_lock_state(void)
{
    return lock_state;
}

void flexspi_diag(struct fmt *f)
{
    fmt_str(f, "LUT ");
    fmt_dec(f, (uint32_t)(seq_base < 0 ? 99 : seq_base));
    fmt_str(f, "  ID ");
    fmt_hex(f, jedec_id, 6);
    fmt_str(f, "  SR ");
    for (int i = 0; i < 3; i++) {
        fmt_hex(f, (uint32_t)boot_sr[i] & 0xffu, 2);
        fmt_str(f, i < 2 ? " " : "");
    }
    static const char *const lock_names[] = { "", "  TRAVA", "  TRAVA FALHOU", "  TRAVA N/A" };
    if (lock_state > 0 && lock_state < 4) {
        fmt_str(f, lock_names[lock_state]);
        fmt_str(f, " -> ");
        for (int i = 0; i < 3; i++) {
            fmt_hex(f, (uint32_t)now_sr[i] & 0xffu, 2);
            fmt_str(f, i < 2 ? " " : "");
        }
    }
    fmt_str(f, "  cmds ");
    fmt_dec(f, commands);
    fmt_str(f, "\nMCR0 ");
    fmt_hex(f, R32(MCR0), 8);
    fmt_str(f, " MCR2 ");
    fmt_hex(f, R32(MCR2), 8);
    fmt_str(f, " AHBCR ");
    fmt_hex(f, R32(AHBCR), 8);
    fmt_str(f, "\nA1 ");
    fmt_hex(f, R32(FLSHA1CR0), 8);
    fmt_str(f, " A2 ");
    fmt_hex(f, R32(FLSHA2CR0), 8);
    fmt_str(f, " B1 ");
    fmt_hex(f, R32(FLSHB1CR0), 8);
    fmt_str(f, "\nCR1 ");
    fmt_hex(f, R32(FLSHA1CR1), 8);
    fmt_str(f, " CR2 ");
    fmt_hex(f, R32(FLSHA1CR2), 8);
    if (!fail.step)
        return;
    fmt_str(f, "\nERRO ");
    fmt_dec(f, failures);
    fmt_str(f, "x: ");
    fmt_str(f, fail.step);
    fmt_str(f, " seq ");
    fmt_dec(f, (uint32_t)fail.seq);
    fmt_str(f, " @");
    fmt_hex(f, fail.addr, 8);
    fmt_str(f, "\nINTR ");
    fmt_hex(f, fail.intr, 4);
    fmt_str(f, " STS0 ");
    fmt_hex(f, fail.sts0, 4);
    fmt_str(f, " STS1 ");
    fmt_hex(f, fail.sts1, 8);
}
