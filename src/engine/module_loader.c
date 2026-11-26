#include "engine/module_loader.h"
#include "engine/pools.h"
#include "fw/bytes.h"
#include "sdk/wvfx.h"
#include <string.h>

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHF_WRITE    1u
#define SHF_ALLOC    2u
#define SHF_EXEC     4u

#define R_ARM_NONE            0
#define R_ARM_ABS32           2
#define R_ARM_REL32           3
#define R_ARM_GLOB_DAT        21
#define R_ARM_JUMP_SLOT       22
#define R_ARM_RELATIVE        23
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS    48

struct elf_view {
    const uint8_t *data;
    uint32_t size;
    uint32_t shoff;
    unsigned shnum;
    const uint8_t *shstr;
    uint32_t shstr_size;
};

struct section {
    uint32_t name, type, flags, addr, offset, size, link;
};

static int section_at(const struct elf_view *e, unsigned i, struct section *s)
{
    const uint8_t *h = e->data + e->shoff + 40u * i;
    s->name = rd32(h);
    s->type = rd32(h + 4);
    s->flags = rd32(h + 8);
    s->addr = rd32(h + 12);
    s->offset = rd32(h + 16);
    s->size = rd32(h + 20);
    s->link = rd32(h + 24);
    if (s->type != SHT_NOBITS && (s->offset > e->size || s->size > e->size - s->offset))
        return -1;
    return 0;
}

static int name_is(const struct elf_view *e, const struct section *s, const char *prefix)
{
    size_t n = strlen(prefix);
    return s->name + n <= e->shstr_size && memcmp(e->shstr + s->name, prefix, n) == 0;
}

/* Which region a section is loaded into, or -1 when the loader ignores it (same rules as the app). */
static int section_region(const struct elf_view *e, const struct section *s)
{
    if (s->type == SHT_PROGBITS && ((s->flags & (SHF_ALLOC | SHF_EXEC)) == (SHF_ALLOC | SHF_EXEC) ||
                                    s->flags == SHF_ALLOC) &&
        (s->addr == 0x00000000u || s->addr == 0x10000000u))
        return (int)(s->addr >> 28);
    if ((s->type == SHT_PROGBITS || s->type == SHT_NOBITS) &&
        (s->flags & (SHF_WRITE | SHF_ALLOC)) == (SHF_WRITE | SHF_ALLOC)) {
        if (name_is(e, s, "ER_IRAM"))
            return POOL_STATE;
        if (name_is(e, s, "ER_SRAM"))
            return POOL_BUFFER;
    }
    return -1;
}

static int elf_open(struct elf_view *e, const uint8_t *data, uint32_t size)
{
    if (size < 52 || memcmp(data, "\x7f" "ELF", 4) != 0 || data[4] != 1 || data[5] != 1)
        return -1;
    unsigned type = rd16(data + 16), machine = rd16(data + 18);
    if ((type != 2 && type != 3) || machine != 40)
        return -1;
    e->data = data;
    e->size = size;
    e->shoff = rd32(data + 32);
    e->shnum = rd16(data + 48);
    unsigned shstrndx = rd16(data + 50);
    if (rd16(data + 46) != 40 || e->shoff > size || e->shnum * 40u > size - e->shoff || shstrndx >= e->shnum)
        return -1;
    struct section s;
    if (section_at(e, shstrndx, &s) != 0)
        return -1;
    e->shstr = data + s.offset;
    e->shstr_size = s.size;
    return 0;
}

/* Place `addr` (module address) in its region; 0 when outside what was loaded. */
static uint32_t relocate_addr(const struct module *m, uint32_t addr)
{
    unsigned r = addr >> 28;
    uint32_t off = addr & 0x0fffffffu;
    if (r >= 4 || !m->region[r] || off > m->region_size[r])
        return 0;
    return m->region[r] + off;
}

static int apply(unsigned type, uint32_t P, uint32_t S)
{
    uint8_t *p = pool_ptr(P, 4);
    if (!p)
        return -1;
    uint32_t w = rd32(p);
    switch (type) {
    case R_ARM_NONE:
        return 0;
    case R_ARM_ABS32:
    case R_ARM_RELATIVE:
        w += S;
        break;
    case R_ARM_REL32:
        w += S - P;
        break;
    case R_ARM_GLOB_DAT:
    case R_ARM_JUMP_SLOT:
        w = S;
        break;
    case R_ARM_THM_MOVW_ABS_NC:
    case R_ARM_THM_MOVT_ABS: {          /* the app ignores the addend of MOVW/MOVT */
        uint32_t v = type == R_ARM_THM_MOVW_ABS_NC ? (S & 0xffffu) : (S >> 16);
        uint32_t hw1 = w & 0xffffu, hw2 = w >> 16;
        hw1 = (hw1 & 0xfbf0u) | ((v >> 12) & 0xfu) | (((v >> 11) & 1u) << 10);
        hw2 = (hw2 & 0x8f00u) | (((v >> 8) & 7u) << 12) | (v & 0xffu);
        w = hw1 | hw2 << 16;
        break;
    }
    default:
        return -1;
    }
    p[0] = (uint8_t)w;
    p[1] = (uint8_t)(w >> 8);
    p[2] = (uint8_t)(w >> 16);
    p[3] = (uint8_t)(w >> 24);
    return 0;
}

