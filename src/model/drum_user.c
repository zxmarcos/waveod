#include "model/drum_user.h"
#include "engine/drums.h"
#include "fw/bytes.h"
#include "fw/wv_store.h"
#include "model/globals.h"
#include "model/settings.h"
#include "platform/sdram.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

const uint8_t *drum_user_record(int slot, unsigned *n)
{
    if (slot < 0 || slot >= DRUM_USER_SLOTS)
        return 0;
    const uint8_t *p = wvs_get(WVS_DRUM, slot, n);
    return p && *n ? p : 0;
}

int drum_user_check(const uint8_t *rec, unsigned n)
{
    if (!rec || n < DRUM_USER_HEADER + 2 || rec[0] != DRUM_USER_VERSION)
        return -1;
    unsigned count = rd16(rec + 20);
    if (count == 0 || count > DRUM_USER_EVENTS || n != DRUM_USER_HEADER + 2 * count)
        return -1;
    uint16_t last = rd16(rec + DRUM_USER_HEADER + 2 * (count - 1));
    return last == DRUM_LOOP || last == DRUM_END ? 0 : -1;
}

int drum_user_info(int slot, struct drum_user_info *out)
{
    unsigned n;
    const uint8_t *p = drum_user_record(slot, &n);
    memset(out, 0, sizeof *out);
    if (!p || drum_user_check(p, n) != 0)
        return -1;
    out->flags = p[1];
    out->bpm = rd16(p + 2);
    out->count = rd16(p + 20);
    memcpy(out->name, p + 4, DRUM_USER_NAME);
    out->name[DRUM_USER_NAME] = 0;
    return 0;
}

int drum_user_store(int slot, const uint8_t *rec, unsigned n)
{
    if (slot < 0 || slot >= DRUM_USER_SLOTS)
        return -10;
    if (n && drum_user_check(rec, n) != 0)
        return -12;
    int err = wvs_put(WVS_DRUM, slot, rec, n);
    return err ? err - 20 : 0;
}

int drum_user_play(int slot)
{
    static uint16_t ev[DRUM_USER_EVENTS] SDRAM_BSS;
    unsigned n;
    const uint8_t *p = drum_user_record(slot, &n);
    if (!p || drum_user_check(p, n) != 0)
        return -1;
    unsigned count = rd16(p + 20);
    for (unsigned i = 0; i < count; i++)
        ev[i] = (uint16_t)rd16(p + DRUM_USER_HEADER + 2 * i);
    return drums_set_events(ev, (int)count) ? 0 : -1;
}

/* ---------------------------------------------------------------- the choice */

static struct settings *cfg;
static struct globals *glb;

void drum_user_init(struct settings *s, struct globals *g)
{
    cfg = s;
    glb = g;
    int slot = s->drum_user - 1;
    if (slot >= 0 && drum_user_play(slot) != 0)
        s->drum_user = 0;                   /* emptied meanwhile: the bank's, stored with the next change */
}

int drum_user_selected(void)
{
    return cfg && cfg->drum_user ? cfg->drum_user - 1 : -1;
}

int drum_user_select(int slot)
{
    struct drum_user_info info;
    if (!cfg || drum_user_info(slot, &info) != 0 || drum_user_play(slot) != 0)
        return -1;
    if (cfg->drum_user != slot + 1) {
        cfg->drum_user = (uint8_t)(slot + 1);
        settings_touch();
    }
    if (info.bpm >= 40 && info.bpm <= 300 && glb && !glb->drum_sync) {
        glb->drum_bpm = info.bpm;
        drums_set_bpm(info.bpm);
        globals_touch();
    }
    return 0;
}

void drum_user_deselect(void)
{
    drums_use_bank();
    if (cfg && cfg->drum_user) {
        cfg->drum_user = 0;
        settings_touch();
    }
}

int drum_user_step(int slot, int d)
{
    struct drum_user_info info;
    int dir = d < 0 ? -1 : 1, steps = d < 0 ? -d : d, at = slot < 0 ? (dir > 0 ? -1 : 0) : slot, found = -1;
    for (int k = 0; k < steps; k++) {
        int next = -1;
        for (int i = 1; i <= DRUM_USER_SLOTS; i++) {
            int s = ((at + dir * i) % DRUM_USER_SLOTS + DRUM_USER_SLOTS) % DRUM_USER_SLOTS;
            if (drum_user_info(s, &info) == 0) {
                next = s;
                break;
            }
        }
        if (next < 0)
            return found;
        found = at = next;
    }
    return found;
}

void drum_user_changed(int slot)
{
    if (slot != drum_user_selected())
        return;
    if (drum_user_play(slot) != 0)
        drum_user_deselect();
}
