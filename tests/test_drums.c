/*
 * The drum machine with the original's sounds and patterns (taken from the firmware file like tools/mkfw.py).
 *
 * `--drums-apply FW OPS OUT` (for tools/check_drums.py): OPS holds blocks {u32 n, u16 note[n]}; each block's notes
 * are hit, then the voices mixed (drums_mix); OUT gets the 64 int32 of every block.
 */
#include "engine/drums.h"
#include "fw/wv_store.h"
#include "model/drum_names.h"
#include "model/drum_user.h"
#include "model/globals.h"
#include "model/settings.h"
#include "host/host.h"
#include "platform/platform.h"
#include "test.h"
#include <math.h>
#include <string.h>

#define N 64

static void test_drums_user(void);

/* model/settings.c and model/globals.c are not in the tests: what model/drum_user.c calls of them */
static int touched;
void settings_touch(void) { touched++; }
void globals_touch(void) { touched++; }

void test_drums(void)
{
    CHECK(drums_init(plat_drum_pack()) == 0 && drums_available(), "the firmware's drum pack");
    if (!drums_available())
        return;
    CHECK(strcmp(drum_pattern_names[90], "1/4") == 0 && drum_genre_of(90) == 7 && drum_genre_first(1) == 30,
          "names and genres");

    /* "1/4": the click sample (2) at velocity 127 every 120 ticks, 0.5 s at 120 BPM */
    drums_set_pattern(90);
    drums_set_bpm(120);
    drums_set_volume(100);
    drums_play(1);
    static float out[2 * N];
    int onsets[8], n = 0, silent = 100000;
    for (int b = 0; b < 44100 * 21 / 10 / N; b++) {
        memset(out, 0, sizeof out);
        drums_process(out, N);
        for (int f = 0; f < N; f++) {
            if (fabsf(out[2 * f]) > 1e-6f) {
                if (silent > 4000 && n < 8)
                    onsets[n++] = b * N + f;
                silent = 0;
            } else {
                silent++;
            }
        }
    }
    CHECK(n == 5, "five clicks in 2.1 s (%d)", n);
    int ok = n == 5;
    for (int i = 0; i < n && ok; i++)
        ok = onsets[i] >= i * 22050 + N && onsets[i] < i * 22050 + 2 * N + 184;     /* a block late, like the original */
    CHECK(ok, "one every 22050 samples (%d %d %d)", n > 0 ? onsets[0] : -1, n > 1 ? onsets[1] : -1, n > 2 ? onsets[2] : -1);
    drums_play(0);
    test_drums_user();
}

/* onsets (sound after 4000 silent samples) in `seconds` of drums_process */
static int count_onsets(float seconds)
{
    static float out[2 * N];
    int n = 0, silent = 100000;
    for (int b = 0; b < (int)(44100 * seconds) / N; b++) {
        memset(out, 0, sizeof out);
        drums_process(out, N);
        for (int f = 0; f < N; f++) {
            if (fabsf(out[2 * f]) > 1e-6f) {
                n += silent > 4000;
                silent = 0;
            } else {
                silent++;
            }
        }
    }
    return n;
}

/* a record of model/drum_user.h: two clicks a beat apart, then `last` */
static unsigned record(uint8_t *rec, const char *name, uint16_t last)
{
    static const uint16_t ev[4] = { DRUM_HIT | 2 << 8 | 127, 120, DRUM_HIT | 2 << 8 | 127, 120 };
    memset(rec, 0, DRUM_USER_HEADER);
    rec[0] = DRUM_USER_VERSION;
    rec[1] = last == DRUM_END ? DRUM_USER_ONCE : 0;
    rec[2] = 100;                               /* BPM 100 */
    memcpy(rec + 4, name, strlen(name));
    rec[20] = 5;
    for (int i = 0; i < 5; i++) {
        uint16_t e = i < 4 ? ev[i] : last;
        rec[DRUM_USER_HEADER + 2 * i] = (uint8_t)e;
        rec[DRUM_USER_HEADER + 2 * i + 1] = (uint8_t)(e >> 8);
    }
    return DRUM_USER_HEADER + 10;
}

