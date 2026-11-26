#include "engine/chain.h"
#include "engine/host_abi.h"
#include "engine/module_call.h"
#include "engine/pools.h"
#include "engine/scope.h"
#include "platform/audio.h"
#include "platform/flash.h"
#include "platform/safety.h"
#include "platform/time.h"
#include "sdk/wvfx.h"
#include <string.h>

/* What IR / capture engines read when their slot has no data (the original points at zeros too). */
static uint8_t no_data[0x2400] __attribute__((aligned(8)));

static uint32_t no_data_addr(void)
{
    return (uint32_t)(uintptr_t)no_data;
}


#define IR_BYTES (1024u * 4u)                   /* what a Cab / User IR item's data is (CABIR_LEN int32) */
#define CLONE_BYTES 0x2000u                     /* a Clone item's (fw/ir_fs.h IRFS_CLONE_DATA) */

/* pool memory for a block: device address -> pointer, 0 when the pool is full */
static void *block_alloc(enum pool_id pool, uint32_t size)
{
    uint32_t addr = pool_alloc(pool, size);
    return addr ? pool_ptr(addr, size) : 0;
}

/* a slot on a block of waveod's: its memory in the pools, then its init (the item's data straight from the
   flash: an IR of a Cab / User IR item, a Clone item's capture) */
static void load_block(struct chain_slot *s, int i, const struct wvfx_desc *d, const struct catalog *cat)
{
    s->state = d->state_size ? block_alloc(POOL_STATE, d->state_size) : 0;
    s->buffer = d->buffer_size ? block_alloc(POOL_BUFFER, d->buffer_size) : 0;
    if ((d->state_size && !s->state) || (d->buffer_size && !s->buffer)) {
        s->status = MODULE_NO_MEMORY;
        return;
    }
    struct wvfx_env env = { WVFX_ABI, (float)AUDIO_RATE, AUDIO_FRAMES, (uint8_t)i, 0, 0, 0 };
    if (catalog_is_special(s->menu)) {
        uint32_t off = catalog_data(cat, s->menu, s->index), size = s->menu == MENU_CLONE ? CLONE_BYTES : IR_BYTES;
        env.data = off ? flash_ptr(off, size) : 0;
        env.data_size = env.data ? size : 0;
    }
    safety_feed();
    safety_slot_main = (uint8_t)i;
    int err = d->init(s->state, s->buffer, &env, s->knobs);
    safety_slot_main = SAFETY_NONE;
    s->block = d;
    s->status = err == WVFX_ERR_NO_MEMORY ? MODULE_NO_MEMORY : err ? MODULE_INIT_FAILED : MODULE_OK;
}

static int runnable(const struct chain *c, const struct chain_slot *s)
{
    return s->menu >= 0 && s->status == MODULE_OK && (s->mod.process || s->block) &&
           !(c->skip >> (s - c->slot) & 1u);
}

/* An original IR / Clone module reads its item's data while it plays, from the address it is given (the original
   gives the flash's). A copy in the buffer pool instead, so that the audio never reads the flash and keeps playing
   while the flash is written (presets, settings: engine_flash_begin); without room for it, the flash, and the slot
   is marked. */
static uint32_t data_copy(struct chain *c, int i, uint32_t off, uint32_t size)
{
    const uint8_t *src = flash_ptr(off, size);
    uint32_t addr = src ? pool_alloc(POOL_BUFFER, size) : 0;
    uint8_t *dst = addr ? pool_ptr(addr, size) : 0;
    if (!dst) {
        c->flash_data |= (uint16_t)(1u << i);
        return flash_device_addr(off);
    }
    memcpy(dst, src, size);
    return addr;
}

static void load_module(struct chain *c, int i, const uint8_t *elf, uint32_t elf_size, const struct catalog *cat);

static void load_slot(struct chain *c, int i, const struct catalog *cat)
{
    struct chain_slot *s = &c->slot[i];
    struct catalog_item item;
    s->status = MODULE_BAD_ELF;
    if (catalog_item(cat, s->menu, s->index, &item) != 0)
        return;
    if (item.block) {
        load_block(s, i, item.block, cat);
        return;
    }
    load_module(c, i, item.engine.elf, item.engine.elf_size, cat);
}

