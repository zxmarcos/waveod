/*
 * Visual language of the UI, from the "Display Multiefeitos" design (claude.ai/design): solid colours only,
 * filled rectangles and 1-2 px lines, one type family (Barlow Semi Condensed) in a few sizes, tabular
 * numbers. The design is drawn for 800 x 480; this panel is 480 x 272, so sizes are scaled by about 0.6.
 */
#pragma once

#include "lvgl.h"

/* palette */
#define C_BG          0x0A0A09
#define C_LINE        0x1F1E1C
#define C_TRACK       0x22211F
#define C_WIRE        0x3A3936
#define C_BORDER_OFF  0x2E2D2A
#define C_DIM         0x4A4844
#define C_OFF         0x5E5C57
#define C_LABEL       0x8A8780
#define C_TEXT        0xEDEBE6
#define C_NEUTRAL_ON  0xD8D5CE
#define C_ACCENT      0xF2AF48      /* amber */
#define C_TUNED       0x75DF8F      /* green: in tune, meter below -15 dB */
#define C_CLIP        0xFB5855      /* red: meter top segments */

/* layout (480 x 272) */
#define UI_W          480
#define UI_H          272
#define UI_PAD        14            /* side gutter */
#define UI_TOPBAR_H   44            /* meters, then buttons tall enough for a finger (the whole bar takes taps, ~9 mm) */

LV_FONT_DECLARE(font_label_12)
LV_FONT_DECLARE(font_tag_12)
LV_FONT_DECLARE(font_body_14)
LV_FONT_DECLARE(font_value_20)
LV_FONT_DECLARE(font_value_26)
LV_FONT_DECLARE(font_letter_26)
LV_FONT_DECLARE(font_title_40)
LV_FONT_DECLARE(font_hero_76)

static inline lv_color_t rgb(uint32_t c) { return lv_color_hex(c); }

/* Category colour of a menu: bright (block on) and dim (card background). Routing blocks are white. */
uint32_t theme_menu_color(int menu);
uint32_t theme_menu_dim(int menu);
uint32_t theme_led_colour(int led);              /* enum led_colour as a screen colour */

/* A plain rectangle with no padding, border, radius or scrolling. */
lv_obj_t *theme_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg);

/* A label with font, colour and letter spacing. */
lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int letter_space);

/* A line icon (ui/images/icons.h, an A8 alpha map) drawn in `color`; set changes source / colour when they differ. */
lv_obj_t *theme_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t color);
void theme_set_icon(lv_obj_t *obj, const lv_image_dsc_t *icon, uint32_t color);

/* Set a label's text only when it differs (avoids needless redraws). */
void theme_set_text(lv_obj_t *label, const char *text);
void theme_set_color(lv_obj_t *obj, uint32_t color);    /* text colour */
void theme_set_bg(lv_obj_t *obj, uint32_t color);
void theme_set_border(lv_obj_t *obj, uint32_t color);
