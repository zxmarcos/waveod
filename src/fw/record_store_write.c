/*
 * Writing the record store the way the original firmware does (app V1.2.2):
 *   - store_format  = FUN_00026000 on an area without the store header;
 *   - store_mount   = FUN_00026160 (start-up scan): superseded copies and in-use sectors without a valid
 *                     record are erased, every sector not in use is free, the search starts at the last one;
 *   - store_write   = FUN_000262F4: the group of the record goes to the next free sector (search from
 *                     `next`, wrapping over 1..count-1): erase it, copy the group's other valid records,
 *                     program the new record, the header with sequence + 1, then the 0x8E8A mark, and finally
 *                     mark the old sector 0x8E80. A power cut at any point leaves either copy readable.
 * The one addition: everything is read back before the 0x8E8A mark commits the new sector.
 */
#include "fw/bytes.h"
#include "fw/record_store.h"
#include "platform/flash.h"
#include <string.h>

#define STORE_MAGIC       0x8F9Au
#define SECTOR_IN_USE     0x8E8Au
#define SECTOR_SUPERSEDED 0x8E80u
#define SECTOR_RESERVED   0x0010u           /* skipped by the original's scan (never free) */
#define SECTOR_HEADER     0x1C

static int is_available(const struct record_store *s, unsigned i)
{
    return (s->available[i / 32] >> (i % 32)) & 1u;
}

static void set_available(struct record_store *s, unsigned i, int on)
{
    if (on)
        s->available[i / 32] |= 1u << (i % 32);
    else
        s->available[i / 32] &= ~(1u << (i % 32));
}

static uint32_t sector_offset(const struct record_store *s, unsigned i)
{
    return s->offset + i * s->sector_size;
}

static const uint8_t *sector_ptr(const struct record_store *s, unsigned i)
{
    return s->base + i * s->sector_size;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int store_format(struct record_store *s, uint32_t offset, uint32_t size)
{
    uint8_t h[0x18];
    memset(h, 0xff, sizeof h);
    put16(h + 0x00, STORE_MAGIC);
    put16(h + 0x04, 1);
    put16(h + 0x08, (uint16_t)size);
    put16(h + 0x0a, (uint16_t)(size >> 16));
    put16(h + 0x0c, 0x1000);
    put16(h + 0x0e, 0x100);
    put16(h + 0x10, (uint16_t)(size / 0x1000));
    put16(h + 0x12, 3);
    put16(h + 0x14, SECTOR_HEADER);
    put16(h + 0x16, 0x54c);
    if (flash_erase_sector(offset) || flash_program(offset, h, sizeof h))
        return -1;
    /* the original marks every data sector superseded (= free) without erasing it */
    uint8_t mark[2];
    put16(mark, SECTOR_SUPERSEDED);
    for (uint32_t i = 1; i < size / 0x1000; i++)
        flash_program(offset + i * 0x1000, mark, 2);
    return store_open(s, offset, size);
}

int store_mount(struct record_store *s)
{
    if (!s->base)
        return -1;
    memset(s->group_sector, 0, sizeof s->group_sector);
    memset(s->available, 0, sizeof s->available);
    s->next = 1;
    for (unsigned i = 1; i < s->sectors; i++) {
        const uint8_t *h = sector_ptr(s, i);
        uint16_t magic = rd16(h);
        if (magic != SECTOR_IN_USE) {
            if (magic != SECTOR_RESERVED) {
                set_available(s, i, 1);
                s->next = (uint16_t)i;
            }
            continue;
        }
        uint8_t group = h[4], sq = h[5], free_mask = h[6], written = h[7];
        if (!(written & ~free_mask) || group >= STORE_MAX_GROUPS) {
            flash_erase_sector(sector_offset(s, i));        /* in use but nothing valid in it */
            set_available(s, i, 1);
            continue;
        }
        unsigned cur = s->group_sector[group];
        if (!cur) {
            s->group_sector[group] = (uint8_t)i;
            s->group_seq[group] = sq;
        } else if ((int)sq - (int)s->group_seq[group] > 0x7f) {
            flash_erase_sector(sector_offset(s, i));        /* older copy */
            set_available(s, i, 1);
        } else {
            s->group_sector[group] = (uint8_t)i;
            s->group_seq[group] = sq;
            flash_erase_sector(sector_offset(s, cur));      /* the one seen before is older */
            set_available(s, cur, 1);
        }
    }
    s->mounted = 1;
    return 0;
}

static int find_free(const struct record_store *s)
{
    unsigned u = s->next;
    do {
        if (is_available(s, u))
            return u > 0 ? (int)u : -1;
        if (++u >= s->sectors)
            u = 1;
    } while (u != s->next);
    return -1;
}

int store_write(struct record_store *s, unsigned n, const void *data, unsigned len)
{
    if (!s->base || n >= STORE_MAX_RECORDS || len > s->record_size)
        return STORE_ERR_ARGS;
    if (!s->mounted && store_mount(s))
        return STORE_ERR_MOUNT;
    unsigned group = n / s->per_sector, k = n % s->per_sector;
    if (group >= STORE_MAX_GROUPS)
        return STORE_ERR_ARGS;
    int fresh = find_free(s);
    if (fresh < 0)
        return STORE_ERR_FULL;
    unsigned old = s->group_sector[group];
    uint32_t new_off = sector_offset(s, (unsigned)fresh);
    if (flash_erase_sector(new_off))
        return STORE_ERR_ERASE;

    uint8_t h[SECTOR_HEADER];
    uint8_t seq = 0, free_mask = 0xff, written = 0xff;
    if (old) {
        const uint8_t *oh = sector_ptr(s, old);
        uint8_t old_header[8];
        memcpy(old_header, oh, sizeof old_header);
        seq = old_header[5];
        free_mask = old_header[6];
        written = old_header[7];
        for (unsigned j = 0; j < s->per_sector; j++) {
            if (j == k)
                continue;
            uint8_t bit = (uint8_t)(1u << j);
            if (!(bit & ~free_mask & written)) {
                free_mask |= bit;
                written |= bit;
                continue;
            }
            /* through RAM, like the original (it copies via a stack buffer): the flash cannot be read
               while it is being programmed */
            static uint8_t copy[0x800];
            uint32_t at = s->header_size + j * s->record_size;
            if (s->record_size > sizeof copy)
                return STORE_ERR_COPY;
            memcpy(copy, oh + at, s->record_size);
            if (flash_program(new_off + at, copy, s->record_size))
                return STORE_ERR_COPY;
        }
    }
    memset(h, 0xff, sizeof h);
    h[4] = (uint8_t)group;
    h[5] = (uint8_t)(seq + 1);
    h[6] = (uint8_t)(free_mask & ~(1u << k));
    h[7] = (uint8_t)(written | 1u << k);
    if (flash_program(new_off + s->header_size + k * s->record_size, data, len) ||
        flash_program(new_off, h, sizeof h))
        return STORE_ERR_PROGRAM;                           /* not committed: the old copy stays */

    uint8_t mark[2];
    put16(mark, SECTOR_IN_USE);
    if (flash_program(new_off, mark, 2))
        return STORE_ERR_COMMIT;
    if (old) {
        put16(mark, SECTOR_SUPERSEDED);
        flash_program(sector_offset(s, old), mark, 2);
        set_available(s, old, 1);
    }
    set_available(s, (unsigned)fresh, 0);
    s->group_sector[group] = (uint8_t)fresh;
    s->group_seq[group] = h[5];
    s->next = (uint16_t)fresh;
    return 0;
}
