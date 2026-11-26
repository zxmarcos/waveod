/*
 * Sweet (DELAY): the original's Sweet delay (QME-200 V1.2.2, library entry 9.10, id 0x0B00000D) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A warm analog-voiced echo, mono (the left input; both outputs
 * the same). The input through a 2nd-order shaping filter (f1) is the dry; f1 plus Feedback x the last repeat (through
 * a 1st-order high pass) goes, clipped to +-1.7, through a 3rd-order low pass into a 4 s line read Time back (between
 * two samples). The read is saturated 4x oversampled: the shaper g(v) = (3.104 |v| - 3.141 v^2 + 2.024 v^2 |v|) /
 * (2.549 - 0.4667 v^2 + (v^2 - 1.141) |v|) (odd) on the previous read, the points 1/3 and 2/3 of the way and the read,
 * and those four into a 40-tap symmetric FIR evaluated once (the decimation). Then a 1st-order low pass that darkens
 * with the time (its pole 1.0513 + 5.70e-06 len over 1.2243 + 5.70e-06 len), a 3rd- and a 2nd-order low pass and a
 * clip to +-1.7: the repeat. Single precision.
 *
 * Out, through a last 2nd-order filter (f9): below Mix 0.5, f1 + 2 Mix repeat; from 0.5, repeat + 2 (1 - Mix) f1
 * (bypassed with Trail on: repeat + f1). Feedback goes by the original's taper (x 0.8). Mix, Feedback and the time
 * glide 0.00022676 of the way to the knobs each sample (with Sync on the time jumps). Bypassed with Trail on the line
 * runs on its feedback alone; with Trail at 0 it is cleared once (the filters run on) and the signal passes. The Init
 * reads the tempo: here the first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 30.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

/* Feedback's taper (the knob x 2.55 its index) */
static const float FEEDBACK_TAPER[256] = {
    0.0f, 0.0f, 0.0f, 0.0017000000225380063f, 0.0035000001080334187f, 0.005200000014156103f, 0.007000000216066837f,
    0.008700000122189522f, 0.010499999858438969f, 0.012199999764561653f, 0.014000000432133675f, 0.015699999406933784f,
    0.017500000074505806f, 0.019200000911951065f, 0.020999999716877937f, 0.022700000554323196f, 0.02449999935925007f,
    0.026200000196695328f, 0.02800000086426735f, 0.02979999966919422f, 0.03150000050663948f, 0.0333000011742115f,
    0.03500000014901161f, 0.03680000081658363f, 0.03849999979138374f, 0.040300000458955765f, 0.041999999433755875f,
    0.043800000101327896f, 0.045499999076128006f, 0.04729999974370003f, 0.04899999871850014f, 0.05079999938607216f,
    0.05249999836087227f, 0.05429999902844429f, 0.0560000017285347f, 0.05779999867081642f, 0.05959999933838844f,
    0.06129999831318855f, 0.06310000270605087f, 0.06480000168085098f, 0.066600002348423f, 0.06830000132322311f,
    0.07010000199079514f, 0.07180000096559525f, 0.07360000163316727f, 0.07530000060796738f, 0.0771000012755394f,
    0.07880000025033951f, 0.08060000091791153f, 0.08229999989271164f, 0.08410000056028366f, 0.08579999953508377f,
    0.08760000020265579f, 0.08940000087022781f, 0.09109999984502792f, 0.09290000051259995f, 0.09459999948740005f,
    0.09640000015497208f, 0.09809999912977219f, 0.09989999979734421f, 0.10159999877214432f, 0.10339999943971634f,
    0.10509999841451645f, 0.10689999908208847f, 0.10859999805688858f, 0.1103999987244606f, 0.11219999939203262f,
    0.11389999836683273f, 0.11569999903440475f, 0.11739999800920486f, 0.11919999867677689f, 0.120899997651577f,
    0.12269999831914902f, 0.12439999729394913f, 0.12620000541210175f, 0.12800000607967377f, 0.12970000505447388f,
    0.1315000057220459f, 0.133200004696846f, 0.13500000536441803f, 0.13670000433921814f, 0.13850000500679016f,
    0.14030000567436218f, 0.1420000046491623f, 0.14380000531673431f, 0.14550000429153442f, 0.14730000495910645f,
    0.14910000562667847f, 0.15080000460147858f, 0.1526000052690506f, 0.1543000042438507f, 0.15610000491142273f,
    0.15790000557899475f, 0.15960000455379486f, 0.16140000522136688f, 0.1632000058889389f, 0.164900004863739f,
    0.16670000553131104f, 0.16850000619888306f, 0.17030000686645508f, 0.1720000058412552f, 0.1738000065088272f,
    0.17560000717639923f, 0.17739999294281006f, 0.17910000681877136f, 0.1808999925851822f, 0.1826999932527542f,
    0.18449999392032623f, 0.18629999458789825f, 0.18799999356269836f, 0.18979999423027039f, 0.1915999948978424f,
    0.19339999556541443f, 0.19519999623298645f, 0.19699999690055847f, 0.1987999975681305f, 0.20059999823570251f,
    0.20239999890327454f, 0.20419999957084656f, 0.20600000023841858f, 0.2078000009059906f, 0.20970000326633453f,
    0.21150000393390656f, 0.21330000460147858f, 0.2151000052690506f, 0.21699999272823334f, 0.21879999339580536f,
    0.22059999406337738f, 0.2224999964237213f, 0.22429999709129333f, 0.22619999945163727f, 0.2280000001192093f,
    0.22990000247955322f, 0.23170000314712524f, 0.23360000550746918f, 0.23549999296665192f, 0.23739999532699585f,
    0.23929999768733978f, 0.24120000004768372f, 0.24310000240802765f, 0.24500000476837158f, 0.24690000712871552f,
    0.24879999458789825f, 0.2506999969482422f, 0.25270000100135803f, 0.25459998846054077f, 0.2565999925136566f,
    0.25850000977516174f, 0.2605000138282776f, 0.26249998807907104f, 0.2644999921321869f, 0.26649999618530273f,
    0.2685000002384186f, 0.2705000042915344f, 0.27250000834465027f, 0.2745000123977661f, 0.2766000032424927f,
    0.27869999408721924f, 0.2806999981403351f, 0.28279998898506165f, 0.2849000096321106f, 0.28700000047683716f,
    0.2892000079154968f, 0.2912999987602234f, 0.29350000619888306f, 0.2955999970436096f, 0.2978000044822693f,
    0.30000001192092896f, 0.30230000615119934f, 0.304500013589859f, 0.3066999912261963f, 0.3089999854564667f,
    0.31130000948905945f, 0.31360000371932983f, 0.3160000145435333f, 0.3183000087738037f, 0.3206999897956848f,
    0.3231000006198883f, 0.3255000114440918f, 0.3278999924659729f, 0.3303999900817871f, 0.3328999876976013f,
    0.3353999853134155f, 0.33799999952316284f, 0.34049999713897705f, 0.34310001134872437f, 0.3456999957561493f,
    0.3483999967575073f, 0.35109999775886536f, 0.3537999987602234f, 0.3564999997615814f, 0.35929998755455017f,
    0.3621000051498413f, 0.36500000953674316f, 0.3677999973297119f, 0.3707999885082245f, 0.37369999289512634f,
    0.3767000138759613f, 0.3797000050544739f, 0.38280001282691956f, 0.38589999079704285f, 0.3889999985694885f,
    0.3921999931335449f, 0.3955000042915344f, 0.3986999988555908f, 0.40209999680519104f, 0.40540000796318054f,
    0.40880000591278076f, 0.4122999906539917f, 0.415800005197525f, 0.41940000653266907f, 0.4230000078678131f,
    0.42669999599456787f, 0.43050000071525574f, 0.4341999888420105f, 0.43810001015663147f, 0.44200000166893005f,
    0.44600000977516174f, 0.44999998807907104f, 0.45410001277923584f, 0.45829999446868896f, 0.4625000059604645f,
    0.4668000042438507f, 0.47119998931884766f, 0.475600004196167f, 0.48010000586509705f, 0.4846999943256378f,
    0.4893999993801117f, 0.4941999912261963f, 0.49900001287460327f, 0.5038999915122986f, 0.508899986743927f,
    0.5139999985694885f, 0.5192000269889832f, 0.524399995803833f, 0.5297999978065491f, 0.5351999998092651f,
    0.5407999753952026f, 0.5464000105857849f, 0.5521000027656555f, 0.5580000281333923f, 0.5638999938964844f,
    0.5699999928474426f, 0.5760999917984009f, 0.5824000239372253f, 0.5888000130653381f, 0.595300018787384f,
    0.6018999814987183f, 0.6086999773979187f, 0.6154999732971191f, 0.6225000023841858f, 0.6297000050544739f,
    0.636900007724762f, 0.6442999839782715f, 0.8799999952316284f, 0.8799999952316284f,
};

