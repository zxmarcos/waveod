/*
 * QSPI flash: read through the FlexSPI memory map the bootloader leaves configured (0x60000000), erase and
 * program through FlexSPI IP commands (flexspi.c), limited to the writable range and read back.
 */
#include "board/qme200/board.h"
#include "platform/flash.h"
#include "platform/safety.h"

#define XIP_BASE 0x60000000u

/* last read-back mismatch, for the diagnostics */
static struct {
    const char *what;
    uint32_t addr;
    uint8_t got, want;
} mismatch;

const uint8_t *flash_ptr(uint32_t offset, uint32_t len)
{
    if (offset > FLASH_SIZE || len > FLASH_SIZE - offset)
        return 0;
    return (const uint8_t *)(uintptr_t)(XIP_BASE + offset);
}

uint32_t flash_device_addr(uint32_t offset)
{
    return XIP_BASE + offset;
}

static int writable(uint32_t offset, uint32_t len)
{
    return flash_writable(offset, len);
}

static int read_back(const char *what, uint32_t offset, const uint8_t *want, uint8_t fill, uint32_t len)
{
    const uint8_t *p = flash_ptr(offset, len);
    for (uint32_t i = 0; i < len; i++) {
        uint8_t w = want ? want[i] : fill;
        if (p[i] != w) {
            mismatch.what = what;
            mismatch.addr = offset + i;
            mismatch.got = p[i];
            mismatch.want = w;
            return -1;
        }
    }
    return 0;
}

int flash_erase_sector(uint32_t offset)
{
    if (!writable(offset, FLASH_SECTOR_SIZE) || (offset & (FLASH_SECTOR_SIZE - 1)))
        return -1;
    safety_feed();                          /* an erase can take a few hundred ms */
    if (flexspi_erase_4k(offset))
        return -1;
    return read_back("apagado", offset, 0, 0xff, FLASH_SECTOR_SIZE);
}

int flash_program(uint32_t offset, const void *data, uint32_t len)
{
    if (!writable(offset, len))
        return -1;
    const uint8_t *src = data;
    for (uint32_t done = 0; done < len;) {
        uint32_t at = offset + done;
        uint32_t n = FLASH_PAGE_SIZE - (at & (FLASH_PAGE_SIZE - 1));
        if (n > len - done)
            n = len - done;
        safety_feed();
        if (flexspi_program_page(at, src + done, n))
            return -1;
        done += n;
    }
    /* NOR programming can only clear bits: the result must equal old & new, and the caller only programs
       erased areas or bits it wants cleared, so a plain compare tells whether it worked */
    return read_back("gravado", offset, src, 0, len);
}

void flash_status(struct flash_status *out)
{
    flexspi_status(out);
    out->lock = (uint8_t)flexspi_lock_state();
}

void flash_read_status(int sr[3])
{
    flexspi_read_status(sr);
}

int flash_lock(uint8_t protect)
{
    return flexspi_lock(protect);
}

int flash_lock_state(void)
{
    return flexspi_lock_state();
}

void flash_diag(char *out, unsigned size)
{
    struct fmt f;
    fmt_init(&f, out, size);
    flexspi_diag(&f);
    if (mismatch.what) {
        fmt_str(&f, "\nleitura ");
        fmt_str(&f, mismatch.what);
        fmt_str(&f, " @");
        fmt_hex(&f, mismatch.addr, 6);
        fmt_str(&f, ": ");
        fmt_hex(&f, mismatch.got, 2);
        fmt_str(&f, " em vez de ");
        fmt_hex(&f, mismatch.want, 2);
    }
}
