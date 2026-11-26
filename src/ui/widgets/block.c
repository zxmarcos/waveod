#include "ui/widgets/block.h"
#include "ui/theme.h"

void block_create(struct block *b, lv_obj_t *parent, int x, int y, int w, int h)
{
    b->box = theme_box(parent, x, y, w, h, C_BG);
    lv_obj_set_style_radius(b->box, 2, 0);
    lv_obj_set_style_border_width(b->box, 2, 0);
    lv_obj_set_style_border_color(b->box, rgb(C_BORDER_OFF), 0);
    lv_obj_set_style_outline_width(b->box, 0, 0);
    lv_obj_set_style_outline_pad(b->box, 2, 0);
    lv_obj_set_style_outline_color(b->box, rgb(C_TEXT), 0);
    b->text = theme_label(b->box, &font_tag_12, C_OFF, 1);
    lv_obj_center(b->text);
}

void block_set(struct block *b, const char *text, uint32_t bg, uint32_t border, uint32_t fg, int ring)
{
    theme_set_text(b->text, text);
    lv_obj_center(b->text);
    theme_set_bg(b->box, bg);
    theme_set_border(b->box, border);
    theme_set_color(b->text, fg);
    int want = ring ? 2 : 0;
    if (lv_obj_get_style_outline_width(b->box, 0) != want)
        lv_obj_set_style_outline_width(b->box, want, 0);
}

void block_set_effect(struct block *b, const char *text, int menu, int on, int ring)
{
    uint32_t c = theme_menu_color(menu);
    if (on)
        block_set(b, text, c, c, C_BG, ring);
    else
        block_set(b, text, C_BG, C_BORDER_OFF, C_OFF, ring);
}

void block_set_empty(struct block *b, int ring)
{
    block_set(b, "+", C_BG, C_BORDER_OFF, C_OFF, ring);
}
