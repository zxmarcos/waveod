#include "fw/record_store.h"
#include "fw/bytes.h"
#include "platform/flash.h"
#include <string.h>

#define STORE_MAGIC   0x8F9Au
#define SECTOR_IN_USE 0x8E8Au

int store_open(struct record_store *s, uint32_t offset, uint32_t size)
{
    memset(s, 0, sizeof *s);
    const uint8_t *base = flash_ptr(offset, size);
    if (!base || size < 0x18 || rd16(base) != STORE_MAGIC)
        return -1;
    s->offset = offset;
    s->sector_size = rd16(base + 0x0c);
    s->sectors = rd16(base + 0x10);
    s->per_sector = rd16(base + 0x12);
    s->header_size = rd16(base + 0x14);
    s->record_size = rd16(base + 0x16);
    if (!s->sector_size || !s->per_sector || s->per_sector > 8 || s->sectors > STORE_MAX_SECTORS ||
        (uint32_t)s->sectors * s->sector_size > size || s->header_size < 0x1c ||
        s->header_size + (uint32_t)s->per_sector * s->record_size > s->sector_size)
        return -1;
    s->base = base;

    for (unsigned i = 1; i < s->sectors; i++) {
        const uint8_t *h = base + i * s->sector_size;
        if (rd16(h) != SECTOR_IN_USE)
            continue;
        uint8_t group = h[4], sq = h[5], free_mask = h[6], written = h[7];
        if (!(written & ~free_mask) || group >= STORE_MAX_GROUPS)
            continue;                                   /* holds no valid record */
        if (s->group_sector[group] && (int)sq - (int)s->group_seq[group] > 0x7f)
            continue;                                   /* older copy of a group already seen */
        s->group_sector[group] = (uint8_t)i;
        s->group_seq[group] = sq;
    }
    return 0;
}

const uint8_t *store_record(const struct record_store *s, unsigned n)
{
    if (!s->base)
        return 0;
    unsigned group = n / s->per_sector, k = n % s->per_sector;
    if (group >= STORE_MAX_GROUPS || !s->group_sector[group])
        return 0;
    const uint8_t *h = s->base + s->group_sector[group] * s->sector_size;
    if (!((1u << k) & ~(unsigned)h[6] & h[7]))
        return 0;
    return h + s->header_size + k * s->record_size;
}
