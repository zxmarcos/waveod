#include "model/expression.h"
#include "platform/time.h"
#include <string.h>

#define POLL_MS     10
#define TAN_0_3     0.30933624f             /* tan(0.3) */

void expression_init(struct expression *x)
{
    memset(x, 0, sizeof *x);
    x->pos[0] = x->pos[1] = 1.0f;           /* the original starts with both pedals at the top (FUN_000399F0) */
}

/* atan for |y| <= tan(0.3) (series, error below 1e-6 there) */
static float small_atan(float y)
{
    float y2 = y * y;
    return y * (1.0f - y2 * (1.0f / 3.0f - y2 * (0.2f - y2 * (1.0f / 7.0f - y2 / 9.0f))));
}

/* FUN_0001AB10: 1 when the averaged value is worth reporting */
static int changed(struct expression *x, int n, int raw)
{
    x->filtered[n] = (int16_t)((raw + 3 * x->filtered[n]) >> 2);
    int d = x->filtered[n] - x->reported[n];
    if (d < 0)
        d = -d;
    if (d < 16) {
        x->drift[n] = 0;
        return 0;
    }
    int before = x->drift[n];
    if (x->drift[n] < 0xff)
        x->drift[n]++;
    if (before <= 20 && d < 64)
        return 0;
    x->reported[n] = x->filtered[n];
    return 1;
}

/* FUN_000200D0 for pedal n with value v (0..255) */
static void pedal(struct expression *x, struct rig *r, const struct globals *g, int n, int v, control_run_fn run,
                  void *ctx)
{
    int lo = g->exp_min[n], hi = g->exp_max[n];
    if (n == 0) {
        int thr = g->exp_toe[0];
        if (!x->toe && v > thr) {
            x->toe = 1;
            control_press(r, CK_TOE, CM_ALL, 0, run, ctx);
        } else if (x->toe && v < (hi + 2 * thr) / 3) {
            x->toe = 0;
        }
    }
    if (v > hi)
        v = hi;
    if (v < lo)
        v = lo;
    float lin = hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f;
    x->pos[n] = n == 0 ? small_atan(TAN_0_3 * lin) / 0.3f : lin;
    rig_exp(r, n == 0 ? r->preset.exp_group : 2, x->pos[n]);
}

int expression_poll(struct expression *x, struct rig *r, const struct globals *g, const int raw[2],
                    control_run_fn run, void *ctx)
{
    uint32_t now = time_ms();
    if (now - x->polled_at < POLL_MS)
        return 0;
    x->polled_at = now;
    int moved = 0;
    for (int n = 0; n < 2; n++)
        if (changed(x, n, raw[n]) && (n == 0 || g->exp2_jack == EXP2_PEDAL)) {   /* FUN_000398A2 */
            pedal(x, r, g, n, x->reported[n] >> 4, run, ctx);
            moved = 1;
        }
    return control_exp_tick(r, now) || moved;
}

void expression_learn_start(struct expression *x)
{
    for (int n = 0; n < 2; n++)
        x->base[n] = (int16_t)(x->filtered[n] >> 4);
    x->learn_key = -1;
}

int expression_learn(struct expression *x, const struct rig *r, const struct globals *g, const int raw[2])
{
    uint32_t now = time_ms();
    if (now - x->polled_at < POLL_MS)
        return -1;
    x->polled_at = now;
    for (int n = 0; n < 2; n++) {
        x->filtered[n] = (int16_t)((raw[n] + 3 * x->filtered[n]) >> 2);
        if (n == 1 && g->exp2_jack != EXP2_PEDAL)
            continue;
        int v = x->filtered[n] >> 4, lo = g->exp_min[n], hi = g->exp_max[n];
        if (n == 0) {
            int thr = g->exp_toe[0];
            if (!x->toe && v > thr) {
                x->toe = 1;
                return CK_TOE;
            }
            if (x->toe && v < (hi + 2 * thr) / 3)
                x->toe = 0;
        }
        int d = v - x->base[n], span = hi > lo ? hi - lo : 255;
        if ((d < 0 ? -d : d) * 4 >= span) {
            int key = n == 1 ? CK_EXP2 : r->preset.exp_group ? CK_EXP1B : CK_EXP1A;
            int still = v - x->learn_v[n];
            if (x->learn_key != key || still > 2 || still < -2) {       /* moving: the rest starts again */
                x->learn_at = now;
                x->learn_v[n] = (int16_t)v;
            }
            x->learn_key = (int8_t)key;
        }
    }
    if (x->learn_key >= 0 && now - x->learn_at >= LEARN_REST_MS) {
        int k = x->learn_key;
        x->learn_key = -1;
        return k;
    }
    return -1;
}

int expression_value(const struct expression *x, int n)
{
    return n >= 0 && n < 2 ? x->filtered[n] >> 4 : 0;
}

/* on a preset load (FUN_00021666 calls FUN_000201D8 without telling the editor) */
void expression_apply(const struct expression *x, struct rig *r)
{
    r->quiet++;
    rig_exp(r, r->preset.exp_group, x->pos[0]);
    rig_exp(r, 2, x->pos[1]);
    r->quiet--;
}
