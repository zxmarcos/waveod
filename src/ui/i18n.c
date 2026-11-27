#include "ui/i18n.h"

uint8_t i18n_lang = LANG_EN;

void i18n_set(int lang)
{
    i18n_lang = (uint8_t)(lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN);
}
