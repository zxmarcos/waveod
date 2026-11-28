#include "fw/ir_fs.h"
#include "fw/bytes.h"
#include "fw/flash_map.h"
#include "fw/ir_store.h"
#include "platform/flash.h"
#include <string.h>

#define MAGIC_ACTIVE    0x5AFAu
#define MAGIC_OLD       0x5AF8u
#define PARTS           3                   /* directory parts the RAM state can follow */
#define ENTRY_MAX       0x4C

struct fs {
    uint32_t base, size;
    uint16_t sector;                        /* directory sector size */
    uint16_t block;                         /* data block size */
    uint8_t dir_sectors, per, capacity, esize;
    uint8_t current[PARTS];                 /* directory sector of each part, 0xFF = none */
    uint32_t free[3];                       /* free data blocks, bit set = free (see free_word) */
    /* header layout (the two file systems differ) */
    uint8_t at_dir_sectors, at_index, at_capacity, at_esize, at_block;
    uint8_t default_capacity, default_esize;
    const char *tag;                        /* the second name the original stores at +0x19 */
    enum irfs_kind kind;
};

static struct fs user_fs = {
    .base = FLASH_USER_IR_FS, .size = FLASH_USER_IR_FS_SIZE, .at_dir_sectors = 0x0a, .at_index = 0x0b,
    .at_capacity = 0x0c, .at_esize = 0x0d, .at_block = 0x08, .default_capacity = 50, .default_esize = 0x4c,
    .tag = "User IR", .kind = IRFS_USER,
};
static struct fs clone_fs = {
    .base = FLASH_CLONE_FS, .size = FLASH_CLONE_FS_SIZE, .at_dir_sectors = 0x06, .at_index = 0x07,
    .at_capacity = 0x09, .at_esize = 0x08, .at_block = 0x0a, .default_capacity = 10, .default_esize = 0x2c,
    .tag = "", .kind = IRFS_CLONE,
};

static void copy_text(uint8_t *dst, const char *src, unsigned n)     /* strncpy: zero padded */
{
    unsigned i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = (uint8_t)src[i];
    for (; i < n; i++)
        dst[i] = 0;
}

static int ctz(uint32_t v)
{
    int n = 0;
    while (!(v & 1u)) {
        v >>= 1;
        n++;
    }
    return n;
}

/* the original keeps user IR blocks in words 0..2 and Clone blocks one word higher (FUN_00024C28) */
static uint32_t *free_word(struct fs *f, unsigned blk)
{
    unsigned w = (blk >> 5) + (f->kind == IRFS_CLONE ? 1u : 0u);
    return w < 3 ? &f->free[w] : 0;
}

static void mark(struct fs *f, unsigned blk, int is_free)
{
    uint32_t *w = free_word(f, blk);
    if (!w)
        return;
    if (is_free)
        *w |= 1u << (blk & 31);
    else
        *w &= ~(1u << (blk & 31));
}

static int find_free(const struct fs *f)
{
    if (f->kind == IRFS_CLONE)
        return f->free[1] ? ctz(f->free[1]) : -1;
    for (int w = 0; w < 3; w++)
        if (f->free[w])
            return 32 * w + ctz(f->free[w]);
    return -1;
}

static uint32_t sector_at(const struct fs *f, unsigned s);
static uint32_t entry_at(const struct fs *f, unsigned s, unsigned j);
static void read_bytes(uint32_t at, uint8_t *dst, uint32_t n);

/* whether a deleted entry (marker cleared, not 'Z', not blank) still names block blk. The original frees the
   block a replaced entry names even when that entry was deleted (FUN_00025D30): after a restart the block may hold
   another capture, which the next write then overwrites. Its own slots live with that; waveod's slots past them
   never take such a block (fw/ir_store.h IR_CLONE_SLOTS), so the original can never free one of theirs. */
static int stale_block(const struct fs *f, unsigned blk)
{
    for (int p = 0; p < PARTS; p++) {
        if (f->current[p] == 0xff)
            continue;
        for (unsigned j = 0; j < f->per; j++) {
            uint8_t e[4];
            read_bytes(entry_at(f, f->current[p], j), e, sizeof e);
            if (e[0] != 'Z' && e[0] != 0xff && rd16(e + 2) == blk)
                return 1;
        }
    }
    return 0;
}

/* a free Clone block for one of waveod's own slots: the lowest no deleted entry names, else the lowest */
static int find_free_own(const struct fs *f)
{
    for (uint32_t w = f->free[1]; w; w &= w - 1) {
        unsigned b = (unsigned)ctz(w);
        if (!stale_block(f, b))
            return (int)b;
    }
    return find_free(f);
}

static uint32_t sector_at(const struct fs *f, unsigned s)
{
    return f->base + (uint32_t)f->sector * s;
}

static uint32_t entry_at(const struct fs *f, unsigned s, unsigned j)
{
    return sector_at(f, s) + 0x10 + (uint32_t)f->esize * j;
}

static uint32_t data_at(const struct fs *f, unsigned blk)
{
    if (f->kind == IRFS_CLONE)
        return f->base + (uint32_t)f->sector * f->dir_sectors + (uint32_t)f->block * blk;
    return f->base + (uint32_t)f->sector * blk;
}

