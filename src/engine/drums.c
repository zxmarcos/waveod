#include "engine/drums.h"
#include "platform/sdram.h"
#include <string.h>

#define VOICES       16
#define CHUNK        64                     /* samples a voice plays per block */
#define EVENTS       200                    /* events of a bank pattern, at most */
#define TICKS        DRUM_TICKS
#define GUARD        400                    /* events read in one block, at most */
#define CLIP         0x7FFFF8               /* DAT_000411A4 */
#define OUT_SCALE    2.610683509374212e-07f /* DAT_00032770 */
#define MAX_SAMPLES  13

static struct {
    const uint8_t *samples, *patterns;
    uint32_t sample_count, pattern_count;
    const int16_t *sample_data[MAX_SAMPLES];
    uint32_t sample_chunks[MAX_SAMPLES];

    /* main loop -> interrupt: what to play (taken when want_gen moves), started / stopped (play_gen) */
    volatile int16_t want_pattern;
    volatile uint8_t want_user;             /* 0 = the bank's pattern, 1 / 2 = user buffer 0 / 1 */
    volatile uint16_t want_count;
    volatile uint32_t want_gen, play_gen;
    volatile uint8_t want_on;
    volatile uint16_t bpm;
    volatile uint8_t volume;
    volatile uint32_t ended;                /* interrupt -> main loop: the play_gen that reached a DRUM_END */
    volatile int8_t in_use;                 /* the user buffer the interrupt plays, -1 = none */

    int pattern;                            /* in use, -1 = none */
    uint32_t gen, played;
    uint8_t on, user;
    const uint16_t *events;
    uint32_t count;                         /* events in the pattern */
    uint32_t event;                         /* next event */
    float wait;                             /* samples until the next events */
    int32_t level;                          /* current volume, 0..100 */
    uint16_t queue[VOICES];                 /* hits waiting for a voice (FUN_00042534's ring, it can overrun) */
    uint8_t head, tail;

    struct {
        uint16_t note;                      /* sample << 8 | velocity */
        uint32_t chunks;                    /* left to stage */
        const int16_t *next;
        uint8_t live;                       /* staged holds a chunk (else it is all zeros) */
        int16_t staged[CHUNK];              /* what the voice plays in the next block */
    } voice[VOICES];
} dr;

static uint16_t user_events[2][DRUM_USER_EVENTS] SDRAM_BSS;    /* double buffered: one plays, one is filled */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int drums_init(const struct drum_pack *pack)
{
    memset(&dr, 0, sizeof dr);
    dr.pattern = -1;
    dr.in_use = -1;
    dr.ended = ~0u;
    dr.want_pattern = 0;
    dr.want_gen = 1;
    dr.bpm = 120;
    dr.volume = 80;
    if (!pack || pack->magic != DRUM_PACK_MAGIC || pack->version != 1)
        return -1;
    const uint8_t *base = (const uint8_t *)pack;
    const uint8_t *s = base + pack->samples, *p = base + pack->patterns;
    uint32_t n = rd32(s), m = rd32(p);
    if (n == 0 || n > MAX_SAMPLES || m == 0 || m > 100)
        return -1;
    for (uint32_t i = 0; i < n; i++) {                  /* FUN_00040CE0 */
        uint32_t off = rd32(s + 4 + 4 * i), ms = rd32(s + 4 + 4 * n + 4 * i) & 0xffff;
        uint32_t chunks = ms * 0x2c >> 6;
        if (off + chunks * CHUNK * 2 > pack->samples_size)
            return -1;
        dr.sample_data[i] = (const int16_t *)(s + off);
        dr.sample_chunks[i] = chunks;
    }
    dr.samples = s;
    dr.patterns = p;
    dr.sample_count = n;
    dr.pattern_count = m;
    return 0;
}

int drums_available(void)
{
    return dr.pattern_count != 0;
}

void drums_set_pattern(int pattern)
{
    if (dr.want_pattern == pattern)
        return;
    dr.want_pattern = (int16_t)pattern;
    if (!dr.want_user)
        dr.want_gen++;
}

int drums_set_events(const uint16_t *events, int count)
{
    if (count <= 0 || count > DRUM_USER_EVENTS)
        return 0;
    int b = dr.in_use == 0 ? 1 : 0;                     /* the buffer the interrupt does not play */
    memcpy(user_events[b], events, (unsigned)count * sizeof events[0]);
    dr.want_count = (uint16_t)count;
    dr.want_user = (uint8_t)(b + 1);
    dr.want_gen++;
    return 1;
}

void drums_use_bank(void)
{
    if (!dr.want_user)
        return;
    dr.want_user = 0;
    dr.want_gen++;
}

int drums_samples(void)
{
    return (int)dr.sample_count;
}

void drums_set_bpm(int bpm)
{
    dr.bpm = (uint16_t)(bpm < 40 ? 40 : bpm > 300 ? 300 : bpm);
}

void drums_set_volume(int volume)
{
    dr.volume = (uint8_t)(volume < 0 ? 0 : volume > 100 ? 100 : volume);
}

void drums_play(int on)
{
    dr.want_on = (uint8_t)(on != 0);
    dr.play_gen++;
}

int drums_playing(void)
{
    return dr.want_on && drums_available() && dr.ended != dr.play_gen;
}

