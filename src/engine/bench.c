#include "engine/bench.h"
#include "dsp/fmath.h"
#include "engine/audio_engine.h"
#include "engine/host_abi.h"
#include "engine/pools.h"
#include "fw/crc32.h"
#include "platform/audio.h"
#include "platform/cpu.h"
#include "platform/safety.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

static uint8_t elf[BENCH_ELF_MAX] SDRAM_BSS __attribute__((aligned(8)));
static uint32_t elf_total, elf_have;        /* the upload's size; bytes received in order */
static struct chain bc SDRAM_BSS;
static uint32_t cycles[BENCH_MAX_BLOCKS] SDRAM_BSS;
static uint32_t sorted[BENCH_MAX_BLOCKS] SDRAM_BSS;

int bench_elf_write(uint32_t offset, uint32_t total, const uint8_t *data, uint32_t n)
{
    if (total == 0 || total > BENCH_ELF_MAX || offset > total || n > total - offset)
        return BENCH_BAD_REQUEST;
    if (offset == 0)
        elf_have = 0;
    if (offset != elf_have)                 /* in order, from 0 */
        return BENCH_BAD_REQUEST;
    memcpy(elf + offset, data, n);
    elf_have = offset + n;
    elf_total = total;
    return 0;
}

/* ---------------------------------------------------------------- the test signal */

#define GEN_HARMONICS 6
#define GEN_NOTE      (AUDIO_RATE / 2)      /* a pluck every 0.5 s */

struct gen {
    uint32_t n, rng;
    float f0, env, gain;
    float phase[GEN_HARMONICS];
};

static uint32_t rnd(struct gen *g)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng;
}

static void gen_init(struct gen *g, float level, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1u;
    float sum = 0.0f;
    for (int h = 1; h <= GEN_HARMONICS; h++)
        sum += 1.0f / (float)h;
    g->gain = level / sum;
}

/* mono guitar-like signal: E2..E4 plucks of decaying harmonics, plus noise 60 dB under the level */
static float gen_next(struct gen *g)
{
    if (g->n % GEN_NOTE == 0) {
        g->f0 = 82.40689f * fm_pow10((float)((rnd(g) >> 8) % 25u) * 0.025085832f);    /* E2 x 2^(k/12) */
        g->env = 1.0f;
        for (int h = 0; h < GEN_HARMONICS; h++)
            g->phase[h] = 0.0f;
    }
    g->n++;
    float x = 0.0f;
    for (int h = 0; h < GEN_HARMONICS; h++) {
        g->phase[h] += g->f0 * (float)(h + 1) * (1.0f / (float)AUDIO_RATE);
        if (g->phase[h] >= 1.0f)
            g->phase[h] -= 1.0f;
        x += sinf(6.2831853f * g->phase[h]) / (float)(h + 1);
    }
    g->env *= 0.99985f;
    float noise = ((float)(rnd(g) >> 9) * (1.0f / 8388608.0f) - 0.5f) * 0.002f;
    return g->gain * (x * g->env + noise);
}

/* ---------------------------------------------------------------- the measurement */

static void sort(uint32_t *a, int n)
{
    for (int gap = n / 2; gap > 0; gap /= 2)            /* Shell sort: a few thousand values */
        for (int i = gap; i < n; i++) {
            uint32_t v = a[i];
            int j = i;
            for (; j >= gap && a[j - gap] > v; j -= gap)
                a[j] = a[j - gap];
            a[j] = v;
        }
}

static uint32_t empty_overhead(void)
{
    uint32_t best = 0xffffffffu;
    for (int k = 0; k < 16; k++) {
        uint32_t irq = cpu_irq_disable();
        uint32_t c0 = time_cycles();
        uint32_t c1 = time_cycles();
        cpu_irq_restore(irq);
        if (c1 - c0 < best)
            best = c1 - c0;
    }
    return best;
}

