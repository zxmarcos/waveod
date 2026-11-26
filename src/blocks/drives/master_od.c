/*
 * Master OD (DRIVE › OD): the original's Master OD (QME-200 V1.2.2, library entry 3.10, id 0x0300000F) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision except the EQ.
 *
 * Per sample: a fixed 1st-order input section, a 2nd-order gain section (Gain, pow4), three fixed 2nd-order sections,
 * x 0.02, an exponential clipper 30 (1 - e^(-10|x|)) with the sign of x on 4 points per sample with the anti-aliasing
 * FIR, a 5th-order EQ in double whose coefficients are polynomials in Bass (pow4), Middle and Treble (pow0_5), a
 * 1st-order level section (Volume), x 0.05, ten fixed 2nd-order sections, x 4.8.
 *
 * Bass and Middle glide 0.004 and Treble 0.002 per process call, also while bypassed; the EQ is recomputed whenever one
 * of them moved. Gain and Volume glide 0.0001 per sample while the block is on. The gain section is recomputed only
 * while Gain is still on its way, so it keeps the value one step short of the knob (kept as the original does it).
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
};

/* the EQ's coefficients: polynomials in Bass (B), Middle (M) and Treble (T). The monomials: products of two knobs
   are taken in single precision and widened, the higher ones multiplied on in double */
enum {
    EQ_B, EQ_T, EQ_BB, EQ_M, EQ_TB, EQ_BBT, EQ_MB, EQ_MT, EQ_MBB, EQ_MM, EQ_MTBB, EQ_MMB, EQ_MMT, EQ_MMBB, EQ_MBT,
    EQ_MMTBB, EQ_MMBT, EQ_MONOMIALS
};
struct eq_term { uint8_t m; double k; };
struct eq_poly { double c0; int n; struct eq_term t[17]; };

