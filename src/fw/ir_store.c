#include "fw/ir_store.h"
#include "fw/bytes.h"
#include "fw/flash_map.h"
#include "platform/flash.h"
#include <string.h>

#define IR_DIR_MAGIC    0x5AA5u
#define IR_ENTRY_SIZE   0x48u
#define FS_MAGIC        0x5AFAu
#define FS_DIR_SECTORS  3

int ir_dir_open(struct ir_dir *d)
{
    memset(d, 0, sizeof *d);
    const uint8_t *h = flash_ptr(FLASH_IR_LIBRARY, 0x10);
    if (!h || rd16(h) != IR_DIR_MAGIC || !rd16(h + 4))
        return -1;
    d->base = h;
    d->block_size = rd16(h + 4);
    d->count = h[0x0a];
    if (!flash_ptr(FLASH_IR_LIBRARY, 0x10 + IR_ENTRY_SIZE * d->count))
        return -1;
    return 0;
}

int ir_cab_count(const struct ir_dir *d)
{
    int n = d->count - IR_USER_COUNT - IR_CLONE_COUNT;
    return n > 0 ? n : 0;
}

static const uint8_t *dir_entry(const struct ir_dir *d, int i)
{
    if (!d->base || i < 0 || i >= d->count)
        return 0;
    return d->base + 0x10 + IR_ENTRY_SIZE * (unsigned)i;
}

int ir_dir_entry(const struct ir_dir *d, int i, uint32_t *id, char name[IR_NAME_LEN + 1])
{
    const uint8_t *e = dir_entry(d, i);
    if (!e)
        return -1;
    if (id)
        *id = rd32(e + 4);
    if (name) {
        int n = 0;
        for (; n < IR_NAME_LEN && e[8 + n] && e[8 + n] != 0xff; n++)
            name[n] = (char)e[8 + n];
        name[n] = 0;
    }
    return 0;
}

int ir_dir_find(const struct ir_dir *d, uint32_t id)
{
    for (int i = 0; i < d->count; i++)
        if (rd32(dir_entry(d, i) + 4) == id)
            return i;
    return -1;
}

static uint32_t dir_data(const struct ir_dir *d, int i)
{
    const uint8_t *e = dir_entry(d, i);
    if (!e || !rd16(e + 2))
        return 0;
    return FLASH_IR_LIBRARY + (uint32_t)rd16(e + 2) * d->block_size;
}

uint32_t ir_cab_data(const struct ir_dir *d, int cab)
{
    return cab >= 0 && cab < ir_cab_count(d) ? dir_data(d, cab) : 0;
}

/*
 * One of the two small file systems. `entry_size_at`/`blocks_from_dir` capture the only differences the
 * original code has between them: where the header keeps the entry size, and whether data blocks are
 * counted from the start of the area (user IRs) or after the directory sectors with their own block size
 * (Clone).
 */
struct small_fs {
    uint32_t base, size;
    unsigned entry_size_at;     /* header byte holding the entry size */
    unsigned capacity_at;       /* header byte holding the number of entries */
    unsigned block_size_at;     /* header u16 holding the data block size, 0 = sector size */
    unsigned dir_index_at;      /* header byte holding this sector's directory index */
};

static const struct small_fs user_ir_fs = { FLASH_USER_IR_FS, FLASH_USER_IR_FS_SIZE, 0x0d, 0x0c, 0, 0x0b };
static const struct small_fs clone_fs = { FLASH_CLONE_FS, FLASH_CLONE_FS_SIZE, 0x08, 0x09, 0x0a, 0x07 };

/* directory entry n of a small file system (marked 'Z'), or 0 */
static const uint8_t *fs_entry(const struct small_fs *fs, int n)
{
    const uint8_t *h0 = flash_ptr(fs->base, 0x10);
    if (!h0 || rd16(h0) >> 7 != FS_MAGIC >> 7)
        return 0;
    uint32_t sector = rd16(h0 + 4);
    unsigned esize = h0[fs->entry_size_at], cap = h0[fs->capacity_at];
    if (!sector || sector > 0x10000 || !esize || n < 0 || n >= cap)
        return 0;
    unsigned per = (sector - 0x10) / esize;
    if (!per)
        return 0;
    int dir_sector = -1;                        /* sector holding directory part n / per */
    for (int s = 0; s < FS_DIR_SECTORS; s++) {
        const uint8_t *h = flash_ptr(fs->base + (uint32_t)s * sector, 0x10);
        if (h && rd16(h) == FS_MAGIC && h[fs->dir_index_at] == (unsigned)n / per)
            dir_sector = s;
    }
    if (dir_sector < 0)
        return 0;
    const uint8_t *e = flash_ptr(fs->base + (uint32_t)dir_sector * sector + 0x10 + (n % per) * esize, esize);
    return e && e[0] == 'Z' ? e : 0;
}

static uint32_t fs_data(const struct small_fs *fs, int n)
{
    const uint8_t *e = fs_entry(fs, n), *h0 = flash_ptr(fs->base, 0x10);
    if (!e)
        return 0;
    uint32_t sector = rd16(h0 + 4);
    uint32_t off = fs->block_size_at ? rd16(h0 + fs->block_size_at) * (uint32_t)rd16(e + 2) + sector * FS_DIR_SECTORS
                                     : sector * (uint32_t)rd16(e + 2);
    return off < fs->size ? fs->base + off : 0;
}

uint32_t ir_user_data(int n)
{
    return n >= 0 && n < IR_USER_COUNT ? fs_data(&user_ir_fs, n) : 0;
}

uint32_t ir_clone_data(const struct ir_dir *d, int n)
{
    if (n < 0 || n >= IR_CLONE_COUNT)
        return 0;
    uint32_t off = fs_data(&clone_fs, n);
    return off ? off : dir_data(d, IR_DIR_CLONE_FIRST(d) + n);
}

int ir_fs_name(int clone, int n, char name[IR_FS_NAME_LEN + 1])
{
    const uint8_t *e = n >= 0 && n < (clone ? IR_CLONE_COUNT : IR_USER_COUNT) ? fs_entry(clone ? &clone_fs : &user_ir_fs, n) : 0;
    if (!e)
        return -1;
    int k = 0;
    for (; k < IR_FS_NAME_LEN && e[8 + k] && e[8 + k] != 0xff; k++)
        name[k] = (char)e[8 + k];
    name[k] = 0;
    return 0;
}
