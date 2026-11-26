/* Minimal test helpers. */
#pragma once

#include <stdint.h>
#include <stdio.h>

extern int test_failures;
extern int test_checks;
extern uint32_t test_now;            /* added to time_ms() (test_midi.c), for code that waits */
/* what the fake MIDI port port (0 = TRS) was sent since the last call (test_midi.c), at most max bytes */
int test_midi_out(int port, uint8_t *buf, int max);

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        test_checks++;                                                      \
        if (!(cond)) {                                                      \
            test_failures++;                                                \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);          \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

/* test groups */
void test_formats(void);
void test_store(void);
void test_preset_save(void);
void test_tuner(void);
void test_overload(void);
void test_globals(void);
void test_assign(void);
void test_route(void);
void test_looper(void);
void test_drums(void);
int drums_apply(const char *fw, const char *ops_path, const char *out_path);
int dsp_apply(const char *ops_path, const char *out_path);
int store_apply(const char *in_path, const char *ops_path, const char *out_path);
void test_loader(const char *dump_path);
void test_spill(void);
void test_editor(void);
void test_midi(void);
void test_wvstore(void);
void test_wvpresets(void);
void test_scenes(void);
void test_controls(void);
void test_latency(void);
void test_cabir(void);
void test_blocks(void);
void test_blocks_catalog(void);
void test_mods(void);
void test_drives(void);
void test_chorus(void);
void test_modfx(void);
void test_delay(void);
void test_reverb(void);
void test_plate(void);
void test_nam(void);
void test_spectrum(void);
void test_mods_rig(void);
int cab_apply(const char *ops_path, const char *out_path);
int irfs_apply(const char *in_path, const char *ops_path, const char *out_path);