static int relocate(const struct elf_view *e, struct module *m)
{
    for (unsigned i = 0; i < e->shnum; i++) {
        struct section rel, sym;
        if (section_at(e, i, &rel) != 0)
            return MODULE_BAD_ELF;
        if (rel.type != SHT_REL)
            continue;
        if (rel.link >= e->shnum || section_at(e, rel.link, &sym) != 0)
            return MODULE_BAD_ELF;
        for (uint32_t j = 0; j + 8 <= rel.size; j += 8) {
            uint32_t r_offset = rd32(e->data + rel.offset + j), r_info = rd32(e->data + rel.offset + j + 4);
            uint32_t so = (r_info >> 8) * 16u;
            if (so + 16 > sym.size)
                return MODULE_BAD_ELF;
            const uint8_t *st = e->data + sym.offset + so;
            uint32_t value = rd32(st + 4);
            unsigned info = st[12], shndx = rd16(st + 14);
            if (shndx == 0 || (info & 0xf) < 1 || (info & 0xf) > 3)
                continue;                       /* undefined or not object/function/section */
            uint32_t S = relocate_addr(m, value), P = relocate_addr(m, r_offset);
            if (!P || (!S && (value & 0x0fffffffu)))
                return MODULE_BAD_RELOC;
            if (!S)
                S = m->region[value >> 28 & 3];
            if (apply(r_info & 0xff, P, S) != 0)
                return MODULE_BAD_RELOC;
            m->relocs++;
        }
    }
    return MODULE_OK;
}

int module_load(const uint8_t *elf, uint32_t elf_size, uint16_t menu_index, struct module *out)
{
    struct elf_view e;
    memset(out, 0, sizeof *out);
    if (elf_open(&e, elf, elf_size) != 0)
        return MODULE_BAD_ELF;

    /* pass 1: extent of each region */
    for (unsigned i = 0; i < e.shnum; i++) {
        struct section s;
        if (section_at(&e, i, &s) != 0)
            return MODULE_BAD_ELF;
        int r = section_region(&e, &s);
        if (r < 0)
            continue;
        if ((s.addr >> 28) != (uint32_t)r)
            return MODULE_BAD_ELF;
        uint32_t end = (s.addr & 0x0fffffffu) + s.size;
        if (end > out->region_size[r])
            out->region_size[r] = end;
    }
    if (out->region_size[POOL_CODE] < 0x28)
        return MODULE_BAD_HEADER;

    /* pass 2: reserve all regions first, so a full pool leaves nothing half loaded */
    for (int r = 0; r < POOL_COUNT; r++) {
        if (!out->region_size[r])
            continue;
        out->region[r] = pool_alloc((enum pool_id)r, out->region_size[r]);
        if (!out->region[r])
            return MODULE_NO_MEMORY;
    }

    /* pass 3: copy / clear */
    for (unsigned i = 0; i < e.shnum; i++) {
        struct section s;
        section_at(&e, i, &s);
        int r = section_region(&e, &s);
        if (r < 0 || !s.size)
            continue;
        uint8_t *dst = pool_ptr(out->region[r] + (s.addr & 0x0fffffffu), s.size);
        if (!dst)
            return MODULE_BAD_ELF;
        if (s.type == SHT_NOBITS)
            memset(dst, 0, s.size);
        else
            memcpy(dst, elf + s.offset, s.size);
    }

    int err = relocate(&e, out);
    if (err != MODULE_OK)
        return err;

    uint8_t *hdr = pool_ptr(out->region[POOL_CODE], 0x28);
    if (rd32(hdr + 0x24) != MODULE_MAGIC)
        return MODULE_BAD_HEADER;
    uint32_t tag = 0xFFAA0000u | menu_index;
    memcpy(hdr, &tag, 4);
    out->header = out->region[POOL_CODE];
    out->id = rd32(hdr + 0x14);
    out->init = rd32(hdr + 0x1c);
    out->process = rd32(hdr + 0x20);
    if (rd32(hdr + 4) == WVFX_MAGIC) {      /* a block of waveod's: its descriptor, inside what was loaded */
        if (rd32(hdr + 8) != WVFX_ABI || !pool_ptr(rd32(hdr + 0x0c), sizeof(struct wvfx_desc)))
            return MODULE_BAD_HEADER;
        out->desc = rd32(hdr + 0x0c);
        out->init = out->process = 0;
    }
    return MODULE_OK;
}
