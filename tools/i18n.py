"""waveod's translations: one catalog for the pedal and waveod Studio (docs/i18n.md).

    python tools/i18n.py gen              the tables from i18n/ (firmware, Studio page, Studio backend)
    python tools/i18n.py check [--strict] what is missing or wrong (CI: --strict, missing texts are errors too)
    python tools/i18n.py scan FILE...     the string literals of source files, to move them into the catalog

i18n/languages.json lists the languages in their stored order (the pedal keeps the index in its settings: only
ever append) with their names ("name" for the Studio, "pedal" in the pedal's upper case); the first is the
reference and the fallback (English). i18n/<code>/*.json are flat objects
{"key": "text"}, split by area; a key is in one file only. Keys:
  fw.*      the pedal (C, printf-style placeholders: %d, %s, %.1f...; the same ones in the same order in every
            language, lv_snprintf has no positional arguments)
  studio.*  waveod Studio, the page and the Python backend ({name} placeholders)
  shared.*  texts the two show alike (no placeholders)
The pedal's texts are written as the screen shows them (upper case where it is upper case) and only with the
characters its fonts have (src/ui/fonts: Latin-1 and a few more; `check` says which are missing).

Generated (committed, `check` fails when they are stale):
  src/ui/i18n_msgs.h / .c                    enum i18n_msg (MSG_<KEY without fw.>), the tables, the language names
  studio/ui/src/lib/i18n/strings.ts          the page's dictionaries and the type of its keys
  studio/waveod_studio/i18n_strings.py       the backend's dictionaries
A text missing in a language is the reference's in the tables.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAT = os.path.join(ROOT, 'i18n')
FW_H = os.path.join(ROOT, 'src', 'ui', 'i18n_msgs.h')
FW_C = os.path.join(ROOT, 'src', 'ui', 'i18n_msgs.c')
TS = os.path.join(ROOT, 'studio', 'ui', 'src', 'lib', 'i18n', 'strings.ts')
PY = os.path.join(ROOT, 'studio', 'waveod_studio', 'i18n_strings.py')
FONTS = os.path.join(ROOT, 'src', 'ui', 'fonts')
TEXT_FONTS = ('font_body_14', 'font_label_12', 'font_tag_12', 'font_value_20', 'font_value_26', 'font_letter_26',
              'font_title_40')                       # the fonts texts are drawn with (not the big digits)

PRINTF = re.compile(r'%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z)?[diuxXfFeEgGcsp%]')
BRACES = re.compile(r'\{(\w+)\}')
KEY = re.compile(r'^(fw|studio|shared)\.[a-z0-9_]+(\.[a-z0-9_]+)*$')


def languages():
    with open(os.path.join(CAT, 'languages.json'), encoding='utf-8') as f:
        return json.load(f)


def load():
    """{code: {key: text}}, {key: file} of the reference; problems found while reading"""
    langs, out, where, problems = languages(), {}, {}, []
    for lang in langs:
        code, texts = lang['code'], {}
        d = os.path.join(CAT, code)
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if not name.endswith('.json'):
                continue
            with open(os.path.join(d, name), encoding='utf-8') as f:
                part = json.load(f)
            for k, v in part.items():
                if not isinstance(v, str):
                    problems.append('%s/%s: %s is not a text' % (code, name, k))
                elif k in texts:
                    problems.append('%s/%s: %s is in two files' % (code, name, k))
                else:
                    texts[k] = v
                    if code == langs[0]['code']:
                        where[k] = name
        out[code] = texts
    return out, where, problems


def specs(text, key):
    if key.startswith('fw.'):
        return [m.group(0) for m in PRINTF.finditer(text) if m.group(0) != '%%']
    if key.startswith('studio.'):
        return sorted(set(BRACES.findall(text)))
    return []


def enum_name(key):
    return 'MSG_' + re.sub(r'[^A-Za-z0-9]', '_', key[3:] if key.startswith('fw.') else key).upper()


def lang_enum(code):
    return 'LANG_' + re.sub(r'[^A-Za-z0-9]', '_', code).upper()


def c_string(text):
    out, prev_hex = ['"'], False
    for b in text.encode('utf-8'):
        ch = chr(b)
        if prev_hex and ch in '0123456789abcdefABCDEF':
            out.append('" "')
        prev_hex = False
        if b == 0x22:
            out.append('\\"')
        elif b == 0x5C:
            out.append('\\\\')
        elif b == 0x0A:
            out.append('\\n')
        elif 0x20 <= b < 0x7F:
            out.append(ch)
        else:
            out.append('\\x%02X' % b)
            prev_hex = True
    out.append('"')
    return ''.join(out)


def table(texts, ref, keys):
    """every key's text in a language, the reference's where it has none"""
    return [texts.get(k, ref[k]) for k in keys]


def font_chars():
    """the code points every text font has (the intersection of their -r ranges)"""
    common = None
    for name in TEXT_FONTS:
        path = os.path.join(FONTS, name + '.c')
        if not os.path.exists(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            head = f.read(4000)
        m = re.search(r'-r ([0-9A-Fa-fx,\-]+)', head)
        if not m:
            continue
        have = set()
        for part in m.group(1).split(','):
            lo, _, hi = part.partition('-')
            have.update(range(int(lo, 16), int(hi or lo, 16) + 1))
        common = have if common is None else common & have
    return common or set(range(0x20, 0x7F))


def generate(cat, langs):
    ref = cat[langs[0]['code']]
    fw = [k for k in ref if k.startswith('fw.') or k.startswith('shared.')]
    st = [k for k in ref if k.startswith('studio.') or k.startswith('shared.')]
    files = {}

    h = ['/* Generated by tools/i18n.py from i18n/: do not edit (docs/i18n.md). */', '#pragma once', '',
         'enum i18n_lang {']
    h += ['    %s,%s' % (lang_enum(l['code']), '  /* the reference and the default */' if i == 0 else '')
          for i, l in enumerate(langs)]
    h += ['    LANG_COUNT', '};', '', 'enum i18n_msg {']
    h += ['    %s,' % enum_name(k) for k in fw]
    h += ['    MSG_COUNT', '};', '',
          'extern const char *const i18n_texts[LANG_COUNT][MSG_COUNT];',
          'extern const char *const i18n_lang_names[LANG_COUNT];     /* each in its own language */', '']
    files[FW_H] = '\n'.join(h)

    c = ['/* Generated by tools/i18n.py from i18n/: do not edit (docs/i18n.md). */', '#include "ui/i18n_msgs.h"',
         '#include "ui/sdram_code.h" /* the texts in SDRAM: ITCM is kept for the audio path */', '',
         'const char *const i18n_lang_names[LANG_COUNT] = {']
    c += ['    %s,' % c_string(l.get('pedal', l['name'])) for l in langs]
    c.append('};')
    # One array of texts per language (each text once), so the section pragma places them too: it does not
    # apply to string literals, which would land in ITCM.
    offsets = {}
    for l in langs:
        pool, at, n = [], {}, 0
        for text in table(cat[l['code']], ref, fw):
            if text not in at:
                at[text] = n
                pool.append('    %s "\\0"' % c_string(text))
                n += len(text.encode('utf-8')) + 1
        offsets[l['code']] = at
        c += ['', 'static const char pool_%s[%d] =' % (lang_enum(l['code']).lower(), n)] + pool + ['    ;']
    c += ['', 'const char *const i18n_texts[LANG_COUNT][MSG_COUNT] = {']
    for l in langs:
        p, at = 'pool_' + lang_enum(l['code']).lower(), offsets[l['code']]
        c.append('    [%s] = {' % lang_enum(l['code']))
        for k, text in zip(fw, table(cat[l['code']], ref, fw)):
            c.append('        [%s] = %s + %d,' % (enum_name(k), p, at[text]))
        c.append('    },')
    c += ['};', '']
    files[FW_C] = '\n'.join(c)

    def strip(k):
        return k[7:] if k.startswith('studio.') else k
    t = ['// Generated by tools/i18n.py from i18n/: do not edit (docs/i18n.md).', '',
         'export const LANGUAGES = %s as const;' % json.dumps([dict(code=l['code'], name=l['name']) for l in langs],
                                                              ensure_ascii=False),
         'export type Lang = %s;' % ' | '.join("'%s'" % l['code'] for l in langs),
         'export type MsgKey =']
    t += ["  | '%s'" % strip(k) for k in st] or ["  never"]
    t[-1] += ';'
    t += ['', 'export const STRINGS: Record<Lang, Record<MsgKey, string>> = {']
    for l in langs:
        t.append("  '%s': {" % l['code'])
        for k, text in zip(st, table(cat[l['code']], ref, st)):
            t.append('    %s: %s,' % (json.dumps(strip(k)), json.dumps(text, ensure_ascii=False)))
        t.append('  },')
    t += ['};', '']
    files[TS] = '\n'.join(t)

    p = ['"""Generated by tools/i18n.py from i18n/: do not edit (docs/i18n.md)."""', '',
         'LANGUAGES = %r' % [l['code'] for l in langs], '', 'STRINGS = {']
    for l in langs:
        p.append('    %r: {' % l['code'])
        for k, text in zip(st, table(cat[l['code']], ref, st)):
            p.append('        %r: %r,' % (strip(k), text))
        p.append('    },')
    p += ['}', '']
    files[PY] = '\n'.join(p)
    return files


def gen(_a):
    langs = languages()
    cat, _, problems = load()
    if problems:
        sys.exit('\n'.join(problems))
    for path, text in generate(cat, langs).items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('%s' % os.path.relpath(path, ROOT))


def check(a):
    langs = languages()
    cat, where, problems = load()
    errors, warnings = list(problems), []
    ref_code = langs[0]['code']
    ref = cat[ref_code]
    chars = font_chars()
    for k in ref:
        if not KEY.match(k):
            errors.append('%s: not a key (fw. / studio. / shared. and a-z 0-9 _)' % k)
    for l in langs:
        code, texts = l['code'], cat[l['code']]
        for k in texts:
            if k not in ref:
                errors.append('%s: %s is not in %s' % (code, k, ref_code))
        missing = [k for k in ref if k not in texts]
        if missing:
            (errors if a.strict else warnings).append('%s: %d texts missing (%s...)' % (
                code, len(missing), ', '.join(missing[:5])))
        for k, text in texts.items():
            if k not in ref:
                continue
            if specs(text, k) != specs(ref[k], k):
                errors.append('%s: %s has %s, %s has %s' % (code, k, specs(text, k), ref_code, specs(ref[k], k)))
            if k.startswith('shared.') and (PRINTF.search(text.replace('%%', '')) or BRACES.search(text)):
                errors.append('%s: %s is shared, it cannot have placeholders' % (code, k))
            if k.startswith(('fw.', 'shared.')):
                bad = sorted({ch for ch in text if ord(ch) not in chars})
                if bad:
                    errors.append('%s: %s uses %s, not in the pedal\'s fonts (tools/i18n.py: docs/i18n.md)' % (
                        code, k, ' '.join('U+%04X' % ord(ch) for ch in bad)))
    for path, text in generate(cat, langs).items():
        try:
            with open(path, encoding='utf-8') as f:
                same = f.read() == text
        except OSError:
            same = False
        if not same:
            errors.append('%s is stale: python tools/i18n.py gen' % os.path.relpath(path, ROOT))
    unused = []
    if a.unused:
        srcs = '\n'.join(_sources())
        for k in ref:
            name = enum_name(k) if k.startswith(('fw.', 'shared.')) else None
            sk = k[7:] if k.startswith('studio.') else k
            base = re.sub(r'_(zero|one|two|few|many|other)$', '', sk)      # tn('base', n)
            stem = sk.rsplit('_', 1)[0]                                     # 'area.name_' + id, `area.name_${id}`
            if (name and name in srcs) or any(q % s in srcs for s in {sk, base} for q in ("'%s'", '"%s"')) or \
                    ("'%s_" % stem) in srcs or ('`%s_${' % stem) in srcs or ("'%s" % stem.rsplit('.', 1)[0] + '.') in srcs \
                    and k.startswith('studio.py.area_'):
                continue
            unused.append(k)
        if unused:
            warnings.append('%d keys used nowhere: %s' % (len(unused), ', '.join(unused[:20])))
    for w in warnings:
        print('warning: ' + w)
    for e in errors:
        print('error: ' + e)
    print('%d keys, %s; %d errors, %d warnings' % (
        len(ref), ', '.join('%s %d' % (l['code'], len(cat[l['code']])) for l in langs), len(errors), len(warnings)))
    return 1 if errors else 0


def _sources():
    out = []
    for base in (os.path.join(ROOT, 'src'), os.path.join(ROOT, 'studio', 'ui', 'src'),
                 os.path.join(ROOT, 'studio', 'waveod_studio')):
        for d, _, files in os.walk(base):
            for n in files:
                if n.endswith(('.c', '.h', '.ts', '.svelte', '.py')) and not n.startswith(('i18n_msgs', 'i18n_strings',
                                                                                             'strings.ts')):
                    with open(os.path.join(d, n), encoding='utf-8', errors='replace') as f:
                        out.append(f.read())
    return out


LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def c_unescape(s):
    raw = bytearray()
    i = 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            n = s[i + 1]
            if n == 'x':
                j = i + 2
                while j < len(s) and j < i + 4 and s[j] in '0123456789abcdefABCDEF':
                    j += 1
                raw.append(int(s[i + 2:j], 16))
                i = j
                continue
            if n in '01234567':
                j = i + 1
                while j < len(s) and j < i + 4 and s[j] in '01234567':
                    j += 1
                raw.append(int(s[i + 1:j], 8))
                i = j
                continue
            raw += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\'}.get(n, n.encode())
            i += 2
            continue
        raw += s[i].encode('utf-8')
        i += 1
    return raw.decode('utf-8', 'replace')


def scan(a):
    for path in a.files:
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        for n, line in enumerate(lines, 1):
            code = line.split('//')[0]
            if code.lstrip().startswith(('#include', '*', '/*')):
                continue
            joined = ''.join(m.group(1) for m in LITERAL.finditer(code))
            if re.search(r'[A-Za-z\x80-\xff]{2}|\\x[89A-Fa-f]|\\3', joined):
                print('%s:%d: %s' % (os.path.relpath(path, ROOT), n, c_unescape(joined)))


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('gen').set_defaults(fn=gen)
    c = sub.add_parser('check')
    c.add_argument('--strict', action='store_true', help='missing texts are errors')
    c.add_argument('--unused', action='store_true', help='also list the keys no source names')
    c.set_defaults(fn=check)
    s = sub.add_parser('scan')
    s.add_argument('files', nargs='+')
    s.set_defaults(fn=scan)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == '__main__':
    main()