/* the EQ numerator, b0 .. b5: c0, then each term (monomial, k) fused on in the original's order */
static const struct eq_poly EQ_NUM[6] = {
    { -2.8410358861832e-05, 17, {
          { EQ_B, -0.00081355467788456 }, { EQ_T, -0.00093355436791756 }, { EQ_BB, 0.00083222880382352 },
          { EQ_M, -0.0050852392630679 }, { EQ_TB, -0.037031346396265 }, { EQ_BBT, 0.037471002441717 },
          { EQ_MB, -0.07934012569901 }, { EQ_MT, -0.035169953152394 }, { EQ_MBB, 0.083295964047112 },
          { EQ_MM, 0.0036211564818696 }, { EQ_MTBB, 1.0 }, { EQ_MMB, 0.057604801724768 },
          { EQ_MMT, 0.031244225110937 }, { EQ_MMBB, -0.060354633291952 }, { EQ_MBT, -0.99891290423691 },
          { EQ_MMTBB, -0.86195760401451 }, { EQ_MMBT, 0.86077340832157 },
    } },
    { 2.5180826968262e-06, 17, {
          { EQ_B, 0.0011106640743842 }, { EQ_T, 0.0022222654321197 }, { EQ_BB, -0.0011144282907799 },
          { EQ_M, 0.010262307613205 }, { EQ_TB, 0.1452478107419 }, { EQ_BBT, -0.14633658381982 },
          { EQ_MB, 0.16480163675856 }, { EQ_MT, 0.13936621898077 }, { EQ_MBB, -0.17377603295509 },
          { EQ_MM, -0.0085383780346098 }, { EQ_MMB, -0.13969073487994 }, { EQ_MMT, -0.13106459651574 },
          { EQ_MMBB, 0.14687137912587 }, { EQ_MBT, 4.4110808649594 }, { EQ_MTBB, -4.4110566006924 },
          { EQ_MMTBB, 4.0240082680594 }, { EQ_MMBT, -4.0228240723665 },
    } },
    { 5.0203220807014e-05, 17, {
          { EQ_B, 0.00018850605388459 }, { EQ_T, -0.00080443696353473 }, { EQ_BB, -0.00022460523486049 },
          { EQ_M, -0.0014919542476274 }, { EQ_TB, -0.22330356489269 }, { EQ_BBT, 0.22376955677278 },
          { EQ_MB, -0.031240049142806 }, { EQ_MT, -0.21952681162369 }, { EQ_MBB, 0.033607857861287 },
          { EQ_MM, 0.0029485561973713 }, { EQ_MMB, 0.054203822502431 }, { EQ_MMT, 0.21687501895383 },
          { EQ_MMBB, -0.05756578472555 }, { EQ_MBT, -7.7306502578668 }, { EQ_MTBB, 7.727146822537 },
          { EQ_MMTBB, -7.4764570320927 }, { EQ_MMBT, 7.4788254234786 },
    } },
    { -1.2070185887074e-05, 17, {
          { EQ_B, -0.00098098431405143 }, { EQ_T, -0.0017752232967899 }, { EQ_BB, 0.00098976181774484 },
          { EQ_M, -0.0089430046874831 }, { EQ_TB, 0.16797010447766 }, { EQ_BBT, -0.16713232683415 },
          { EQ_MB, -0.14087153525331 }, { EQ_MT, 0.17217418722645 }, { EQ_MBB, 0.14854084223168 },
          { EQ_MM, 0.006885886908109 }, { EQ_MMB, 0.10996804380791 }, { EQ_MMT, -0.17667972887643 },
          { EQ_MMBB, -0.11546770694228 }, { EQ_MBT, 6.7163815787262 }, { EQ_MTBB, -6.7151008634565 },
          { EQ_MMTBB, 6.9048975280665 }, { EQ_MMBT, -6.9072659194524 },
    } },
    { -2.2625909098726e-05, 17, {
          { EQ_B, 0.00061279793056551 }, { EQ_T, 0.0017379913314523 }, { EQ_BB, -0.00059537287552857 },
          { EQ_M, 0.0065771935106953 }, { EQ_TB, -0.061744903230091 }, { EQ_BBT, 0.060839255304546 },
          { EQ_MB, 0.11058017484182 }, { EQ_MT, -0.067383049742963 }, { EQ_MBB, -0.1169038219084 },
          { EQ_MM, -0.0065697126792409 }, { EQ_MMB, -0.1118086242272 }, { EQ_MMT, 0.070771661399634 },
          { EQ_MMBB, 0.1179204180175 }, { EQ_MBT, -2.8905713225049 }, { EQ_MTBB, 2.8929876620716 },
          { EQ_MMTBB, -3.1666690120202 }, { EQ_MMBT, 3.1654848163272 },
    } },
    { 8.7190560367039e-06, 17, {
          { EQ_B, -0.00014193045376722 }, { EQ_T, -0.00044704213532981 }, { EQ_BB, 0.00013691716646951 },
          { EQ_M, -0.0013193029257217 }, { EQ_TB, 0.0088618992994839 }, { EQ_BBT, -0.0086109038650762 },
          { EQ_MB, -0.023930101505253 }, { EQ_MT, 0.010539408311821 }, { EQ_MBB, 0.025235190723408 },
          { EQ_MM, 0.0016524911265008 }, { EQ_MMB, 0.029722691072026 }, { EQ_MMT, -0.011146580072236 },
          { EQ_MMBB, -0.031403672183586 }, { EQ_MBT, 0.49267204092308 }, { EQ_MTBB, -0.49397702045973 },
          { EQ_MMTBB, 0.57617785200142 }, { EQ_MMBT, -0.57499365630848 },
    } },
};
/* the EQ denominator, a0 .. a5: c0, then each term (monomial, k) fused on in the original's order */
static const struct eq_poly EQ_DEN[6] = {
    { -0.0013937368641079, 8, {
          { EQ_B, -0.054904728649896 }, { EQ_BB, 0.055567538154653 }, { EQ_M, -0.055707169766823 },
          { EQ_MBB, 1.4872189427748 }, { EQ_MM, 0.042402464892594 }, { EQ_MB, -1.4818188677729 },
          { EQ_MMBB, -1.1302433426315 }, { EQ_MMB, 1.1260236503709 },
    } },
    { 0.0024167596858234, 8, {
          { EQ_B, 0.18034614633592 }, { EQ_BB, -0.18154684598288 }, { EQ_M, 0.18271083209236 },
          { EQ_MBB, -5.6145427837083 }, { EQ_MB, 5.6063352244503 }, { EQ_MM, -0.15614568364811 },
          { EQ_MMB, -4.7176547830566 }, { EQ_MMBB, 4.725733957995 },
    } },
    { 7.4091527888927e-05, 8, {
          { EQ_B, -0.22783365030028 }, { EQ_BB, 0.22787448544816 }, { EQ_M, -0.22890871516107 },
          { EQ_MBB, 8.219329259138 }, { EQ_MM, 0.22398102102958 }, { EQ_MB, -8.2210496142426 },
          { EQ_MMBB, -7.8120070171374 }, { EQ_MMB, 7.8115847763011 },
    } },
    { -0.0022078909564552, 8, {
          { EQ_B, 0.13748794799832 }, { EQ_BB, -0.13640383170473 }, { EQ_M, 0.13558374971109 },
          { EQ_MBB, -5.7735602572983 }, { EQ_MB, 5.7811546874915 }, { EQ_MM, -0.15594003625074 },
          { EQ_MMB, -6.3808643349934 }, { EQ_MMBB, 6.3735676104741 },
    } },
    { 0.0013159600336172, 8, {
          { EQ_B, -0.039351015611139 }, { EQ_BB, 0.038650041609663 }, { EQ_M, -0.037463929591155 },
          { EQ_MBB, 1.9135862826958 }, { EQ_MM, 0.052507419542229 }, { EQ_MB, -1.9172660025931 },
          { EQ_MMBB, -2.5628332883585 }, { EQ_MMB, 2.5674752214554 },
    } },
    { -0.00021255403196999, 8, {
          { EQ_B, 0.0042361401425377 }, { EQ_BB, -0.0041168861379956 }, { EQ_M, 0.0037852327155945 },
          { EQ_MBB, -0.232031443602 }, { EQ_MB, 0.23264457266684 }, { EQ_MM, -0.0068051855655459 },
          { EQ_MMB, -0.40656453007736 }, { EQ_MMBB, 0.40578207965828 },
    } },
};

