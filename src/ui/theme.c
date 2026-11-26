#include "ui/theme.h"
#include "fw/library.h"
#include <string.h>

/* oklch(0.78 0.13 h) / oklch(0.24 0.045 h) of the design, converted to sRGB */
static const uint32_t menu_bright[LIB_MAX_MENUS] = {
    [MENU_DYN] = 0x17D0D8, [MENU_FX] = 0xE698DF,  [MENU_WAH] = 0xFE93A4,  [MENU_DRV] = 0xFF958D,
    [MENU_AMP] = 0xF4A25C, [MENU_CAB] = 0xD1B64A, [MENU_IR] = 0xD1B64A,   [MENU_EQ] = 0x80CD82,
    [MENU_MOD] = 0xC4A4FE, [MENU_DLY] = 0x73BDFF, [MENU_RVB] = 0x36D2BA,  [MENU_CLONE] = 0xFD9976,
    [MENU_LOOP] = 0x9FB0FF, [MENU_SND] = 0x9FB0FF, [MENU_RTN] = 0x9FB0FF, [MENU_VOL] = 0xB5C159,
};
static const uint32_t menu_dark[LIB_MAX_MENUS] = {
    [MENU_DYN] = 0x002528, [MENU_FX] = 0x2B1729,  [MENU_WAH] = 0x31151A,  [MENU_DRV] = 0x321614,
    [MENU_AMP] = 0x2F1A07, [MENU_CAB] = 0x261F03, [MENU_IR] = 0x261F03,   [MENU_EQ] = 0x102511,
    [MENU_MOD] = 0x231A31, [MENU_DLY] = 0x0D2034, [MENU_RVB] = 0x002620,  [MENU_CLONE] = 0x31170E,
    [MENU_LOOP] = 0x191D34, [MENU_SND] = 0x191D34, [MENU_RTN] = 0x191D34, [MENU_VOL] = 0x1F2206,
};

uint32_t theme_menu_color(int menu)
{
    return menu >= 0 && menu < LIB_MAX_MENUS ? menu_bright[menu] : C_TEXT;
}

uint32_t theme_menu_dim(int menu)
{
    return menu >= 0 && menu < LIB_MAX_MENUS ? menu_dark[menu] : C_LINE;
}

uint32_t theme_led_colour(int led)
{
    /* enum led_colour: red, green, yellow, blue, magenta, cyan, white */
    static const uint32_t rgb_of[8] = { C_OFF, C_CLIP, C_TUNED, C_ACCENT, 0x5B8DEF, 0xD86BE0, 0x4FD1D9, C_TEXT };
    return led > 0 && led < 8 ? rgb_of[led] : C_OFF;
}

lv_obj_t *theme_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, rgb(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, rgb(color), 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *theme_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color)
{
    lv_obj_t *o = lv_image_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(o, rgb(color), 0);
    if (icon)
        lv_image_set_src(o, icon);
    return o;
}

void theme_set_icon(lv_obj_t *obj, const lv_image_dsc_t *icon, uint32_t color)
{
    if (lv_image_get_src(obj) != icon)
        lv_image_set_src(obj, icon);
    if (lv_color_to_u32(lv_obj_get_style_image_recolor(obj, 0)) != lv_color_to_u32(rgb(color)))
        lv_obj_set_style_image_recolor(obj, rgb(color), 0);
}

void theme_set_text(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (!cur || strcmp(cur, text) != 0)
        lv_label_set_text(label, text);
}

void theme_set_color(lv_obj_t *obj, uint32_t color)
{
    if (lv_color_to_u32(lv_obj_get_style_text_color(obj, 0)) != lv_color_to_u32(rgb(color)))
        lv_obj_set_style_text_color(obj, rgb(color), 0);
}

void theme_set_bg(lv_obj_t *obj, uint32_t color)
{
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, 0)) != lv_color_to_u32(rgb(color)))
        lv_obj_set_style_bg_color(obj, rgb(color), 0);
}

void theme_set_border(lv_obj_t *obj, uint32_t color)
{
    if (lv_color_to_u32(lv_obj_get_style_border_color(obj, 0)) != lv_color_to_u32(rgb(color)))
        lv_obj_set_style_border_color(obj, rgb(color), 0);
}
