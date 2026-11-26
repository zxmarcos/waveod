/*
 * Protocol 11 of group 0xF: an effect's cost on the CPU (engine/bench.h, docs/bench.md, tools/bench.py).
 *
 *   0x1F00001B bench elf {u32 offset, u32 total <= 64 KB, u32 n <= 4096, bytes} -> {u32 offset, i32 error};
 *                                                           in order from 0, into RAM (never the flash)
 *   0x1F00001C bench    {u32 source (0 the catalog, 1 the ELF sent), i32 menu, i32 index, u32 blocks (1..2000),
 *                        u32 skip, u32 flags (1 cold cache, 2 bypassed), f32 level, u32 seed, u32 bpm,
 *                        f32 knobs[15] (NaN: the catalog item's default)}
 *                    -> {i32 status, u32 load cycles, u32 overhead, u32 blocks, u32 min, median, p99, max,
 *                        f32 mean, u32 CRC, f32 peak, u32 pool bytes[4] (code, constants, state, buffers),
 *                        u32 core Hz, u32 0, f32 knobs[15] (what the slot got), u32 cycles[blocks]}
 *   0x1F00001D profile  {u32 reset}                     -> {u32 core Hz, f32 block ms, f32 worst ms, u32 audio
 *                                                          blocks, u32 n, per used slot in chain order {u8 slot,
 *                                                          i8 menu, u16 0, u32 id, u32 blocks, u32 mean, u32 max,
 *                                                          u32 last}, u32 blocks, u32 m, u32 mean[m]}: the
 *                                                          preset playing, each slot's process timed in the
 *                                                          audio interrupt (cycles), then the mean cycles of
 *                                                          each section of the interrupt (engine_section);
 *                                                          reset: counted afresh from the next block (after
 *                                                          this answer)
 * The bench's answer comes when the measurement is over (a few seconds at most); the audio is silent meanwhile.
 */
#include "editor/server.h"
#include "engine/audio_engine.h"
#include "engine/bench.h"
#include "model/rig.h"
#include "platform/cpu.h"
#include "platform/platform.h"
#include "platform/sdram.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define BENCH_HEAD (68 + 4 * PRESET_KNOBS)
static uint8_t out[BENCH_HEAD + 4 * BENCH_MAX_BLOCKS] SDRAM_BSS;

static void w32(unsigned at, uint32_t v)
{
    out[at] = (uint8_t)v;
    out[at + 1] = (uint8_t)(v >> 8);
    out[at + 2] = (uint8_t)(v >> 16);
    out[at + 3] = (uint8_t)(v >> 24);
}

static float f32(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t u32(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void elf_write(const struct msg *m)
{
    uint32_t off = msg_word(m, 2), total = msg_word(m, 3), n = msg_word(m, 4);
    int err = BENCH_BAD_REQUEST;
    if (n <= 4096 && 20 + n <= m->len)
        err = bench_elf_write(off, total, m->data + 20, n);
    w32(0, off);
    w32(4, (uint32_t)err);
    reply(m->cmd, out, 8);
}

static void run(const struct msg *m)
{
    struct bench_result r;
    if (m->len < 8 + 4 * (9 + PRESET_KNOBS)) {
        memset(out, 0, BENCH_HEAD);
        w32(0, (uint32_t)BENCH_BAD_REQUEST);
        reply(m->cmd, out, BENCH_HEAD);
        return;
    }
    struct bench_request rq = {
        .source = msg_word(m, 2), .menu = (int32_t)msg_word(m, 3), .index = (int32_t)msg_word(m, 4),
        .blocks = msg_word(m, 5), .skip = msg_word(m, 6), .flags = msg_word(m, 7), .level = f32(msg_word(m, 8)),
        .seed = msg_word(m, 9), .bpm = (uint16_t)msg_word(m, 10),
    };
    for (int k = 0; k < PRESET_KNOBS; k++)
        rq.knobs[k] = f32(msg_word(m, 11 + k));
    bench_run(&rq, &srv.rig->cat, &srv.rig->chain, plat_core_hz(), &r);
    uint32_t words[] = { (uint32_t)r.status, r.load_cycles, r.overhead, r.blocks, r.min, r.median, r.p99, r.max,
                         u32(r.mean), r.crc, u32(r.peak), r.pool_used[0], r.pool_used[1], r.pool_used[2],
                         r.pool_used[3], r.core_hz };
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
        w32(4 * i, words[i]);
    w32(64, 0);
    for (int k = 0; k < PRESET_KNOBS; k++)
        w32(68 + 4 * (unsigned)k, u32(r.knobs[k]));
    for (uint32_t b = 0; b < r.blocks; b++)
        w32(BENCH_HEAD + 4 * b, r.cycles[b]);
    reply(m->cmd, out, BENCH_HEAD + 4 * r.blocks);
}

static void profile(const struct msg *m)
{
    struct chain *c = &srv.rig->chain;
    struct engine_stats st;
    engine_stats_peek(&st);
    w32(0, plat_core_hz());
    w32(4, u32(st.block_ms));
    w32(8, u32(st.worst_ms));
    w32(12, st.blocks);
    unsigned at = 20, n = 0;
    for (int pos = 0; pos < c->count && pos < PRESET_SLOTS; pos++) {
        int i = c->order[pos];
        if (i < 0 || i >= PRESET_SLOTS || c->slot[i].menu < 0)
            continue;
        const struct chain_slot *s = &c->slot[i];
        uint32_t irq = cpu_irq_disable();                   /* the interrupt writes them */
        uint32_t blocks = s->cyc_blocks, max = s->cyc_max, last = s->cyc_last;
        uint64_t sum = s->cyc_sum;
        cpu_irq_restore(irq);
        out[at] = (uint8_t)i;
        out[at + 1] = (uint8_t)s->menu;
        out[at + 2] = out[at + 3] = 0;
        w32(at + 4, s->id);
        w32(at + 8, blocks);
        w32(at + 12, blocks ? (uint32_t)(sum / blocks) : 0);
        w32(at + 16, max);
        w32(at + 20, last);
        at += 24;
        n++;
    }
    w32(16, n);
    uint32_t mean[ENGINE_SECTIONS];
    w32(at, engine_sections(mean));
    w32(at + 4, ENGINE_SECTIONS);
    at += 8;
    for (int k = 0; k < ENGINE_SECTIONS; k++, at += 4)
        w32(at, mean[k]);
    reply(m->cmd, out, at);
    if (msg_word(m, 2)) {
        chain_profile_reset(c);
        engine_sections_reset();
    }
}

void msg_waveod_bench(const struct msg *m)
{
    if (m->cmd == CMD_WV_BENCH_ELF)
        elf_write(m);
    else if (m->cmd == CMD_WV_PROFILE)
        profile(m);
    else
        run(m);
}
