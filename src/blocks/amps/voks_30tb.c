/*
 * Voks 30TB (AMP › Voks): the original's Voks 30TB amp (QME-200 V1.2.2, library entry 4.9, id 0x07000027) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (amp.h, voks.h have the pieces):
 *   pre-filter    x 1.1, the 30's two fixed 2nd-order sections
 *   gain          a 2nd-order section from Gain (pow2)
 *   tone stack    3rd order, polynomials in Bass (pow3 x 0.9) and Treble (pow3); left out with Char on
 *   boost         x Volume x 6, a 2nd-order section from Tone Cut (pow3)
 *   stage         the envelope-driven clip 0.1 - 0.1 e^(-k |x|), k = 6 + 5 env, x max(1 / (1 + 3 k), 0.1), on 4
 *                 points per sample, the FIR
 *   tone cut      a 2nd-order section from Tone Cut
 *   power amp     five fixed 2nd-order sections, x 25
 * Gain, Tone Cut, Volume, Bass and Treble glide 0.0001 per sample; the coefficients stop one glide step short of the
 * knobs. Init computes the tone stack's denominators from Bass before the x 0.9. Turning Char clears the gain
 * section's state.
 */
#include "voks.h"

enum { K_GAIN, K_TONECUT, K_VOLUME, K_BASS, K_TREBLE, K_CHAR };

static const struct wvfx_param params[] = {
    VOKS_PARAM("Gain", 30.0f, K_GAIN), VOKS_PARAM("Tone Cut", 50.0f, K_TONECUT), VOKS_PARAM("Volume", 50.0f, K_VOLUME),
    VOKS_PARAM("Bass", 50.0f, K_BASS), VOKS_PARAM("Treble", 50.0f, K_TREBLE),
    { "Char", 0.0f, 1.0f, 1.0f, 0.0f, K_CHAR, 0, 0, 0, "Off\0On\0", 0 },
};

static const struct sos POWER[5] = {
    { 0.716604471206665f, -1.4274790287017822f, 0.710945188999176f, 1.0f, -1.9954160451889038f, 0.9955151081085205f },
    { 0.8948261737823486f, -1.4274790287017822f, 0.8120898008346558f, 1.0f, -1.5989476442337036f,
      0.9117233753204346f },
    { 1.4274790287017822f, -0.14202280342578888f, 0.019565600901842117f, 1.0f, -0.4935801029205322f,
      0.23233099281787872f },
    { 1.4274790287017822f, 0.10049030184745789f, 0.004634299781173468f, 1.0f, 0.8192653059959412f,
      0.25673720240592957f },
    { 0.8902760148048401f, 1.4274790287017822f, 0.6107416749000549f, 1.0f, 1.3407881259918213f, 0.5161585807800293f },
};

#define BOOST_C1 -6.643856048583984f
#define BOOST_C0 1.660964012145996f
#define TONECUT_C1 -0.9965784549713135f

/* Bass's linear factors in the denominators: Process's for the glided Bass, Init's for the taper before x 0.9 */
static const float LIN_PROCESS[4] = { -1.3815139532089233f, 3.599663019180298f, -3.0586700439453125f,
                                      0.8405258059501648f };
static const float LIN_INIT[4] = { -1.243362545967102f, 3.239696741104126f, -2.752802848815918f, 0.756473183631897f };

struct state {
    float gain, tonecut, volume, bass, treble;  /* glided */
    float chr;                                  /* the knob the gain section's state was kept for */
    struct sos gain_c, boost_c, tonecut_c;
    struct tone3 tone;
    struct sos_state pre[2], gs, boost, tc, power[5];
    struct env_state env;
    struct stage st;
};

/* the gain section's factors (gain2_set) */
static const float GAIN[7] = { -1.3779289722442627f, -0.6220710873603821f, -0.9517537951469421f, -0.4444088935852051f,
                               -0.036467310041189194f, -1.0482460260391235f, 0.4079414904117584f };