static const struct fos PF1 = { 0.9988675117492676f, -0.9988675117492676f, -0.9977350234985352f };
static const struct sos PF2[3] = {
    { 1.6164621114730835f, -2.5727219581604004f, 0.9564332962036133f, 1.0f, -1.769709587097168f, 0.7777889966964722f },
    { 2.5727219581604004f, -0.4354323148727417f, 0.0021098998840898275f, 1.0f, -0.7220578789710999f,
      0.07472629845142365f },
    { 2.5727219581604004f, 2.2266197204589844f, 0.3051570951938629f, 1.0f, 0.646241307258606f, 0.003706699935719371f },
};
static const struct sos POST[10] = {
    { 0.908810019493103f, -1.6486610174179077f, 0.741540789604187f, 1.0f, -1.9381077289581299f, 0.9411501288414001f },
    { 0.8556904792785645f, -1.6486610174179077f, 0.7950282096862793f, 1.0f, -1.614694356918335f, 0.6260684132575989f },
    { 0.8996902108192444f, -1.6486610174179077f, 0.7580438852310181f, 1.0f, -1.7501102685928345f, 0.7531542181968689f },
    { 1.15739107131958f, -1.6486610174179077f, 0.5771803259849548f, 1.0f, -1.6082416772842407f, 0.8288838267326355f },
    { 1.034016489982605f, -1.6486610174179077f, 0.8349364995956421f, 1.0f, -0.6307761073112488f, 0.17534320056438446f },
    { 1.6486610174179077f, -1.4489939212799072f, 0.4815874993801117f, 1.0f, -1.70355224609375f, 0.7278285026550293f },
    { 1.6486610174179077f, -0.40457281470298767f, 0.33762669563293457f, 1.0f, 0.0829508975148201f,
      0.03908830136060715f },
    { 1.6486610174179077f, 0.472833514213562f, 0.15174399316310883f, 1.0f, 0.05824439972639084f,
      0.001988800009712577f },
    { 0.11147060245275497f, 0.1022540032863617f, 0.02557539939880371f, 1.0f, 0.999908983707428f, 0.2296804040670395f },
    { 1.059403419494629f, 1.6486610174179077f, 0.6226943731307983f, 1.0f, 1.7703313827514648f, 0.7840467095375061f },
};

