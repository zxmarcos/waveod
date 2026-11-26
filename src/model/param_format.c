#include "model/param_format.h"
#include "sdk/wvfx.h"

_Static_assert(PARAM_FORMAT_WV_SIGNED == WVFX_FMT_SIGNED && PARAM_FORMAT_WV_DEGREES == WVFX_FMT_DEGREES, "sdk/wvfx.h formats");

const char *const param_sync_divisions[11] = {
    "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16"
};

struct out_buf {
    char *p;
    unsigned left;
};

static void put_char(struct out_buf *o, char c)
{
    if (o->left > 1) {
        *o->p++ = c;
        o->left--;
    }
    *o->p = 0;
}

static void put_str(struct out_buf *o, const char *s)
{
    while (*s)
        put_char(o, *s++);
}

/* fixed-point text of v with `decimals` digits (no libc printf on the pedal) */
static void put_float(struct out_buf *o, float v, int decimals)
{
    static const float scale[] = { 1.0f, 10.0f, 100.0f, 1000.0f };
    if (decimals < 0)
        decimals = 0;
    if (decimals > 3)
        decimals = 3;
    if (v < 0) {
        float r = -v * scale[decimals] + 0.5f;
        if ((unsigned long)r)           /* no "-0" */
            put_char(o, '-');
        v = -v;
    }
    unsigned long n = (unsigned long)(v * scale[decimals] + 0.5f);
    unsigned long ip = n / (unsigned long)scale[decimals], fp = n % (unsigned long)scale[decimals];
    char t[24];
    int k = 0;
    do {
        t[k++] = (char)('0' + ip % 10);
        ip /= 10;
    } while (ip && k < 20);
    while (k)
        put_char(o, t[--k]);
    if (decimals) {
        put_char(o, '.');
        for (int d = decimals - 1; d >= 0; d--) {
            unsigned long div = 1;
            for (int i = 0; i < d; i++)
                div *= 10;
            put_char(o, (char)('0' + fp / div % 10));
        }
    }
}

/* expand a template with one "%.Nf" and "%%" */
static void put_template(struct out_buf *o, const char *t, float v)
{
    for (; *t; t++) {
        if (t[0] == '%' && t[1] == '%') {
            put_char(o, '%');
            t++;
        } else if (t[0] == '%' && t[1] == '.' && t[2] >= '0' && t[2] <= '9' && t[3] == 'f') {
            put_float(o, v, t[2] - '0');
            t += 3;
        } else if (t[0] == '%' && t[1] == 'f') {
            put_float(o, v, 2);
            t++;
        } else {
            put_char(o, *t);
        }
    }
}

static const struct param_format *format_of(const struct lib_param *p)
{
    static const struct param_format wv_signed = { FMT_SIGNED, 0, { "+%.0f", "%.0f" } };
    static const struct param_format wv_degrees = { FMT_PRINTF, 0, { "%.0f\xC2\xB0" } };
    return p->f3 < PARAM_FORMAT_COUNT ? &param_formats[p->f3] :
           p->f3 == PARAM_FORMAT_WV_SIGNED ? &wv_signed :
           p->f3 == PARAM_FORMAT_WV_DEGREES ? &wv_degrees : 0;
}

/* option i of a packed list "A\0B\0\0", or 0 */
static const char *packed_item(const char *s, int i)
{
    for (; *s; s++) {
        if (i-- == 0)
            return s;
        while (*s)
            s++;
    }
    return 0;
}

int param_list_size(const struct lib_param *p)
{
    int n = 0;
    if (p->options) {
        while (packed_item(p->options, n))
            n++;
        return n;
    }
    const struct param_format *f = format_of(p);
    if (!f || f->kind != FMT_LIST)
        return 0;
    while (f->list[n])
        n++;
    return n;
}

const char *param_list_item(const struct lib_param *p, int i)
{
    if (i < 0)
        return 0;
    if (p->options)
        return packed_item(p->options, i);
    const struct param_format *f = format_of(p);
    return i < param_list_size(p) ? f->list[i] : 0;
}

void param_format(const struct lib_param *p, float value, int synced, char *out, unsigned size)
{
    struct out_buf o = { out, size };
    out[0] = 0;
    if (synced) {
        int i = (int)(value + 0.5f);
        put_str(&o, i >= 0 && i < 11 ? param_sync_divisions[i] : "?");
        return;
    }
    if (p->unit && !p->options) {                   /* a block's unit, before any format code */
        put_template(&o, p->unit, value);
        return;
    }
    if (p->options || (format_of(p) && format_of(p)->kind == FMT_LIST)) {
        const char *s = param_list_item(p, (int)(value + (value < 0 ? -0.5f : 0.5f)));
        if (s)
            put_str(&o, s);
        else
            put_float(&o, value, 0);
        return;
    }
    const struct param_format *f = format_of(p);
    int kind = f ? f->kind : FMT_NUMBER;
    switch (kind) {
    case FMT_PRINTF:
        put_template(&o, f->tmpl[0], value);
        return;
    case FMT_MIN_WORD:
        if (value <= p->min)
            put_str(&o, f->tmpl[0]);
        else
            put_template(&o, f->tmpl[1], value);
        return;
    case FMT_MAX_WORD:
        if (value >= p->max)
            put_str(&o, f->tmpl[0]);
        else
            put_template(&o, f->tmpl[1], value);
        return;
    case FMT_PAN:
        if (value > -0.5f && value < 0.5f)
            put_str(&o, f->tmpl[0]);
        else if (value > 0)
            put_template(&o, f->tmpl[1], value);
        else
            put_template(&o, f->tmpl[2], -value);
        return;
    case FMT_SIGNED:
        put_template(&o, value > 0 ? f->tmpl[0] : f->tmpl[1], value);
        return;
    case FMT_THOUSANDS:
        if (value >= 1000.0f)
            put_template(&o, f->tmpl[1], value / 1000.0f);
        else
            put_template(&o, f->tmpl[0], value);
        return;
    default:
        put_float(&o, value, 0);
        return;
    }
}
