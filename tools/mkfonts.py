"""Rasterize the UI fonts (Barlow Semi Condensed, SIL OFL 1.1) into LVGL C fonts.

Needs Node.js (runs lv_font_conv through npx). Output: src/ui/fonts/*.c, then commit them, so that a
normal build does not need Node.

    python tools/mkfonts.py [font_name ...]
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'assets', 'fonts', 'src')
OUT = os.path.join(ROOT, 'src', 'ui', 'fonts')
CONV = 'lv_font_conv@1.5.3'

TEXT = '0x20-0x7E,0xA0-0xFF,0x2013,0x2212'
HERO = '0x20,0x2D,0x30-0x39,0x41-0x5A'
NOTE = '0x23,0x2D,0x41-0x47'                         # tuner: note letters, sharp, dash
BACK = TEXT + ',0x2039'                              # + the back chevron (top bar, hints)

# name, weight, size, ranges
FONTS = [
    ('font_label_12', 'SemiBold', 12, BACK),
    ('font_tag_12', 'Bold', 12, TEXT),
    ('font_body_14', 'Medium', 14, TEXT),
    ('font_value_20', 'SemiBold', 20, BACK),
    ('font_value_26', 'SemiBold', 26, TEXT),
    ('font_letter_26', 'Bold', 26, TEXT),
    ('font_title_40', 'SemiBold', 40, TEXT),
    ('font_hero_76', 'Bold', 76, HERO),
    ('font_note_140', 'Bold', 140, NOTE),
]


def main():
    only = sys.argv[1:]
    npx = shutil.which('npx') or shutil.which('npx.cmd')
    if not npx:
        sys.exit('npx (Node.js) not found')
    os.makedirs(OUT, exist_ok=True)
    for name, weight, size, ranges in FONTS:
        if only and name not in only:
            continue
        ttf = os.path.join(SRC, 'BarlowSemiCondensed-%s.ttf' % weight)
        out = os.path.join(OUT, name + '.c')
        cmd = [npx, '--yes', CONV, '--font', ttf, '-r', ranges, '--size', str(size), '--bpp', '4',
               '--format', 'lvgl', '--no-compress', '--lv-include', 'lvgl.h', '--lv-font-name', name, '-o', out]
        subprocess.run(cmd, check=True)
        print('%s: %s %dpx' % (name, weight, size))


if __name__ == '__main__':
    main()
