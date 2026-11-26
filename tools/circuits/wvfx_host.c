/* A waveod block on the PC (tools/check_drives.py): runs the block's process over a stereo float32 file at 44.1 kHz,
 * 64 frames a block, calling poll after every block as the main loop does.
 *
 *   clang -O2 -march=native -ffp-contract=off -I src -DWVFX_SRC='"../../src/blocks/dark_muff.c"'
 *         -DWVFX_DESC=dark_muff_block -o x.exe tools/circuits/wvfx_host.c       (tools/wvbuild.py host_compile)
 *   x.exe in.f32 out.f32 [k0 k1 ...]          knobs as on the pedal; the rest at their defaults
 *
 * Environment: WVFX_WORDS="w0,w1,..." sets the knob words by index (as the original's module gets them; over the
 * arguments); WVFX_KNOBS_AT="block:k=v,..." changes knobs at a block (several separated by ';');
 * WVFX_BYPASS="from-to" bypasses blocks from..to-1; WVFX_DATA=FILE gives init the item's data (env.data).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include WVFX_SRC

#define FRAMES 64

int main(int argc, char **argv)
{
    const struct wvfx_desc *d = &WVFX_DESC;
    if (argc < 3) { fprintf(stderr, "usage: %s in.f32 out.f32 [knobs...]\n", argv[0]); return 1; }
    float k[WVFX_KNOBS] = { 0 };
    for (int i = 0; i < d->nparams; i++)
        k[d->params[i].knob] = d->params[i].def;
    for (int i = 3; i < argc && i - 3 < d->nparams; i++)
        k[d->params[i - 3].knob] = (float)atof(argv[i]);
    const char *words = getenv("WVFX_WORDS");                  /* "w0,w1,...": the knob words as they are */
    for (int i = 0; words && *words && i < WVFX_KNOBS; i++) {
        k[i] = strtof(words, (char **)&words);
        if (*words == ',') words++;
    }
    void *state = calloc(1, d->state_size ? d->state_size : 1), *buffer = calloc(1, d->buffer_size ? d->buffer_size : 1);
    struct wvfx_env env = { WVFX_ABI, 44100.0f, FRAMES, 0, 0, 0, 0 };
    const char *data_path = getenv("WVFX_DATA");               /* the item's data (a Clone capture, an IR) */
    static uint8_t data[0x10000];
    if (data_path && *data_path) {
        FILE *fd = fopen(data_path, "rb");
        if (!fd) { perror(data_path); return 1; }
        env.data = data;
        env.data_size = (uint32_t)fread(data, 1, sizeof data, fd);
        fclose(fd);
    }
    if (d->init(state, buffer, &env, k)) { fprintf(stderr, "init failed\n"); return 1; }
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    if (!fi || !fo) { perror("open"); return 1; }
    const char *at = getenv("WVFX_KNOBS_AT"), *byp = getenv("WVFX_BYPASS");
    long b0 = -1, b1 = -1;
    if (byp) sscanf(byp, "%ld-%ld", &b0, &b1);
    float buf[2 * FRAMES], aux[2 * FRAMES], send[2 * FRAMES], ret[2 * FRAMES] = { 0 };
    size_t got;
    for (long blk = 0; (got = fread(buf, sizeof(float), 2 * FRAMES, fi)) > 0; blk++) {
        for (size_t i = got; i < 2 * FRAMES; i++) buf[i] = 0.0f;
        for (const char *p = at; p && *p;) {                /* "block:k=v,k=v;block:..." */
            long when = strtol(p, (char **)&p, 10);
            if (*p == ':') p++;
            while (*p && *p != ';') {
                int idx = (int)strtol(p, (char **)&p, 10);
                if (*p == '=') p++;
                float v = strtof(p, (char **)&p);
                if (when == blk && idx >= 0 && idx < WVFX_KNOBS) k[idx] = v;
                if (*p == ',') p++;
            }
            if (*p == ';') p++;
        }
        memcpy(aux, buf, sizeof aux);
        struct wvfx_io io = { buf, aux, send, ret, k, 500, FRAMES, (uint8_t)!(blk >= b0 && blk < b1), 0 };
        d->process(state, &io);
        if (d->poll) d->poll(state, k);
        fwrite(buf, sizeof(float), got, fo);
    }
    fclose(fi); fclose(fo);
    return 0;
}