static void read_bytes(uint32_t at, uint8_t *dst, uint32_t n)       /* to RAM: never program from XIP */
{
    const uint8_t *p = flash_ptr(at, n);
    if (p)
        memcpy(dst, p, n);
    else
        memset(dst, 0xff, n);
}

/* FUN_000255D4 / FUN_00024C28 */
static void mount(struct fs *f)
{
    memset(f->current, 0xff, sizeof f->current);
    f->free[0] = f->free[1] = f->free[2] = 0xffffffffu;
    f->dir_sectors = f->kind == IRFS_USER ? 1 : 3;
    f->capacity = f->kind == IRFS_USER ? 0 : f->capacity;
    f->sector = FLASH_SECTOR_SIZE;
    for (unsigned s = 0; s < (f->kind == IRFS_USER ? f->dir_sectors : 3u); s++) {
        uint8_t h[16];
        read_bytes(sector_at(f, s), h, sizeof h);
        if (rd16(h) >> 7 != MAGIC_ACTIVE >> 7)
            continue;
        f->sector = rd16(h + 4);
        f->block = rd16(h + f->at_block);
        f->dir_sectors = h[f->at_dir_sectors];
        f->capacity = h[f->at_capacity];
        f->esize = h[f->at_esize];
        f->per = (uint8_t)(f->esize ? (f->sector - 0x10) / f->esize : 0);
        if (rd16(h) != MAGIC_ACTIVE || h[f->at_index] >= PARTS)
            continue;
        f->current[h[f->at_index]] = (uint8_t)s;
        for (unsigned j = 0; j < f->per; j++) {
            uint8_t e[4];
            read_bytes(entry_at(f, s, j), e, sizeof e);
            if (e[0] == 'Z')
                mark(f, rd16(e + 2), 0);
        }
    }
    /* the directory sectors themselves (the Clone file system marks them in a word it never searches) */
    for (unsigned b = 0; b < f->dir_sectors; b++) {
        if (f->kind == IRFS_USER)
            mark(f, b, 0);
        else
            f->free[0] &= ~(1u << b);
    }
}

void irfs_mount(void)
{
    mount(&user_fs);
    mount(&clone_fs);
}

/* FUN_000255D4 / FUN_00024AE0 on an unformatted area: the headers of the directory parts needed */
static int format(struct fs *f)
{
    unsigned per = (FLASH_SECTOR_SIZE - 0x10) / f->default_esize;
    unsigned parts = (f->default_capacity + per - 1) / per;
    for (unsigned s = 0; s < parts; s++) {
        uint8_t h[16];
        memset(h, 0xff, sizeof h);
        put16le(h, MAGIC_ACTIVE);
        put16le(h + 2, 0xffff);
        put16le(h + 4, FLASH_SECTOR_SIZE);
        if (f->kind == IRFS_USER) {
            put16le(h + 6, (uint16_t)(f->size / FLASH_SECTOR_SIZE));
            put16le(h + 8, 0x1000);
            h[0x0a] = 3;
            h[0x0b] = (uint8_t)s;
            h[0x0c] = f->default_capacity;
            h[0x0d] = f->default_esize;
        } else {
            h[6] = 3;
            h[7] = (uint8_t)s;
            h[8] = f->default_esize;
            h[9] = f->default_capacity;
            put16le(h + 0x0a, IRFS_CLONE_DATA);
            h[0x0c] = 0x0c;
        }
        if (flash_erase_sector(sector_at(f, s)) || flash_program(sector_at(f, s), h, sizeof h))
            return -1;
    }
    mount(f);
    return 0;
}

static int formatted(const struct fs *f)
{
    uint8_t h[2];
    read_bytes(f->base, h, sizeof h);
    return rd16(h) >> 7 == MAGIC_ACTIVE >> 7;
}

/* a spare directory sector: the lowest that is no part's current one */
static int spare_sector(const struct fs *f)
{
    uint32_t mask = (1u << f->dir_sectors) - 1u;
    for (int p = 0; p < PARTS; p++)
        if (f->current[p] != 0xff)
            mask &= ~(1u << f->current[p]);
    return mask ? ctz(mask) : -1;
}

/* copy directory part `part` (sector old) to a spare sector, entry j replaced by `with` when j == idx (and
   only the entries in use copied unless copy_all), then switch the part over */
static int rewrite_part(struct fs *f, unsigned part, unsigned old, unsigned idx, const uint8_t *with, int copy_all,
                        unsigned *old_block)
{
    int nw = spare_sector(f);
    if (nw < 0 || flash_erase_sector(sector_at(f, (unsigned)nw)))
        return -1;
    for (unsigned j = 0; j < f->per; j++) {
        uint8_t e[ENTRY_MAX];
        read_bytes(entry_at(f, old, j), e, f->esize);
        if (j == idx) {
            if (old_block)
                *old_block = rd16(e + 2);
            memcpy(e, with, f->esize);
        } else if (!copy_all && e[0] != 'Z') {
            continue;
        }
        if (flash_program(entry_at(f, (unsigned)nw, j), e, f->esize))
            return -1;
    }
    uint8_t h[16];
    read_bytes(sector_at(f, old), h, sizeof h);
    put16le(h, MAGIC_ACTIVE);
    put16le(h + 2, 0xffff);
    h[f->at_index] = (uint8_t)part;
    const uint8_t mark_old[2] = { MAGIC_OLD & 0xff, MAGIC_OLD >> 8 };
    if (flash_program(sector_at(f, (unsigned)nw), h, sizeof h) || flash_program(sector_at(f, old), mark_old, 2))
        return -1;
    f->current[part] = (uint8_t)nw;
    return 0;
}