static const float NUM1[3] = { -3.554753541946411f, -0.20205315947532654f, 3.3527004718780518f };
static const float DEN1[2] = { -0.3003005087375641f, -0.29559314250946045f };
static const float NUM3[4] = { 0.0053502293303608894f, 0.016050688922405243f, 0.016050688922405243f,
                               0.0053502293303608894f };
static const float DEN3[3] = { -2.23078989982605f, 1.7143292427062988f, -0.44073745608329773f };
static const float NUM5[4] = { 0.00894797220826149f, 0.02684391662478447f, 0.02684391662478447f,
                               0.00894797220826149f };
static const float DEN5[3] = { -1.7583926916122437f, 0.8179787397384644f, 0.011997763998806477f };
static const float NUM6[3] = { 0.08147872984409332f, 0.16295745968818665f, 0.08147872984409332f };
static const float DEN6[2] = { -1.4685533046722412f, 0.7944682836532593f };
static const float NUM9[3] = { -0.1837262213230133f, -0.05251121148467064f, 0.13121500611305237f };
static const float DEN9[2] = { -0.07804452627897263f, -0.8169330358505249f };
static const float NUM_FB[2] = { 0.9814540147781372f, -0.9814540147781372f };
static const float DEN_FB = -0.9629080295562744f;
static const float NUM_LP[2] = { 0.08650371432304382f, 0.08650371432304382f };

