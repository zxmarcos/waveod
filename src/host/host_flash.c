/* Simulator: flash image built from a firmware update file. */
#include "fw/bytes.h"
#include "host/host.h"
#include "platform/flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint8_t *flash;

const uint8_t *flash_ptr(uint32_t offset, uint32_t len)
{
    if (!flash || offset > FLASH_SIZE || len > FLASH_SIZE - offset)
        return 0;
    return flash + offset;
}

uint32_t flash_device_addr(uint32_t offset)
{
    return 0x60000000u + offset;
}

/* same rules as the pedal: only the writable range, NOR semantics (erase = 0xFF, program = AND) */
static int writable(uint32_t offset, uint32_t len)
{
    return flash && flash_writable(offset, len);
}

uint32_t host_flash_erases, host_flash_programs;

int flash_erase_sector(uint32_t offset)
{
    if (!writable(offset, FLASH_SECTOR_SIZE) || (offset & (FLASH_SECTOR_SIZE - 1)))
        return -1;
    memset(flash + offset, 0xff, FLASH_SECTOR_SIZE);
    host_flash_erases++;
    return 0;
}

int flash_program(uint32_t offset, const void *data, uint32_t len)
{
    if (!writable(offset, len))
        return -1;
    const uint8_t *src = data;
    for (uint32_t i = 0; i < len; i++)
        flash[offset + i] &= src[i];
    host_flash_programs++;
    return memcmp(flash + offset, src, len) == 0 ? 0 : -1;
}

void flash_diag(char *out, unsigned size)
{
    snprintf(out, size, "simulador: %u apagamentos, %u gravações", (unsigned)host_flash_erases,
             (unsigned)host_flash_programs);
}

void flash_status(struct flash_status *out)
{
    out->jedec = 0;
    for (int i = 0; i < 3; i++)
        out->sr[i] = out->sr_now[i] = -1;
    out->lock = FLASH_LOCK_UNSUPPORTED;
}

void flash_read_status(int sr[3])
{
    sr[0] = sr[1] = sr[2] = -1;
}

int flash_lock(uint8_t protect)
{
    (void)protect;
    return FLASH_LOCK_UNSUPPORTED;
}

int flash_lock_state(void)
{
    return FLASH_LOCK_UNSUPPORTED;
}

uint8_t *host_flash_image(void)
{
    if (!flash) {
        flash = malloc(FLASH_SIZE);
        memset(flash, 0xff, FLASH_SIZE);
    }
    return flash;
}

/* FRMW container: sections 1 (effects), 2 (IRs) and 3 (factory presets) go where the pedal keeps them;
   the record store stays erased, as on a pedal that never saved anything. */
/* The drum pack tools/drumpack.py builds, here from the original application's SDRAM image (V1.2.2 banks at
   0x8001AF78 and 0x800BC310), for the simulator. */
static uint8_t *drum_pack;

static void build_drum_pack(const uint8_t *mcu, uint32_t len)
{
    free(drum_pack);
    drum_pack = 0;
    if (len < 0x28 || rd32(mcu + 0x24) != 0x80000000u)
        return;
    uint32_t fa = rd32(mcu + 0x1c), size = rd32(mcu + 0x20);
    if (fa < 0x20000u || fa - 0x20000u + size > len)
        return;
    const uint8_t *img = mcu + fa - 0x20000u;
    const uint32_t s = 0x1AF78u, p = 0xBC310u;
    if (p + 4 + 400 > size || rd32(img + s) != 13 || rd32(img + p) != 100)
        return;
    uint32_t last = rd32(img + s + 4 + 4 * 12), ms = rd32(img + s + 4 + 4 * 13 + 4 * 12);
    uint32_t s_size = last + (ms * 44 >> 6) * 128, p_size = rd32(img + p + 4 + 4 * 99) + 400;
    s_size = (s_size + 3) & ~3u;
    if (s + s_size > size || p + p_size > size)
        return;
    drum_pack = malloc(24 + s_size + p_size);
    if (!drum_pack)
        return;
    uint32_t hdr[6] = { 0x4D524457u, 1, 24, s_size, 24 + s_size, p_size };
    memcpy(drum_pack, hdr, sizeof hdr);
    memcpy(drum_pack + 24, img + s, s_size);
    memcpy(drum_pack + 24 + s_size, img + p, p_size);
}

const void *plat_drum_pack(void)
{
    return drum_pack;
}

int host_flash_load_firmware(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);
    if (size < 0x50 || memcmp(data, "FRMW", 4) != 0) {
        free(data);
        return -1;
    }
    host_flash_image();
    memset(flash, 0xff, FLASH_SIZE);
    build_drum_pack(data + 0x30 + rd32(data + 0x30), rd32(data + 0x34));
    static const uint32_t dest[4] = { 0, 0x480000u, 0x700000u, 0x440000u };
    for (int s = 1; s < 4; s++) {
        uint32_t off = rd32(data + 0x30 + 8 * s), len = rd32(data + 0x34 + 8 * s);
        if (0x30u + off + len <= (uint32_t)size && dest[s] + len <= FLASH_SIZE)
            memcpy(flash + dest[s], data + 0x30 + off, len);
    }
    free(data);
    return 0;
}