/* a module ELF (the original's or a block of waveod's built as one) into slot i */
static void load_module(struct chain *c, int i, const uint8_t *elf, uint32_t elf_size, const struct catalog *cat)
{
    struct chain_slot *s = &c->slot[i];
    s->status = module_load(elf, elf_size, (uint16_t)s->index, &s->mod);
    if (s->status != MODULE_OK)
        return;
    if (s->mod.desc) {                      /* a block of waveod's built as a module */
        ABI_MODULE_HEADER[i] = s->mod.header;
#ifndef WAVEOD_HOST                         /* the simulator cannot run its ARM code: it stays silent there */
        const struct wvfx_desc *d = (const struct wvfx_desc *)(uintptr_t)s->mod.desc;
        if (d->magic != WVFX_MAGIC || d->abi != WVFX_ABI || !d->init || !d->process ||
            !pool_ptr((uint32_t)(uintptr_t)d->process & ~1u, 2) || !pool_ptr((uint32_t)(uintptr_t)d->init & ~1u, 2)) {
            s->status = MODULE_BAD_HEADER;
            return;
        }
        load_block(s, i, d, cat);
#endif
        return;
    }
    if (catalog_is_special(s->menu)) {
        uint32_t off = catalog_data(cat, s->menu, s->index);
        s->ctx = off ? data_copy(c, i, off, s->menu == MENU_CLONE ? CLONE_BYTES : IR_BYTES) : no_data_addr();
    } else {
        s->ctx = c->beat_ms;
    }
    ABI_MODULE_HEADER[i] = s->mod.header;
    safety_feed();                          /* a module's Init may take a while */
    safety_slot_main = (uint8_t)i;          /* a crash record names the slot */
    module_call_init(s->mod.init, s->knobs, &s->ctx);
    safety_slot_main = SAFETY_NONE;
}

void chain_set_tempo(struct chain *c, uint16_t bpm)
{
    c->beat_ms = 60000u / (bpm >= 40 && bpm <= 300 ? bpm : 120u);
}

static int build(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm);

int chain_build(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm)
{
    pools_reset();
    c->side = 0;
    return build(c, cat, p, bpm);
}

int chain_build_side(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm, int side)
{
    pools_use(side);
    c->side = (uint8_t)(side != 0);
    return build(c, cat, p, bpm);
}

int chain_out_of_memory(const struct chain *c)
{
    for (int i = 0; i < PRESET_SLOTS; i++)
        if (c->slot[i].menu >= 0 && c->slot[i].status == MODULE_NO_MEMORY)
            return 1;
    return 0;
}

static int build(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm)
{
    c->skip = c->watch = c->flash_data = 0;
    for (int i = 0; i < CHAIN_RAMPS; i++)
        c->ramp[i].left = 0;
    chain_set_tempo(c, bpm);
    c->mode = ROUTE_SERIAL;                 /* until chain_set_route */
    c->count = p->count;
    memcpy(c->order, p->order, sizeof c->order);
    int failed = 0;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        struct chain_slot *s = &c->slot[i];
        const struct preset_slot *ps = &p->slot[i];
        memset(&s->mod, 0, sizeof s->mod);
        s->block = 0;
        s->state = s->buffer = 0;
        ABI_MODULE_HEADER[i] = 0;
        s->menu = ps->menu;
        s->index = ps->index;
        s->id = ps->id;
        s->enabled = ps->on;
        memcpy(s->knobs, ps->knobs, sizeof s->knobs);
        s->status = MODULE_OK;
        s->cyc_blocks = s->cyc_last = s->cyc_max = 0;
        s->cyc_sum = 0;
        if (s->menu < 0)
            continue;
        load_slot(c, i, cat);
        failed += s->status != MODULE_OK;
    }
    return failed;
}

int chain_load_one(struct chain *c, const struct catalog *cat, const struct preset_slot *ps, const uint8_t *elf,
                   uint32_t elf_size, uint16_t bpm, int side)
{
    pools_use(side);
    memset(c, 0, sizeof *c);
    for (int i = 0; i < PRESET_SLOTS; i++)
        c->slot[i].menu = -1;
    c->side = (uint8_t)(side != 0);
    chain_set_tempo(c, bpm);
    c->mode = ROUTE_SERIAL;
    c->count = 1;
    c->order[0] = 0;
    struct chain_slot *s = &c->slot[0];
    s->menu = ps->menu;
    s->index = ps->index;
    s->id = ps->id;
    s->enabled = ps->on;
    memcpy(s->knobs, ps->knobs, sizeof s->knobs);
    if (elf)
        load_module(c, 0, elf, elf_size, cat);
    else
        load_slot(c, 0, cat);
    return s->status;
}

static void run(struct chain *c, struct chain_slot *s, float *io, float *aux, float **extra, int frames);

static inline void profile(struct chain_slot *s, uint32_t cycles)
{
    s->cyc_last = cycles;
    s->cyc_sum += cycles;
    s->cyc_blocks++;
    if (cycles > s->cyc_max)
        s->cyc_max = cycles;
}

void chain_profile_reset(struct chain *c)
{
    c->profile_reset = 1;
}

void chain_run_slot(struct chain *c, int i, float *io, float *aux, float **extra, int frames)
{
    run(c, &c->slot[i], io, aux, extra, frames);
}

