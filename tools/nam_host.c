/*
 * The NAM engine and its Clone block built for the PC, for tools/check_nam.py.
 *
 *   nam_host net CAPTURE IN.f32 OUT.f32 [BLOCK]   the network alone (a2_init + a2_process in blocks of BLOCK, 64)
 *   nam_host block CAPTURE CALLS OUT              the block (engine/nam_block.c) replaying the calls the emulator
 *                                                 recorded: CALLS = u32 count, f32 init knobs[15], then per call
 *                                                 {u16 frames, u8 enabled, u8 0, f32 knobs[15], f32 io[2 x frames]};
 *                                                 OUT = the io of every call after it
 * CAPTURE is a Clone slot's bytes (studio/waveod_studio/nam.py).
 */
#include "engine/pools.h"
#include "nam/a2.h"
#include "nam/capture.h"
#include "sdk/wvfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc nam_block;

static unsigned char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = calloc(1, (size_t)*n + 0x2000);
    if (fread(p, 1, (size_t)*n, f) != (size_t)*n)
        exit(2);
    fclose(f);
    return p;
}

static int net(const unsigned char *cap, const char *in, const char *out, int block)
{
    static struct a2 s;
    static float dw[A2_DW_FLOATS], lin[A2_LIN_FLOATS];
    a2_init(&s, dw, lin, (const float *)(cap + NAM_CAPTURE_HEADER));
    long n;
    float *x = (float *)slurp(in, &n);
    n /= 4;
    float *y = malloc((size_t)n * 4);
    for (long t = 0; t < n; t += block)
        a2_process(&s, x + t, y + t, (int)(n - t < block ? n - t : block));
    FILE *f = fopen(out, "wb");
    fwrite(y, 4, (size_t)n, f);
    fclose(f);
    return 0;
}

static int block(const unsigned char *cap, const char *calls, const char *out)
{
    long n;
    unsigned char *c = slurp(calls, &n), *p = c;
    uint32_t count;
    float knobs[WVFX_KNOBS], io[2 * WVFX_MAX_FRAMES];
    memcpy(&count, p, 4);
    memcpy(knobs, p + 4, sizeof knobs);
    p += 4 + sizeof knobs;
    pools_init();
    pools_reset();
    void *state = calloc(1, nam_block.state_size);
    struct wvfx_env env = {WVFX_ABI, 44100.0f, WVFX_MAX_FRAMES, 0, 0, cap, 0x2000};
    if (nam_block.init(state, 0, &env, knobs) != 0) {
        fprintf(stderr, "init refused the capture\n");
        return 1;
    }
    FILE *f = fopen(out, "wb");
    for (uint32_t i = 0; i < count; i++) {
        uint16_t frames;
        memcpy(&frames, p, 2);
        uint8_t enabled = p[2];
        memcpy(knobs, p + 4, sizeof knobs);
        memcpy(io, p + 4 + sizeof knobs, (size_t)frames * 8);
        p += 4 + sizeof knobs + (size_t)frames * 8;
        struct wvfx_io b = {io, io, 0, 0, knobs, 500, frames, enabled, 0};
        nam_block.process(state, &b);
        fwrite(io, 8, frames, f);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && strcmp(argv[1], "net") == 0) {
        long n;
        return net(slurp(argv[2], &n), argv[3], argv[4], argc > 5 ? atoi(argv[5]) : 64);
    }
    if (argc == 5 && strcmp(argv[1], "block") == 0) {
        long n;
        return block(slurp(argv[2], &n), argv[3], argv[4]);
    }
    fprintf(stderr, "usage: nam_host net CAPTURE IN.f32 OUT.f32 [BLOCK] | block CAPTURE CALLS OUT\n");
    return 2;
}
