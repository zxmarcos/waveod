#include "fw/wv_store.h"
#include "fw/bytes.h"
#include "fw/crc32.h"
#include "fw/flash_map.h"
#include "platform/flash.h"
#include "platform/sdram.h"
#include <string.h>

#define SECTORS      (FLASH_WV_STORE_SIZE / FLASH_SECTOR_SIZE)
#define SECTOR_HDR   16
#define REC_HDR      16
#define ALIGN        16
#define SECTOR_MAGIC 0x31535657u            /* "WVS1" */
#define REC_MAGIC    0x5257u                /* "WR" */
#define RESERVE      1                      /* free sectors kept for the clean-up */

enum { S_FREE, S_DIRTY, S_USED };           /* erased, holding something else, ours */

struct entry {
    uint32_t addr;                          /* flash offset of the record header, 0 = none */
    uint32_t seq;
};

static struct {
    uint8_t state[SECTORS];
    uint32_t sseq[SECTORS];                 /* sector sequence (S_USED) */
    uint16_t fill[SECTORS];                 /* where the next record goes; FLASH_SECTOR_SIZE = closed */
    int head;                               /* sector being appended to, -1 = none */
    uint32_t next_sseq, next_seq;
    struct entry index[WVS_TYPES][WVS_KEYS];
    uint8_t buf[FLASH_SECTOR_SIZE];         /* a record on its way to the flash */
} st SDRAM_BSS;

static uint32_t sector_off(int s)
{
    return FLASH_WV_STORE + (uint32_t)s * FLASH_SECTOR_SIZE;
}

static int blank(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (p[i] != 0xff)
            return 0;
    return 1;
}

static unsigned round_up(unsigned n)
{
    return (n + ALIGN - 1) & ~(unsigned)(ALIGN - 1);
}

/* a valid record at `off` inside its sector: its total size, 0 = the end of the records (erased), -1 = damage;
   *type = -1 for an intact record of a type later firmware added (stepped over, not indexed, dropped by a
   clean-up) */
static int check_record(uint32_t off, uint32_t end, int *type, int *key, uint32_t *seq)
{
    const uint8_t *h = flash_ptr(off, REC_HDR);
    if (!h || off + REC_HDR > end)
        return 0;
    if (blank(h, REC_HDR))
        return 0;
    unsigned len = rd16(h + 6);
    if (rd16(h) != REC_MAGIC || rd16(h + 4) >= WVS_KEYS || off + REC_HDR + len > end)
        return -1;
    const uint8_t *body = flash_ptr(off + REC_HDR, len);
    uint32_t crc = crc32(crc32(0, h, 12), body, len);
    if (crc != rd32(h + 12))
        return -1;
    *type = h[2] < WVS_TYPES ? h[2] : -1;
    *key = rd16(h + 4);
    *seq = rd32(h + 8);
    return (int)round_up(REC_HDR + len);
}

static void scan_sector(int s)
{
    uint32_t base = sector_off(s), end = base + FLASH_SECTOR_SIZE, off = base + SECTOR_HDR;
    for (;;) {
        int type, key;
        uint32_t seq;
        int size = check_record(off, end, &type, &key, &seq);
        if (size == 0)
            break;
        if (size < 0) {                     /* cut short or damaged: append nothing more here */
            off = end;
            break;
        }
        struct entry *e = type >= 0 ? &st.index[type][key] : 0;
        if (e && (!e->addr || seq > e->seq)) {
            e->addr = off;
            e->seq = seq;
        }
        if (seq >= st.next_seq)
            st.next_seq = seq + 1;
        off += (uint32_t)size;
    }
    if (off + REC_HDR > end)
        off = end;
    st.fill[s] = (uint16_t)(off - base);
}

void wvs_open(void)
{
    memset(&st, 0, sizeof st);
    st.head = -1;
    st.next_seq = 1;
    st.next_sseq = 1;
    for (int s = 0; s < SECTORS; s++) {
        const uint8_t *h = flash_ptr(sector_off(s), FLASH_SECTOR_SIZE);
        if (!h || blank(h, FLASH_SECTOR_SIZE)) {
            st.state[s] = S_FREE;
            continue;
        }
        if (rd32(h) != SECTOR_MAGIC || rd32(h + 8) != ~rd32(h + 4)) {
            st.state[s] = S_DIRTY;
            continue;
        }
        st.state[s] = S_USED;
        st.sseq[s] = rd32(h + 4);
        if (st.sseq[s] >= st.next_sseq)
            st.next_sseq = st.sseq[s] + 1;
        if (st.head < 0 || st.sseq[s] > st.sseq[st.head])
            st.head = s;
        scan_sector(s);
    }
}

const uint8_t *wvs_get(int type, int key, unsigned *len)
{
    if (type < 0 || type >= WVS_TYPES || key < 0 || key >= WVS_KEYS || !st.index[type][key].addr)
        return 0;
    const uint8_t *h = flash_ptr(st.index[type][key].addr, REC_HDR);
    *len = rd16(h + 6);
    return flash_ptr(st.index[type][key].addr + REC_HDR, *len);
}

void wvs_forget(int type, int key)
{
    if (type >= 0 && type < WVS_TYPES && key >= 0 && key < WVS_KEYS)
        st.index[type][key].addr = 0;
}

/* ---------------------------------------------------------------- writing */

static int count_free(void)
{
    int n = 0;
    for (int s = 0; s < SECTORS; s++)
        n += st.state[s] != S_USED;
    return n;
}

