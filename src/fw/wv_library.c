#include "fw/wv_library.h"
#include "fw/bytes.h"
#include "fw/crc32.h"
#include "fw/flash_map.h"
#include "platform/flash.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

int wvlib_check(const uint8_t *img, uint32_t size)
{
    if (!img || size < 0x18 || rd32(img) != WVLIB_MAGIC)
        return -1;
    uint32_t total = rd32(img + 0x0C);
    if (total < 0x18 || total > size)
        return -1;
    return crc32(0, img + 0x18, total - 0x18) == rd32(img + 0x10) ? 0 : -1;
}

static struct {
    uint8_t head[0x18];                         /* the header of the image being written */
    uint8_t have_head;
    uint32_t next;                              /* the offset the next sector must start at */
} wr;

int wvlib_write(uint32_t offset, const uint8_t *data, uint32_t n)
{
    if (offset == 0)
        wr.have_head = 0, wr.next = 0;
    if (offset != wr.next || offset % FLASH_SECTOR_SIZE || !n || n > FLASH_SECTOR_SIZE ||
        offset + n > FLASH_WV_LIBRARY_SIZE || (offset == 0 && n < sizeof wr.head))
        return -1;
    if (flash_erase_sector(FLASH_WV_LIBRARY + offset) != 0)
        return -1;
    uint32_t skip = 0;
    if (offset == 0) {                          /* the header waits for the commit */
        for (unsigned i = 0; i < sizeof wr.head; i++)
            wr.head[i] = data[i];
        wr.have_head = 1;
        skip = sizeof wr.head;
    }
    if (flash_program(FLASH_WV_LIBRARY + offset + skip, data + skip, n - skip) != 0)
        return -1;
    wr.next = offset + FLASH_SECTOR_SIZE;
    return 0;
}

int wvlib_commit(uint32_t total)
{
    if (!wr.have_head || total < sizeof wr.head || total > wr.next || rd32(wr.head + 0x0C) != total)
        return -1;
    if (flash_program(FLASH_WV_LIBRARY, wr.head, sizeof wr.head) != 0)
        return -1;
    wr.have_head = 0;
    wr.next = 0;
    return wvlib_check(flash_ptr(FLASH_WV_LIBRARY, total), total);
}

int wvlib_open(struct library *lib)
{
    lib_open(lib, 0, 0);                        /* empty */
    const uint8_t *head = flash_ptr(FLASH_WV_LIBRARY, 0x18);
    if (!head || rd32(head) != WVLIB_MAGIC)
        return -1;
    uint32_t total = rd32(head + 0x0C);
    if (total > FLASH_WV_LIBRARY_SIZE)
        return -1;
    const uint8_t *img = flash_ptr(FLASH_WV_LIBRARY, total);
    if (wvlib_check(img, total) != 0 || lib_open(lib, img, total) != 0)
        return -1;
    lib->wv = 1;
    return 0;
}