void drums_hit(uint16_t note)
{
    dr.queue[dr.head] = note;
    dr.head = (uint8_t)((dr.head + 1) % VOICES);
}

/* the queued hits get the first free voices (FUN_0004104C): a hit with no free voice is lost, and the queue
   stops at a 0 */
static void start_hits(void)
{
    if (dr.head == dr.tail)
        return;
    uint16_t note = dr.queue[dr.tail];
    dr.tail = (uint8_t)((dr.tail + 1) % VOICES);
    while (note) {
        uint32_t sample = note >> 8;
        for (int v = 0; v < VOICES; v++)
            if (!dr.voice[v].chunks) {
                dr.voice[v].note = note;
                dr.voice[v].next = sample < dr.sample_count ? dr.sample_data[sample] : 0;
                dr.voice[v].chunks = sample < dr.sample_count ? dr.sample_chunks[sample] : 0;
                break;
            }
        if (dr.head == dr.tail)
            break;
        note = dr.queue[dr.tail];
        dr.tail = (uint8_t)((dr.tail + 1) % VOICES);
    }
}
/* FUN_00040D94; a user pattern may be longer and may end (DRUM_END) */
static void sequence(int frames)
{
    if (dr.want_gen != dr.gen) {
        dr.gen = dr.want_gen;
        int u = dr.want_user, want = dr.want_pattern;
        if (u) {
            dr.in_use = (int8_t)(u - 1);
            dr.user = 1;
            dr.pattern = -1;
            dr.events = user_events[u - 1];
            dr.count = dr.want_count;
        } else if (want >= 0 && (uint32_t)want < dr.pattern_count) {
            dr.in_use = -1;
            dr.user = 0;
            dr.pattern = want;
            dr.events = (const uint16_t *)(dr.patterns + rd32(dr.patterns + 4 + 4 * (uint32_t)want));
            dr.count = EVENTS;
        }
        dr.event = 0;
        dr.wait = 0.0f;
    }
    if (dr.play_gen != dr.played) {                     /* started or stopped: from the start */
        dr.played = dr.play_gen;
        dr.on = dr.want_on;
        dr.event = 0;
        dr.wait = 0.0f;
    }
    if (!dr.on || !dr.events || dr.ended == dr.played)
        return;
    float tick = 44100.0f * 60.0f / ((float)dr.bpm * TICKS);
    dr.wait -= (float)frames;
    for (int guard = 0; dr.wait <= 0.0f && guard < GUARD; guard++) {
        if (dr.event >= dr.count)
            dr.event = 0;
        uint16_t e = dr.events[dr.event++];
        if (e == DRUM_LOOP) {
            dr.event = 0;
        } else if (e < 0x8000) {
            dr.wait += (float)e * tick;
        } else if (e < 0xf000) {
            drums_hit(e & 0x7fff);
        } else if (e == DRUM_END && dr.user) {
            dr.ended = dr.played;                       /* drums_playing() says 0 until started again */
            break;
        } else {
            break;
        }
    }
}

int drums_mix(int32_t acc[CHUNK])
{
    start_hits();
    memset(acc, 0, CHUNK * sizeof acc[0]);
    int mixed = 0;
    for (int v = 0; v < VOICES; v++) {                  /* FUN_0004104C: mix what was staged, stage the next */
        if (!dr.voice[v].live && !dr.voice[v].chunks)
            continue;                                   /* silent and staying so: adds 0, its zeros stay */
        if (dr.voice[v].live) {
            int32_t vel = dr.voice[v].note & 0x7f;
            for (int i = 0; i < CHUNK; i++) {
                int32_t a = acc[i] + dr.voice[v].staged[i] * vel;
                acc[i] = a > CLIP ? CLIP : a < -CLIP ? -CLIP : a;
            }
            mixed = 1;
        }
        if (dr.voice[v].chunks) {
            memcpy(dr.voice[v].staged, dr.voice[v].next, sizeof dr.voice[v].staged);
            dr.voice[v].next += CHUNK;
            dr.voice[v].chunks--;
            dr.voice[v].live = 1;
        } else {
            memset(dr.voice[v].staged, 0, sizeof dr.voice[v].staged);
            dr.voice[v].live = 0;
        }
    }
    return mixed;
}

void drums_process(float *io, int frames)
{
    if (!drums_available())
        return;
    sequence(frames);
    int32_t acc[CHUNK];
    int32_t level = dr.level, target = dr.volume;
    if (!drums_mix(acc)) {
        /* nothing sounding: the output would add +0 to every sample (x + 0 = x but for the sign of a zero, which
           gives the same words on the converters); only the volume moves on, one step per frame */
        int n = frames < CHUNK ? frames : CHUNK;
        dr.level = level < target ? (target - level > n ? level + n : target)
                                  : (level - target > n ? level - n : target);
        return;
    }
    for (int f = 0; f < frames && f < CHUNK; f++) {     /* FUN_00040D08 */
        if (level < target)
            level++;
        else if (level > target)
            level--;
        int32_t t = (int32_t)(((int64_t)0x51eb851f * (acc[f] * level)) >> 32);        /* x level / 100 */
        float out = (float)((t >> 5) - (t >> 31)) * OUT_SCALE;
        io[2 * f] += out;
        io[2 * f + 1] += out;
    }
    dr.level = level;
}
