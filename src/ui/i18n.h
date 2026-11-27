/*
 * The pedal's texts in the chosen language (docs/i18n.md). The catalog is the JSON files of i18n/<language>/; tools/i18n.py
 * gen writes the tables (ui/i18n_msgs.h / .c: enum i18n_msg, enum i18n_lang). A screen writes T(MSG_X) where it wrote
 * the text; arrays of texts become arrays of message ids.
 *
 * The language is a setting (settings.language, GLOBAL › TELA › IDIOMA, the editor's protocol). When it changes,
 * ui.c builds every screen again (ui_relang), so texts set once at creation follow too.
 */
#pragma once

#include "ui/i18n_msgs.h"
#include <stdint.h>

extern uint8_t i18n_lang;                   /* enum i18n_lang */

static inline const char *T(enum i18n_msg m)
{
    return i18n_texts[i18n_lang][m];
}

/* the language from now on (out of range: the reference, English) */
void i18n_set(int lang);
