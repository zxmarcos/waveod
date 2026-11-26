/*
 * A chain block / tag: solid rectangle with a 2 px border, centred bold text and an optional selection
 * ring (outline) around it.
 */
#pragma once

#include "lvgl.h"

struct block {
    lv_obj_t *box, *text;
};

void block_create(struct block *b, lv_obj_t *parent, int x, int y, int w, int h);
void block_set(struct block *b, const char *text, uint32_t bg, uint32_t border, uint32_t fg, int ring);

/* Style of a chain block for an effect of `menu` that is on/off (routing blocks pass menu -1). */
void block_set_effect(struct block *b, const char *text, int menu, int on, int ring);

/* Empty "+" slot. */
void block_set_empty(struct block *b, int ring);