/* the tone stack from Bass b and Treble t; the denominators' linear terms are lin x c */
static void tone_set(struct tone3 *o, float b, float t, float lin, const float *c)
{
    float b2 = b * b, tb2 = t * b2;
    float t1 = t * 0.001034429995343089f, t2 = t * 0.001029112027026713f;
    float q1 = tb2 * 0.9911131262779236f, q3 = tb2 * 2.973339080810547f;
    float n0 = st_fma(b2, 0.14034639298915863f, -0.0001370404934277758f);
    float n1 = st_fma(b2, -0.3559216856956482f, 8.770165732130408e-05f);
    float n2 = st_fma(b2, 0.2957434058189392f, 0.0001370404934277758f);
    float n3 = st_fma(b2, -0.08016806840896606f, -8.770165732130408e-05f);
    o->b[0] = st_fma(-0.14028300344944f - t, b, (n0 - t1) + q1);
    o->b[1] = st_fma(st_fma(t, 3.00205397605896f, 0.3558829128742218f), b, (n1 + t2) - q3);
    o->b[2] = st_fma(st_fma(t, -3.0040969848632812f, -0.29580679535865784f), b, (n2 + t1) + q3);
    o->b[3] = st_fma(st_fma(t, 1.0020430088043213f, 0.08020682632923126f), b, n3 - (t2 + q1));
    o->a[0] = st_fma(b2, 1.3705470561981201f, st_fma(lin, c[0], -0.0014210069784894586f));
    o->a[1] = st_fma(b2, -3.568347930908203f, st_fma(lin, c[1], 0.0008659944869577885f));
    o->a[2] = st_fma(b2, 3.0299949645996094f, st_fma(lin, c[2], 0.0014184409519657493f));
    o->a[3] = st_fma(b2, -0.8321936726570129f, st_fma(lin, c[3], -0.0008685598731972277f));
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    float raw = st_taper(st_pow3, k[K_BASS]);
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2, k[K_GAIN]);
    s->tonecut = st_taper(st_pow3, k[K_TONECUT]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = raw * 0.9f;
    s->treble = st_taper(st_pow3, k[K_TREBLE]);
    s->chr = k[K_CHAR];
    gain2_set(&s->gain_c, s->gain, s->gain, GAIN);
    tone_set(&s->tone, s->bass, s->treble, raw, LIN_INIT);
    voks_boost_set(&s->boost_c, s->tonecut, BOOST_C1, BOOST_C0);
    voks_tonecut_set(&s->tonecut_c, s->tonecut, TONECUT_C1);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]);
    float tonecut = st_taper(st_pow3, io->knobs[K_TONECUT]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow3, io->knobs[K_BASS]) * 0.9f;
    float treble = st_taper(st_pow3, io->knobs[K_TREBLE]);
    float chr = io->knobs[K_CHAR];
    if (chr != s->chr) {
        s->chr = chr;
        s->gs = (struct sos_state){ 0 };
    }
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES], tc[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->tonecut = st_glide(s->tonecut, tonecut);
            s->volume = st_glide(s->volume, volume);
            s->bass = st_glide(s->bass, bass);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    sos_block(&VOKS_PRE[0], &s->pre[0], x, n);
    sos_block(&VOKS_PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->tonecut = st_glide(s->tonecut, tonecut);
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->treble = st_glide(s->treble, treble);
        tc[i] = s->tonecut;
        if (s->gain != gain)
            gain2_set(&s->gain_c, s->gain, s->gain, GAIN);
        if (s->bass != bass || s->treble != treble)
            tone_set(&s->tone, s->bass, s->treble, s->bass, LIN_PROCESS);
        if (s->tonecut != tonecut)
            voks_boost_set(&s->boost_c, s->tonecut, BOOST_C1, BOOST_C0);
        float v = sos_run_div(&s->gain_c, &s->gs, x[i]);
        if (chr == 0.0f)
            v = tone3_run_std(&s->tone, v);
        v = sos_run(&s->boost_c, &s->boost, s->volume * 6.0f * v);
        x[i] = v;
        k[i] = st_fma(voks_env_run(&s->env, v), 5.0f, 6.0f);
    }
    stage_env_block(&s->st, x, k, n, 0.1f, 0.1f);
    for (int i = 0; i < n; i++) {
        if (tc[i] != tonecut)                   /* the Tone Cut's section follows the glide as the boost did */
            voks_tonecut_set(&s->tonecut_c, tc[i], TONECUT_C1);
        x[i] = sos_run_div_b1(&s->tonecut_c, &s->tc, x[i]);
    }
    for (int j = 0; j < 5; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 25.0f;
}

const struct wvfx_desc voks_30tb_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x27),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Voks 30TB",
    .category = "Voks",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