static struct fs *fs_of(enum irfs_kind kind)
{
    return kind == IRFS_CLONE ? &clone_fs : &user_fs;
}

/* the slots written: the header's capacity for the user IRs (the original's 50), waveod's 20 for Clone (the
   header says the original's 10; fw/ir_store.h IR_CLONE_SLOTS), all in the first directory part */
static int slot_ok(const struct fs *f, int slot)
{
    int n = f->kind == IRFS_CLONE ? IR_CLONE_SLOTS : f->capacity;
    return slot >= 0 && slot < n && f->per && (unsigned)slot / f->per < PARTS;
}

int irfs_write(enum irfs_kind kind, int slot, const char *name, const uint8_t *data, uint32_t len)
{
    struct fs *f = fs_of(kind);
    if (!formatted(f) && format(f))
        return -1;
    if (!slot_ok(f, slot))
        return -1;
    unsigned part = (unsigned)slot / f->per, idx = (unsigned)slot % f->per;
    if (part >= PARTS || f->current[part] == 0xff)
        return -1;
    unsigned cur = f->current[part];
    int own = f->kind == IRFS_CLONE && slot >= IR_CLONE_COUNT;    /* a slot the original never writes */
    int blk = own ? find_free_own(f) : find_free(f);
    if (blk < 0)
        return -1;
    uint32_t at = data_at(f, (unsigned)blk), cap = f->block;
    if (at + cap > f->base + f->size)
        return -1;
    for (uint32_t s = 0; s < (f->kind == IRFS_CLONE ? f->block / FLASH_SECTOR_SIZE : 1u); s++)
        if (flash_erase_sector(at + s * FLASH_SECTOR_SIZE))
            return -1;
    if (data && len && flash_program(at, data, len < cap ? len : cap))
        return -1;

    uint8_t e[ENTRY_MAX], old[ENTRY_MAX];
    memset(e, 0, sizeof e);
    copy_text(e + 8, name, 17);
    copy_text(e + 0x19, f->tag, 17);
    e[1] = (uint8_t)slot;
    put16le(e + 2, (uint16_t)blk);
    read_bytes(entry_at(f, cur, idx), old, f->esize);
    if (old[0] == 0xff) {                   /* a position never used: in place, '_' then 'Z' */
        e[0] = '_';
        if (flash_program(entry_at(f, cur, idx), e, f->esize))
            return -1;
        e[0] = 'Z';
        if (flash_program(entry_at(f, cur, idx), e, f->esize))
            return -1;
        mark(f, (unsigned)blk, 0);
        return 0;
    }
    e[0] = 'Z';
    unsigned freed = 0;                     /* FUN_00025D30 copies every Clone entry, FUN_00025A84 the used ones */
    if (rewrite_part(f, part, cur, idx, e, f->kind == IRFS_CLONE, &freed))
        return -1;
    if (!own || old[0] == 'Z')                  /* a deleted entry's block may be another capture's by now */
        mark(f, freed, 1);
    mark(f, (unsigned)blk, 0);
    return 0;
}

int irfs_rename(enum irfs_kind kind, int slot, const char *name)
{
    struct fs *f = fs_of(kind);
    if (!slot_ok(f, slot))
        return -1;
    unsigned part = (unsigned)slot / f->per, idx = (unsigned)slot % f->per;
    if (part >= PARTS || f->current[part] == 0xff)
        return -1;
    unsigned cur = f->current[part];
    uint8_t e[ENTRY_MAX];
    read_bytes(entry_at(f, cur, idx), e, f->esize);
    if (e[0] != 'Z')
        return -1;
    memset(e + 4, 0, f->esize - 4u);        /* {'Z', slot, block, 0, name, tag} */
    copy_text(e + 8, name, 17);
    copy_text(e + 0x19, f->tag, 17);
    e[1] = (uint8_t)slot;
    return rewrite_part(f, part, cur, idx, e, 1, 0);
}

int irfs_delete_clone(int slot)
{
    struct fs *f = &clone_fs;
    if (!slot_ok(f, slot))
        return -1;
    unsigned cur = f->current[(unsigned)slot / f->per];
    if (cur == 0xff)
        return 0;
    uint8_t e[ENTRY_MAX];
    read_bytes(entry_at(f, cur, (unsigned)slot % f->per), e, f->esize);
    if (e[0] == 0xff)
        return 0;
    e[0] = 0;
    return flash_program(entry_at(f, cur, (unsigned)slot % f->per), e, f->esize) ? -1 : 0;
}