void bench_run(const struct bench_request *rq, const struct catalog *cat, const struct chain *running,
               uint32_t core_hz, struct bench_result *out)
{
    memset(out, 0, sizeof *out);
    out->core_hz = core_hz;
    out->cycles = cycles;
    if (rq->blocks == 0 || rq->blocks > BENCH_MAX_BLOCKS || rq->skip > 100000u || rq->menu < 0 ||
        rq->source > BENCH_ELF) {
        out->status = BENCH_BAD_REQUEST;
        return;
    }
    if (rq->source == BENCH_ELF && (elf_total == 0 || elf_have != elf_total)) {
        out->status = BENCH_NO_ELF;
        return;
    }

    struct preset_slot ps = { .on = (rq->flags & BENCH_BYPASS) ? 0 : 1, .menu = (int8_t)rq->menu,
                              .index = (int16_t)rq->index, .id = 0 };
    memcpy(ps.knobs, rq->knobs, sizeof ps.knobs);
    struct catalog_item item;
    if (rq->source == BENCH_CATALOG && catalog_item(cat, rq->menu, rq->index, &item) == 0) {
        ps.id = item.id;
        for (int k = 0; k < item.nparams; k++) {            /* NaN: the item's default */
            struct lib_param d;
            if (catalog_param(&item, k, &d) == 0 && d.idx < PRESET_KNOBS && ps.knobs[d.idx] != ps.knobs[d.idx])
                ps.knobs[d.idx] = d.def;
        }
    }
    for (int k = 0; k < PRESET_KNOBS; k++)
        if (ps.knobs[k] != ps.knobs[k])
            ps.knobs[k] = 0.0f;
    memcpy(out->knobs, ps.knobs, sizeof out->knobs);
    int side = !running->side;

    engine_suspend();                       /* the interrupt leaves the chains alone (and cuts any tail) */
    uint32_t headers[PRESET_SLOTS];
    for (int i = 0; i < PRESET_SLOTS; i++)
        headers[i] = ABI_MODULE_HEADER[i];

    safety_feed();
    uint32_t t0 = time_cycles();
    out->status = chain_load_one(&bc, cat, &ps, rq->source == BENCH_ELF ? elf : 0, elf_total, rq->bpm, side);
    out->load_cycles = time_cycles() - t0;
    for (int i = 0; i < POOL_COUNT; i++)
        out->pool_used[i] = side ? pools[i].top : pools[i].used;

    if (out->status == MODULE_OK) {
        static float io[2 * AUDIO_FRAMES], aux[2 * AUDIO_FRAMES], send[2 * AUDIO_FRAMES], ret[2 * AUDIO_FRAMES];
        float *extra[2] = { send, ret };
        struct gen g;
        gen_init(&g, rq->level, rq->seed);
        memset(ret, 0, sizeof ret);
        out->overhead = empty_overhead();
        uint32_t crc = 0, total = rq->skip + rq->blocks;
        float peak = 0.0f;
        uint64_t sum = 0;
        for (uint32_t b = 0; b < total; b++) {
            for (int f = 0; f < AUDIO_FRAMES; f++) {
                float x = gen_next(&g);
                io[2 * f] = io[2 * f + 1] = aux[2 * f] = aux[2 * f + 1] = x;
            }
            memset(send, 0, sizeof send);
            if (rq->flags & BENCH_COLD)
                cpu_dcache_flush();
            uint32_t irq = cpu_irq_disable();
            chain_run_slot(&bc, 0, io, aux, extra, AUDIO_FRAMES);     /* times the process (cyc_last) */
            cpu_irq_restore(irq);
            crc = crc32(crc, (const uint8_t *)io, sizeof io);
            crc = crc32(crc, (const uint8_t *)send, sizeof send);
            for (int k = 0; k < 2 * AUDIO_FRAMES; k++) {
                float a = io[k] < 0.0f ? -io[k] : io[k];
                if (a > peak)
                    peak = a;
            }
            if (b >= rq->skip) {
                uint32_t c = bc.slot[0].cyc_last, ov = out->overhead;
                c = c > ov ? c - ov : 0;
                cycles[b - rq->skip] = c;
                sum += c;
            }
            safety_feed();
        }
        safety_slot_audio = SAFETY_NONE;
        int n = (int)rq->blocks;
        memcpy(sorted, cycles, sizeof(uint32_t) * (unsigned)n);
        sort(sorted, n);
        out->blocks = (uint32_t)n;
        out->min = sorted[0];
        out->median = sorted[n / 2];
        out->p99 = sorted[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1];
        out->max = sorted[n - 1];
        out->mean = (float)sum / (float)n;
        out->crc = crc;
        out->peak = peak;
    }

    pools_use(side);                        /* the slot's side emptied again; the running chain's kept */
    for (int i = 0; i < PRESET_SLOTS; i++)
        ABI_MODULE_HEADER[i] = headers[i];
    engine_resume();
}