struct state {
    float gain, master, bass, middle, treble;     /* glided */
    struct sos gc;
    struct fos lc;                      /* the level section, a0 in la0 */
    float la0;
    double eb[6], ea[6], ex[6], ey[6];  /* the EQ: b0..b5, a0..a5, its last inputs and outputs */
    struct fos_state pf1, ls;
    struct sos_state gs, pf2[3], post[10];
    struct stage st;
};

static void gain_set(struct sos *c, float g)
{
    float b1 = st_fma(g, -1.9019800424575806f, 0.11113379895687103f);
    *c = (struct sos){ st_fma(g, 50.0099983215332f, 1.1337870359420776f), b1,
                       st_fma(g, -48.1080207824707f, -1.0226529836654663f), g + 1.1337870359420776f, b1,
                       st_fma(g, 0.9019799828529358f, -1.0226529836654663f) };
}

/* the level section from Volume (0-1); b0 = b1 is given, the Init takes it from the raw knob */
static void level_set(struct state *s, float m, float b)
{
    s->lc = (struct fos){ b, b, st_fma(m - m * m, 0.9528902173042297f, -0.025913219898939133f) };
    s->la0 = (-0.45554909110069275f - m) + m * m;
}

static double eq_poly(const struct eq_poly *p, const double *v)
{
    double acc = p->c0;
    for (int i = 0; i < p->n; i++)
        acc = st_fmad(v[p->t[i].m], p->t[i].k, acc);
    return acc;
}

static void eq_set(struct state *s)
{
    float B = s->bass, M = s->middle, T = s->treble;
    double v[EQ_MONOMIALS];
    v[EQ_B] = B;
    v[EQ_T] = T;
    v[EQ_M] = M;
    v[EQ_BB] = B * B;
    v[EQ_TB] = T * B;
    v[EQ_MB] = M * B;
    v[EQ_MT] = M * T;
    v[EQ_MM] = M * M;
    v[EQ_BBT] = v[EQ_BB] * v[EQ_T];
    v[EQ_MBB] = v[EQ_M] * v[EQ_BB];
    v[EQ_MTBB] = v[EQ_MT] * v[EQ_BB];
    v[EQ_MMB] = v[EQ_MM] * v[EQ_B];
    v[EQ_MMT] = v[EQ_MM] * v[EQ_T];
    v[EQ_MMBB] = v[EQ_MM] * v[EQ_BB];
    v[EQ_MBT] = v[EQ_MB] * v[EQ_T];
    v[EQ_MMTBB] = v[EQ_MMT] * v[EQ_BB];
    v[EQ_MMBT] = v[EQ_MMB] * v[EQ_T];
    for (int i = 0; i < 6; i++) {
        s->eb[i] = eq_poly(&EQ_NUM[i], v);
        s->ea[i] = eq_poly(&EQ_DEN[i], v);
    }
}