static void run(struct chain *c, struct chain_slot *s, float *io, float *aux, float **extra, int frames)
{
    safety_slot_audio = (uint8_t)(s - c->slot);
    float before[2 * AUDIO_FRAMES];
    int tap = scope_taps(c, (int)(s - c->slot)) && frames <= AUDIO_FRAMES;  /* the spectrum analyser's */
    if (tap)
        memcpy(before, io, sizeof(float) * 2 * (unsigned)frames);
    if (s->block) {
        struct wvfx_io b = { io, aux, extra[0], extra[1], s->knobs, c->beat_ms, (uint16_t)frames, s->enabled, 0 };
        uint32_t t0 = time_cycles();
        s->block->process(s->state, &b);
        profile(s, time_cycles() - t0);
    } else {
        uint32_t t0 = time_cycles();
        module_call_process(s->mod.process, io, (uint16_t)frames, s->knobs, aux, &s->enabled, &c->beat_ms, extra);
        profile(s, time_cycles() - t0);
    }
    if (tap)
        scope_push(before, 2, io, 2, frames);
    if (c->watch >> (s - c->slot) & 1u) {
        float peak = c->watch_peak;
        for (int k = 0; k < 2 * frames; k++) {
            float a = io[k] < 0 ? -io[k] : io[k];
            if (a > peak)
                peak = a;
        }
        c->watch_peak = peak;
    }
}

