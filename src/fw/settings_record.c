#include "fw/settings_record.h"
#include "fw/bytes.h"
#include "fw/flash_map.h"
#include <string.h>

#define FIRST_FREE  242                     /* the default map uses records 2..241 */
#define TAG         "WVOD"
#define HEADER      8                       /* tag, u16 version, u16 length */
#define VERSION     1

/* the highest record the preset map does not point at */
static int record_number(const struct preset_source *src)
{
    for (int r = STORE_MAX_RECORDS - 1; r >= FIRST_FREE; r--) {
        int used = 0;
        for (int i = 0; i < PRESET_COUNT && !used; i++)
            used = src->map[i] == r;
        if (!used)
            return r;
    }
    return -1;
}

int settings_record_read(const struct preset_source *src, void *out, unsigned len)
{
    memset(out, 0, len);
    int r = record_number(src);
    const uint8_t *rec = src->has_store && r >= 0 ? store_record(&src->store, (unsigned)r) : 0;
    if (!rec || memcmp(rec, TAG, 4) != 0 || rd16(rec + 4) != VERSION)
        return -1;
    unsigned stored = rd16(rec + 6);
    if (stored > SETTINGS_RECORD_MAX)
        return -1;
    memcpy(out, rec + HEADER, stored < len ? stored : len);
    return (int)stored;
}

int settings_record_write(struct preset_source *src, const void *data, unsigned len)
{
    uint8_t rec[HEADER + SETTINGS_RECORD_MAX];
    int r = record_number(src);
    if (r < 0 || len > SETTINGS_RECORD_MAX)
        return -1;
    if (!src->has_store) {
        if (store_format(&src->store, FLASH_RECORD_STORE, FLASH_RECORD_STORE_SIZE))
            return -1;
        src->has_store = 1;
    }
    memcpy(rec, TAG, 4);
    rec[4] = VERSION;
    rec[5] = 0;
    rec[6] = (uint8_t)len;
    rec[7] = (uint8_t)(len >> 8);
    memcpy(rec + HEADER, data, len);
    return store_write(&src->store, (unsigned)r, rec, HEADER + len);
}