/* the EQ: both sums start from the x1 / y1 product */
static double eq_run(struct state *s, double x)
{
    double n = s->eb[1] * s->ex[1], d = s->ea[1] * s->ey[1];
    n = st_fmad(s->eb[0], x, n);
    for (int i = 2; i < 6; i++)
        n = st_fmad(s->eb[i], s->ex[i], n);
    for (int i = 2; i < 6; i++)
        d = st_fmad(s->ea[i], s->ey[i], d);
    double y = (n - d) / s->ea[0];
    for (int i = 5; i > 1; i--) {
        s->ex[i] = s->ex[i - 1];
        s->ey[i] = s->ey[i - 1];
    }
    s->ex[1] = x;
    s->ey[1] = y;
    return y;
}

/*
 * The stage over a block in place, y the section's output: the points of x = 0.02 y, each through 30 (1 - e^(-10
 * |x|)) with the sign of x (clip_env1 = clip_env, a = 30: 30 - 30 e above zero, 30 e - 30 below); the current point
 * the original feeds expf from y with the 10 folded into its factor (0.2), its side by x's sign (y's: where x rounds to
 * 0 both sides give 0). No point is the last one's again. Then the FIR four samples at a time.
 */
static void stage_block(struct stage *s, float *y, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i] * 0.019999999552965164f, d = x - prev, *o = buf + 40 + 4 * i;
        o[0] = clip_env1(prev, 10.0f, 30.0f);
        o[1] = clip_env1(st_fma(d, 0.33333334f, prev), 10.0f, 30.0f);
        o[2] = clip_env1(st_fma(d, 0.6666667f, prev), 10.0f, 30.0f);
        o[3] = clip_env1(y[i], 0.19999998807907104f, 30.0f);
        prev = x;
    }
    s->prev = prev;
    stage_block_fir(s, buf, y, n);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow4, k[0]);
    s->master = k[1] * 0.01f;
    s->bass = st_taper(st_pow4, k[2]);
    s->middle = k[3] * 0.01f;
    s->treble = st_taper(st_pow0_5, k[4]);
    gain_set(&s->gc, s->gain);
    level_set(s, s->master, k[1] * -0.002355491043999791f);
    eq_set(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float g = st_taper(st_pow4, k[0]), master = k[1] * 0.01f;
    float b = st_taper(st_pow4, k[2]), m = k[3] * 0.01f, t = st_taper(st_pow0_5, k[4]);
    if (s->bass != b || s->treble != t || s->middle != m) {
        s->bass = st_glide_by(s->bass, b, 0.004f);
        s->middle = st_glide_by(s->middle, m, 0.004f);
        s->treble = st_glide_by(s->treble, t, 0.002f);
        eq_set(s);
    }
    if (!io->enabled)
        return;
    /* stage by stage over the block: the Gain's glide before the stage, the Master's after it (each once per
       sample, its section following it); the same samples as sample by sample */
    float *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, g);
        if (s->gain != g)
            gain_set(&s->gc, s->gain);
        y[i] = sos_run_div(&s->gc, &s->gs, fos_run(&PF1, &s->pf1, p[2 * i]));
    }
    for (int j = 0; j < 3; j++)
        sos_block(&PF2[j], &s->pf2[j], y, n);
    stage_block(&s->st, y, n);
    for (int i = 0; i < n; i++) {
        if (s->master != master) {
            s->master = st_glide(s->master, master);
            level_set(s, s->master, s->master * -0.23554910719394684f);
        }
        float v = (float)eq_run(s, (double)y[i]);
        y[i] = fos_run_div(&s->lc, s->la0, &s->ls, v) * 0.05000000074505806f;
    }
    for (int j = 0; j < 10; j++)
        sos_block(&POST[j], &s->post[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 4.800000190734863f;
}

const struct wvfx_desc master_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x10A),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Master OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