/* the FIR's taps by pair (newest index, the other is 39 - it), in the original's order of summing */
/* a filter's last inputs and outputs ([k] = k samples ago; [0] = [1] once stepped) */
struct hist {
    float x[4], y[4];
};

struct state {
    float mix, feedback, len, repeat;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct dly_line line;
    struct hist f1, fb, f3, lp, f5, f6, f9;
    struct stage st;                    /* the shaped points of the reads (amp.h; st.prev: the line's last read) */
};

/* the FIR's pair order (FIR_I; FIR_C[k] = AMP_FIR[FIR_I[k]]) */
static const uint8_t FIR_ORDER[20] = { 0, 3, 2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

static float clip17(float x)
{
    return dly_hold(x, 1.7000000476837158f);
}

/* the 2nd-order filters: num b, den a (a0 = 1) */
static float biquad(struct hist *h, const float *b, const float *a, float x)
{
    float n = st_fma(b[2], h->x[2], st_fma(b[1], h->x[1], b[0] * x));
    float d = st_fma(h->y[2], a[1], h->y[1] * a[0]);
    float y = n - d;
    h->x[2] = h->x[1];
    h->x[1] = h->x[0] = x;
    h->y[2] = h->y[1];
    h->y[1] = h->y[0] = y;
    return y;
}

/* the 3rd-order ones */
static float third(struct hist *h, const float *b, const float *a, float x)
{
    float n = st_fma(b[3], h->x[3], st_fma(b[2], h->x[2], st_fma(b[1], h->x[1], b[0] * x)));
    float d = st_fma(a[2], h->y[3], st_fma(a[1], h->y[2], a[0] * h->y[1]));
    float y = n - d;
    h->x[3] = h->x[2];
    h->x[2] = h->x[1];
    h->x[1] = h->x[0] = x;
    h->y[3] = h->y[2];
    h->y[2] = h->y[1];
    h->y[1] = h->y[0] = y;
    return y;
}

/* the saturation curve (odd) */
static float shape(float v)
{
    float v2 = v * v, a = st_abs(v);
    float p = st_fma(a, 3.1040000915527344f, st_fma(v2 * 2.0239999294281006f, a, -(v2 * 3.1410000324249268f)));
    float d = st_fma(v2 + -1.1410000324249268f, a, st_fma(v2, -0.4666999876499176f, 2.5490000247955322f));
    return (v < 0.0f ? -p : p) / d;
}

/* the echo after the FIR: the time-dependent low pass, the two filters, the clip */
static float echo_tail(struct state *s, float y, float len)
{
    float t = len * 5.697421329386998e-06f;
    float l = st_fma(s->lp.y[1], t + 1.0513283014297485f, st_fma(NUM_LP[1], s->lp.x[1], y * NUM_LP[0]));
    l = l / (t + 1.2243356704711914f);
    s->lp.x[1] = s->lp.x[0] = y;
    s->lp.y[1] = s->lp.y[0] = l;
    float o = biquad(&s->f6, NUM6, DEN6, third(&s->f5, NUM5, DEN5, l));
    return dly_hold(o, 1.7000000476837158f);
}

/* one sample of the echo: in -> the repeat */
static float echo(struct state *s, float in, float len)
{
    dly_write(&s->line, third(&s->f3, NUM3, DEN3, clip17(in)));
    float r = dly_read(&s->line, (int)len, len - __builtin_truncf(len));             /* VRINTZ */
    float q[4];
    stage_points(&s->st, r, q);
    for (int k = 0; k < 4; k++)
        q[k] = shape(q[k]);
    float y = stage_fir_ord(&s->st, q, FIR_ORDER);
    /* the time-dependent low pass */
    float t = len * 5.697421329386998e-06f;
    float l = st_fma(s->lp.y[1], t + 1.0513283014297485f, st_fma(NUM_LP[1], s->lp.x[1], y * NUM_LP[0]));
    l = l / (t + 1.2243356704711914f);
    s->lp.x[1] = s->lp.x[0] = y;
    s->lp.y[1] = s->lp.y[0] = l;
    float o = biquad(&s->f6, NUM6, DEN6, third(&s->f5, NUM5, DEN5, l));
    return dly_hold(o, 1.7000000476837158f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = st_taper(FEEDBACK_TAPER, k[2]) * 0.800000011920929f;
    s->time0 = k[1];
    s->sync0 = k[3];
    s->line.buf = buffer;
    __builtin_memset(buffer, 0, DLY_LINE * sizeof(float));
    s->cleared = 1;
    return 0;
}

/*
 * The loop of process() over a block, stage by stage (the same operations on the same values), when every read is
 * of a sample written before the block (the delay longer than the block: always, 882+ samples): the dry filter;
 * the reads, shaped and through the FIR (amp.h's stage over the block), the echo's tail: the block's repeats; then
 * per sample the feedback filter of the repeat before, the line's input and write; then the output filter.
 */
static void block(struct state *s, const struct wvfx_io *io, const float *k, float target, float df, float dm,
                  float dl)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float *p = io->io, dry[WVFX_MAX_FRAMES], rep[WVFX_MAX_FRAMES], L[WVFX_MAX_FRAMES], F[WVFX_MAX_FRAMES];
    float M[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    for (int i = 0; i < n; i++) {
        s->mix += dm;
        float glided = s->len + dl;
        s->feedback += df;
        s->len = k[3] == 1.0f ? target : glided;
        M[i] = s->mix;
        F[i] = s->feedback;
        L[i] = s->len;
        dry[i] = biquad(&s->f1, NUM1, DEN1, p[2 * i]);
    }
    /* the reads: sample i's after its write (the line's position then w + 1 + i) */
    for (int i = 0; i < n; i++) {
        int wi = s->line.w + 1 + i;
        while (wi > DLY_LINE - 1)
            wi -= DLY_LINE;
        struct dly_line at = { s->line.buf, wi };
        rep[i] = dly_read(&at, (int)L[i], L[i] - __builtin_truncf(L[i]));               /* VRINTZ */
    }
    STAGE_BLOCK_POINTS(&s->st, rep, n, buf, shape(v));
    stage_block_fir_ord(&s->st, buf, rep, n, FIR_ORDER);
    for (int i = 0; i < n; i++)
        rep[i] = echo_tail(s, rep[i], L[i]);
    /* the line's input from the repeat before (s->repeat before the first) */
    for (int i = 0; i < n; i++) {
        float h = st_fma(-DEN_FB, s->fb.y[1], st_fma(NUM_FB[1], s->fb.x[1], NUM_FB[0] * s->repeat));   /* VFMS */
        s->fb.x[1] = s->fb.x[0] = s->repeat;
        s->fb.y[1] = s->fb.y[0] = h;
        float in = io->enabled ? st_fma(h, F[i], dry[i]) : h * F[i];
        dly_write(&s->line, third(&s->f3, NUM3, DEN3, clip17(in)));
        s->repeat = rep[i];
    }
    for (int i = 0; i < n; i++) {
        float y, r = rep[i];
        if (M[i] < 0.5f)
            y = st_fma(r + r, M[i], dry[i]);
        else if ((int)k[4] == 1 && !io->enabled)
            y = dry[i] + r;
        else
            y = st_fma(dry[i] + dry[i], 1.0f - M[i], r);
        p[2 * i] = p[2 * i + 1] = biquad(&s->f9, NUM9, DEN9, y);
    }
    if (io->enabled)
        s->cleared = 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = dly_time(k[1], k[3], io->beat_ms);
    if (io->frames == 0)
        return;
    float df = (st_taper(FEEDBACK_TAPER, k[2]) * 0.800000011920929f - s->feedback) * 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    if (io->enabled || k[4] == 1.0f) {
        /* the reads come from before the block while the delay (882+ samples; it glides toward the target) is
           longer than the block */
        const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
        if ((int)s->len > n + 1 && (int)target > n + 1) {
            block(s, io, k, target, df, dm, dl);
            return;
        }
    }
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix += dm;
        float glided = s->len + dl;
        s->feedback += df;
        s->len = k[3] == 1.0f ? target : glided;
        float x = p[0];
        if (!io->enabled && k[4] != 1.0f) {
            if (k[4] == 0.0f && !s->cleared) {
                s->repeat = 0.0f;
                s->line.w = 0;
                __builtin_memset(s->line.buf, 0, DLY_LINE * sizeof(float));
                s->cleared = 1;
            }
            continue;
        }
        float dry = biquad(&s->f1, NUM1, DEN1, x);
        float h = st_fma(-DEN_FB, s->fb.y[1], st_fma(NUM_FB[1], s->fb.x[1], NUM_FB[0] * s->repeat));   /* VFMS */
        s->fb.x[1] = s->fb.x[0] = s->repeat;
        s->fb.y[1] = s->fb.y[0] = h;
        s->repeat = echo(s, io->enabled ? st_fma(h, s->feedback, dry) : h * s->feedback, s->len);
        float y;
        if (s->mix < 0.5f)
            y = st_fma(s->repeat + s->repeat, s->mix, dry);
        else if ((int)k[4] == 1 && !io->enabled)
            y = dry + s->repeat;
        else
            y = st_fma(dry + dry, 1.0f - s->mix, s->repeat);
        p[0] = p[1] = biquad(&s->f9, NUM9, DEN9, y);
        if (io->enabled)
            s->cleared = 0;
    }
}

const struct wvfx_desc dly_sweet_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10A),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sweet",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_LINE * (int)sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