/* a fresh sector becomes the head */
static int open_sector(void)
{
    int pick = -1;
    for (int s = 0; s < SECTORS && pick < 0; s++)          /* an erased one first */
        if (st.state[s] == S_FREE)
            pick = s;
    for (int s = 0; s < SECTORS && pick < 0; s++)
        if (st.state[s] == S_DIRTY)
            pick = s;
    if (pick < 0)
        return -2;
    uint32_t off = sector_off(pick);
    const uint8_t *now = flash_ptr(off, FLASH_SECTOR_SIZE);
    if (!blank(now, FLASH_SECTOR_SIZE) && flash_erase_sector(off) != 0)
        return -3;
    uint8_t h[SECTOR_HDR];
    uint32_t seq = st.next_sseq++;
    memset(h, 0xff, sizeof h);
    put32le(h, SECTOR_MAGIC);
    put32le(h + 4, seq);
    put32le(h + 8, ~seq);
    if (flash_program(off, h, sizeof h) != 0) {
        st.state[pick] = S_DIRTY;
        return -3;
    }
    st.state[pick] = S_USED;
    st.sseq[pick] = seq;
    st.fill[pick] = SECTOR_HDR;
    st.head = pick;
    return 0;
}

/* the record in st.buf (header filled, `size` bytes with the alignment) at the head; its flash offset */
static int append(unsigned size, uint32_t *at)
{
    if (st.head < 0 || st.fill[st.head] + size > FLASH_SECTOR_SIZE) {
        int err = open_sector();
        if (err)
            return err;
    }
    uint32_t off = sector_off(st.head) + st.fill[st.head];
    unsigned len = REC_HDR + rd16(st.buf + 6);
    st.fill[st.head] = (uint16_t)(st.fill[st.head] + size);
    if (flash_program(off, st.buf, len) != 0) {
        st.fill[st.head] = FLASH_SECTOR_SIZE;               /* close it: the next record goes elsewhere */
        return -3;
    }
    *at = off;
    return 0;
}

static int live(uint32_t off, int type, int key)
{
    return type >= 0 && st.index[type][key].addr == off;
}

/* copy the live records of the oldest sector to the head and erase it */
static int clean_one(void)
{
    int victim = -1;
    for (int s = 0; s < SECTORS; s++)
        if (st.state[s] == S_USED && s != st.head && (victim < 0 || st.sseq[s] < st.sseq[victim]))
            victim = s;
    if (victim < 0)
        return -2;
    uint32_t base = sector_off(victim), end = base + FLASH_SECTOR_SIZE;
    for (uint32_t off = base + SECTOR_HDR; off < base + st.fill[victim];) {
        int type, key;
        uint32_t seq;
        int size = check_record(off, end, &type, &key, &seq);
        if (size <= 0)
            break;
        if (live(off, type, key)) {
            memcpy(st.buf, flash_ptr(off, (unsigned)size), (unsigned)size);   /* same seq: still the newest */
            uint32_t at;
            int err = append((unsigned)size, &at);
            if (err)
                return err;
            st.index[type][key].addr = at;
        }
        off += (uint32_t)size;
    }
    if (flash_erase_sector(base) != 0) {
        st.state[victim] = S_DIRTY;
        return -3;
    }
    st.state[victim] = S_FREE;
    return 0;
}

int wvs_put(int type, int key, const void *data, unsigned len)
{
    if (type < 0 || type >= WVS_TYPES || key < 0 || key >= WVS_KEYS || len > WVS_PAYLOAD_MAX)
        return -1;
    unsigned size = round_up(REC_HDR + len);
    /* everything live plus this record must fit the sectors outside the reserve and the head (the record it
       replaces goes). Records do not cross sectors, so with records of at most m bytes a full sector holds at
       least max(room - m, m) */
    unsigned live = 0, m = size, room = FLASH_SECTOR_SIZE - SECTOR_HDR;
    for (int t = 0; t < WVS_TYPES; t++)
        for (int k = 0; k < WVS_KEYS; k++)
            if (st.index[t][k].addr && !(t == type && k == key)) {
                unsigned r = round_up(REC_HDR + rd16(flash_ptr(st.index[t][k].addr, REC_HDR) + 6));
                live += r;
                m = r > m ? r : m;
            }
    unsigned per = room - m > m ? room - m : m;
    if (live + size > (unsigned)(SECTORS - RESERVE - 1) * per)
        return -2;
    /* room for the record without taking the last free sectors, which the clean-up needs */
    for (int tries = 0; st.head < 0 || st.fill[st.head] + size > FLASH_SECTOR_SIZE; tries++) {
        if (count_free() > RESERVE)
            break;
        if (tries >= SECTORS)
            return -2;                                      /* everything is live: full */
        int err = clean_one();
        if (err)
            return err;
    }
    uint8_t *h = st.buf;
    uint32_t seq = st.next_seq++;
    put16le(h, REC_MAGIC);
    h[2] = (uint8_t)type;
    h[3] = 0;
    put16le(h + 4, (uint16_t)key);
    put16le(h + 6, (uint16_t)len);
    put32le(h + 8, seq);
    memcpy(h + REC_HDR, data, len);
    put32le(h + 12, crc32(crc32(0, h, 12), h + REC_HDR, len));
    uint32_t at;
    int err = append(size, &at);
    if (err)
        return err;
    st.index[type][key].addr = at;
    st.index[type][key].seq = seq;
    return 0;
}

void wvs_stats(struct wvs_stats *out)
{
    memset(out, 0, sizeof *out);
    out->sectors = SECTORS;
    for (int s = 0; s < SECTORS; s++)
        out->used += st.state[s] == S_USED;
    out->free = SECTORS - out->used;
    for (int t = 0; t < WVS_TYPES; t++)
        for (int k = 0; k < WVS_KEYS; k++)
            if (st.index[t][k].addr)
                out->live_bytes += REC_HDR + rd16(flash_ptr(st.index[t][k].addr, REC_HDR) + 6);
}