/* MEUS RITMOS: the engine playing a copied pattern once or round, the places in waveod's store, the choice */
static void test_drums_user(void)
{
    static uint8_t rec[DRUM_USER_HEADER + 10];
    uint16_t ev[5];
    unsigned n = record(rec, "TESTE", DRUM_END);
    for (int i = 0; i < 5; i++)
        ev[i] = (uint16_t)(rec[DRUM_USER_HEADER + 2 * i] | rec[DRUM_USER_HEADER + 2 * i + 1] << 8);
    drums_set_bpm(120);
    CHECK(drums_set_events(ev, 5), "a pattern of 5 events");
    drums_play(1);
    int k = count_onsets(3.0f);
    CHECK(k == 2 && !drums_playing(), "once: two clicks, then stopped (%d)", k);
    drums_play(1);
    CHECK(drums_playing() && count_onsets(0.8f) == 2, "started again from its start");
    ev[4] = DRUM_LOOP;
    drums_set_events(ev, 5);
    drums_play(1);
    k = count_onsets(2.1f);
    CHECK(k == 5 && drums_playing(), "round: five clicks in 2.1 s (%d)", k);
    drums_use_bank();                           /* "1/4" again */
    drums_play(1);
    CHECK(count_onsets(1.1f) == 3, "the bank's pattern back");
    drums_play(0);
    CHECK(!drums_set_events(ev, DRUM_USER_EVENTS + 1) && !drums_set_events(ev, 0), "too long / empty refused");

    /* the places */
    wvs_open();
    static struct settings st;
    static struct globals g;
    memset(&st, 0, sizeof st);
    memset(&g, 0, sizeof g);
    drum_user_init(&st, &g);
    CHECK(drum_user_check(rec, n) == 0 && drum_user_check(rec, n - 2) != 0, "record check");
    rec[DRUM_USER_HEADER + 8] = 0;              /* last event no end */
    CHECK(drum_user_check(rec, n) != 0, "a record must end with DRUM_LOOP / DRUM_END");
    record(rec, "TESTE", DRUM_END);
    CHECK(drum_user_store(3, rec, n) == 0 && drum_user_store(DRUM_USER_SLOTS, rec, n) == -10, "stored in place 4");
    record(rec, "VOLTA", DRUM_LOOP);
    CHECK(drum_user_store(7, rec, n) == 0, "stored in place 8");
    struct drum_user_info info;
    CHECK(drum_user_info(3, &info) == 0 && strcmp(info.name, "TESTE") == 0 && info.bpm == 100 && info.count == 5 &&
          (info.flags & DRUM_USER_ONCE), "place 4 read back");
    CHECK(drum_user_info(4, &info) != 0, "place 5 empty");
    CHECK(drum_user_step(-1, 1) == 3 && drum_user_step(3, 1) == 7 && drum_user_step(7, 1) == 3 &&
          drum_user_step(3, -1) == 7, "stepping through the used places");
    CHECK(drum_user_select(4) != 0 && drum_user_selected() < 0, "an empty place is not chosen");
    CHECK(drum_user_select(7) == 0 && drum_user_selected() == 7 && st.drum_user == 8 && g.drum_bpm == 100,
          "place 8 chosen, its tempo taken");
    drums_play(1);
    CHECK(count_onsets(1.3f) == 3, "place 8 plays (100 BPM: 0.6 s a beat)");
    drum_user_store(7, 0, 0);
    drum_user_changed(7);
    CHECK(drum_user_selected() < 0 && drum_user_info(7, &info) != 0 && count_onsets(1.1f) >= 2,
          "emptied while playing: the bank's pattern");
    drums_play(0);
    drum_user_store(3, 0, 0);
}

int drums_apply(const char *fw, const char *ops_path, const char *out_path)
{
    if (host_flash_load_firmware(fw) != 0 || drums_init(plat_drum_pack()) != 0)
        return 1;
    FILE *in = fopen(ops_path, "rb"), *out = fopen(out_path, "wb");
    if (!in || !out)
        return 1;
    uint32_t n;
    while (fread(&n, 4, 1, in) == 1) {
        for (uint32_t i = 0; i < n; i++) {
            uint16_t note;
            if (fread(&note, 2, 1, in) != 1)
                return 1;
            drums_hit(note);
        }
        int32_t acc[64];
        drums_mix(acc);
        fwrite(acc, sizeof acc, 1, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}