static float clamp1(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

void chain_set_route(struct chain *c, int mode, const uint8_t lane_n[4], const uint8_t joint[JOINT_PARAMS])
{
    /* balance and pans: 50 = both sides full, towards one end the other side fades out */
    float bal = joint[JOINT_BALANCE], mix = joint[JOINT_MIX] * 0.01f;
    float pa = joint[JOINT_PAN_A], pb = joint[JOINT_PAN_B];
    struct chain_joints j;
    j.in_a = clamp1((100.0f - bal) / 50.0f) * joint[JOINT_LEVEL_A] * 0.01f;
    j.in_b = clamp1(bal / 50.0f) * joint[JOINT_LEVEL_B] * 0.01f;
    j.a_l = (1.0f - mix) * clamp1((100.0f - pa) / 50.0f);
    j.a_r = (1.0f - mix) * clamp1(pa / 50.0f);
    j.b_l = mix * clamp1((100.0f - pb) / 50.0f);
    j.b_r = mix * clamp1(pb / 50.0f);
    c->joints = j;                          /* the interrupt may mix old and new gains for one block */
    for (int i = 0; i < 4; i++)
        c->lane_n[i] = lane_n[i];
    c->mode = (uint8_t)mode;
}

/* one block of a lane: with a `no_cab` mask a Cab / IR is skipped (both sides) or the side without cabinets
   keeps its signal */
static void run_in_place(struct chain *c, struct chain_slot *s, float *io, float *aux, float **extra, int frames,
                         int no_cab)
{
    if (!no_cab || (s->menu != MENU_CAB && s->menu != MENU_IR)) {
        run(c, s, io, aux, extra, frames);
        return;
    }
    if (no_cab == (CAB_OFF_L | CAB_OFF_R))
        return;
    int side = no_cab == CAB_OFF_L ? 0 : 1;
    float keep[AUDIO_FRAMES];
    for (int f = 0; f < frames && f < AUDIO_FRAMES; f++)
        keep[f] = io[2 * f + side];
    run(c, s, io, aux, extra, frames);
    for (int f = 0; f < frames && f < AUDIO_FRAMES; f++)
        io[2 * f + side] = keep[f];
}

/* chain positions [*pos, *pos + n) */
static void run_span(struct chain *c, int *pos, int n, float *io, float *aux, float **extra, int frames, int no_cab)
{
    for (int end = *pos + n; *pos < end && *pos < c->count; ++*pos) {
        int i = c->order[*pos];
        if (i < 0 || i >= PRESET_SLOTS)
            continue;
        struct chain_slot *s = &c->slot[i];
        if (runnable(c, s))
            run_in_place(c, s, io, aux, extra, frames, no_cab);
    }
}

/* PRE, then the split signal through lane A and lane B, then the merge and POST (or one lane per output) */
static void process_lanes(struct chain *c, float *io, float *aux, float **extra, int frames, int no_cab)
{
    static float split[2 * AUDIO_FRAMES], lane_a[2 * AUDIO_FRAMES];
    const struct chain_joints j = c->joints;
    int n = 2 * (frames < AUDIO_FRAMES ? frames : AUDIO_FRAMES), pos = 0;
    run_span(c, &pos, c->lane_n[0], io, aux, extra, frames, no_cab);
    for (int i = 0; i < n; i++) {
        split[i] = io[i];
        io[i] = split[i] * j.in_a;
    }
    run_span(c, &pos, c->lane_n[1], io, aux, extra, frames, no_cab);
    for (int i = 0; i < n; i++) {
        lane_a[i] = io[i];
        io[i] = split[i] * j.in_b;
    }
    run_span(c, &pos, c->lane_n[2], io, aux, extra, frames, no_cab);
    if (c->mode == ROUTE_DUAL) {                /* lane A to the left output, lane B to the right one */
        for (int i = 0; i < n; i += 2)
            io[i] = lane_a[i];
    } else {
        for (int i = 0; i < n; i += 2) {
            float al = lane_a[i], ar = lane_a[i + 1], bl = io[i], br = io[i + 1];
            io[i] = al * j.a_l + bl * j.b_l;
            io[i + 1] = ar * j.a_r + br * j.b_r;
        }
        run_span(c, &pos, c->lane_n[3], io, aux, extra, frames, no_cab);
    }
    safety_slot_audio = SAFETY_NONE;
}

void chain_ramp(struct chain *c, int slot, int knob, float value, int blocks)
{
    if (slot < 0 || slot >= PRESET_SLOTS || knob < 0 || knob >= PRESET_KNOBS)
        return;
    struct chain_ramp *free_one = 0, *mine = 0;
    for (int i = 0; i < CHAIN_RAMPS; i++) {
        struct chain_ramp *r = &c->ramp[i];
        if (r->left && r->slot == slot && r->knob == knob)
            mine = r;
        else if (!r->left && !free_one)
            free_one = r;
    }
    if (mine)
        mine->left = 0;                     /* the interrupt leaves it alone from here */
    struct chain_ramp *r = mine ? mine : free_one;
    float *k = &c->slot[slot].knobs[knob];
    if (blocks <= 0 || !r) {
        *k = value;
        return;
    }
    r->slot = (int8_t)slot;
    r->knob = (uint8_t)knob;
    r->target = value;
    r->step = (value - *k) / (float)blocks;
    r->left = (uint8_t)(blocks > 255 ? 255 : blocks);
}

/* one audio block of every running ramp */
static void ramps(struct chain *c)
{
    for (int i = 0; i < CHAIN_RAMPS; i++) {
        struct chain_ramp *r = &c->ramp[i];
        if (!r->left)
            continue;
        float *k = &c->slot[r->slot].knobs[r->knob];
        *k = r->left == 1 ? r->target : *k + r->step;
        r->left--;
    }
}

void chain_process(struct chain *c, float *io, float *aux, float **extra, int frames, int no_cab)
{
    if (c->profile_reset) {
        for (int i = 0; i < PRESET_SLOTS; i++) {
            struct chain_slot *s = &c->slot[i];
            s->cyc_blocks = s->cyc_last = s->cyc_max = 0;
            s->cyc_sum = 0;
        }
        c->profile_reset = 0;
    }
    ramps(c);
    if (c->mode != ROUTE_SERIAL) {
        process_lanes(c, io, aux, extra, frames, no_cab);
        return;
    }
    struct chain_slot *later[PRESET_SLOTS];
    int n = 0;
    for (int pos = 0; pos < c->count; pos++) {
        int i = c->order[pos];
        if (i < 0 || i >= PRESET_SLOTS)
            break;
        struct chain_slot *s = &c->slot[i];
        if (!runnable(c, s))
            continue;
        if (no_cab && (s->menu == MENU_CAB || s->menu == MENU_IR))
            later[n++] = s;
        else
            run(c, s, io, aux, extra, frames);
    }
    if (no_cab == (CAB_OFF_L | CAB_OFF_R)) {
        safety_slot_audio = SAFETY_NONE;
        return;
    }
    int side = no_cab == CAB_OFF_L ? 0 : 1;            /* the channel that stays without the cabinet */
    float keep[AUDIO_FRAMES];
    for (int k = 0; k < n; k++) {
        for (int f = 0; f < frames && f < AUDIO_FRAMES; f++)
            keep[f] = io[2 * f + side];
        run(c, later[k], io, aux, extra, frames);
        for (int f = 0; f < frames && f < AUDIO_FRAMES; f++)
            io[2 * f + side] = keep[f];
    }
    safety_slot_audio = SAFETY_NONE;
}

void chain_poll(struct chain *c)
{
    for (int i = 0; i < PRESET_SLOTS; i++) {
        struct chain_slot *s = &c->slot[i];
        if (s->block && s->block->poll && s->status == MODULE_OK)
            s->block->poll(s->state, s->knobs);
    }
}

void chain_bypass_all(struct chain *c)
{
    for (int i = 0; i < PRESET_SLOTS; i++)
        c->slot[i].enabled = 0;
}
