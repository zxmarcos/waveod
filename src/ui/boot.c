#include "ui/boot.h"
#include "platform/time.h"
#include "ui/images/logo.h"
#include "ui/theme.h"

#define WAVE_START   150                    /* ms after start */
#define WAVE_MS      750
#define WORD_START   700
#define WORD_MS      500
#define WORD_RISE    14                     /* px the name rises */
#define TAG_START    1100
#define TAG_MS       450
#define END_MS       2350

static struct {
    lv_obj_t *screen, *wave_clip, *word, *tagline;
    uint32_t started;
    int running, finished;
} b;

static void set_width(void *obj, int32_t v)
{
    lv_obj_set_width(obj, v);
}

static void set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_image_opa(obj, (lv_opa_t)v, 0);
}

static void set_y(void *obj, int32_t v)
{
    lv_obj_set_y(obj, v);
}

static void animate(void *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t delay, uint32_t ms,
                    lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static lv_obj_t *image(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

static void touched(lv_event_t *e)
{
    (void)e;
    boot_skip();
}

void boot_start(void)
{
    b.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(b.screen);
    lv_obj_set_style_bg_color(b.screen, lv_color_black(), 0);        /* the logo's own black */
    lv_obj_set_style_bg_opa(b.screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(b.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b.screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b.screen, touched, LV_EVENT_PRESSED, 0);
    int x0 = (UI_W - LOGO_W) / 2, y0 = (UI_H - LOGO_H) / 2;

    /* the wave: a box that widens over the picture, which stays put (children are clipped to it) */
    b.wave_clip = theme_box(b.screen, x0 + LOGO_WAVE_X, y0 + LOGO_WAVE_Y, 0, logo_wave.header.h, 0);
    lv_obj_set_style_bg_opa(b.wave_clip, LV_OPA_TRANSP, 0);
    image(b.wave_clip, &logo_wave, 0, 0);
    b.word = image(b.screen, &logo_word, x0 + LOGO_WORD_X, y0 + LOGO_WORD_Y + WORD_RISE);
    set_opa(b.word, LV_OPA_TRANSP);
    b.tagline = image(b.screen, &logo_tagline, x0 + LOGO_TAGLINE_X, y0 + LOGO_TAGLINE_Y);
    set_opa(b.tagline, LV_OPA_TRANSP);

    animate(b.wave_clip, set_width, 0, logo_wave.header.w, WAVE_START, WAVE_MS, lv_anim_path_ease_in_out);
    animate(b.word, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, WORD_START, WORD_MS, lv_anim_path_linear);
    animate(b.word, set_y, y0 + LOGO_WORD_Y + WORD_RISE, y0 + LOGO_WORD_Y, WORD_START, WORD_MS, lv_anim_path_ease_out);
    animate(b.tagline, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, TAG_START, TAG_MS, lv_anim_path_linear);

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_HIDDEN);           /* top bar and notices wait */
    lv_screen_load(b.screen);
    b.started = time_ms();
    b.running = 1;
}

int boot_running(void)
{
    return b.running;
}

void boot_skip(void)
{
    if (b.running)
        b.started = time_ms() - END_MS;
}

int boot_done(void)
{
    if (!b.running || time_ms() - b.started < END_MS)
        return 0;
    b.running = 0;
    lv_anim_delete(b.wave_clip, 0);
    lv_anim_delete(b.word, 0);
    lv_anim_delete(b.tagline, 0);
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_HIDDEN);
    return 1;
}
