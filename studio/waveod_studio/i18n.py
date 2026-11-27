"""The Studio backend's messages in the page's language (docs/i18n.md): tr('key', name=value) from the shared catalog
(i18n_strings.py, made by tools/i18n.py gen from i18n/<language>/*.json). The page tells the language
(webapp.Api.set_language); the command line uses the system's."""
import locale
import os

from waveod_studio.i18n_strings import LANGUAGES, STRINGS

_lang = LANGUAGES[0]


def match(tag):
    """the catalog's language for a tag like pt_BR.UTF-8 / pt-BR / pt, or None"""
    if not tag:
        return None
    t = tag.split('.')[0].replace('_', '-').lower()
    for code in LANGUAGES:
        if code.lower() == t:
            return code
    for code in LANGUAGES:
        if code.lower().split('-')[0] == t.split('-')[0]:
            return code
    return None


def system_language():
    for tag in (os.environ.get('LC_ALL'), os.environ.get('LC_MESSAGES'), os.environ.get('LANG'),
                (locale.getlocale()[0] if locale.getlocale() else None)):
        code = match(tag)
        if code:
            return code
    return LANGUAGES[0]


def set_language(code):
    global _lang
    _lang = code if code in STRINGS else LANGUAGES[0]


def language():
    return _lang


def tr(key, **params):
    s = STRINGS.get(_lang, {}).get(key) or STRINGS[LANGUAGES[0]].get(key) or key
    for k, v in params.items():
        s = s.replace('{%s}' % k, str(v))
    return s


set_language(system_language())
