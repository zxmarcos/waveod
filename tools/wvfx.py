"""waveod's effect blocks as modules (docs/blocks.md): build a block into a module ELF, pack modules into a
library for the flash, and show what a library holds.

    python tools/wvfx.py build src/blocks/stereo_util.c [-o out/fx/stereo_util.elf] [--desc NAME] [-D NAME=VALUE]
    python tools/wvfx.py lib OUT.bin A.elf [B.elf ...] [--as A.elf=0x570F0101:NAME]
    python tools/wvfx.py show LIB.bin
    python tools/wvfx.py new "My Block" [--menu DRV] [--category NAME] [--id N] [-o my_block.c]

build: the block's source with src/sdk/wvfx_module.c (the header, -DWVFX_DESC=<file name>_block unless --desc)
and src/sdk/wvfx_rt.c, by the Arm Toolchain for Embedded's clang (tools/wvbuild.py) for the Cortex-M7 with its FPU
(no movw / movt: addresses come from literal pools), linked by its ld.lld with src/sdk/wvfx.ld keeping the
relocations, then rewritten into the form the
loader takes (engine/module_loader.h): the four regions, every absolute address an R_ARM_ABS32 against its
region with the offset in place, PC-relative references inside a region dropped. A block keeps its state in
the memory the host gives it: writable sections (mutable statics) are refused.

lib: the library at FLASH_WV_LIBRARY (fw/wv_library.h): the container of the original's effect library
(fw/library.h) with "WVLB" and a CRC-32 of everything after the header; each entry's name, menu, parameters
come from the module's descriptor. --as packs a module under another id and name (a test copy of a built-in).

new: a block's source to start from (a level and a tone, its descriptor named after the file, an id from the name
in 0x8000..0xFFFF, how to measure it on the pedal: docs/bench.md).
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'studio'))
from waveod_studio import wvlib                                     # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wvbuild                                                      # noqa: E402
SDK = os.path.join(ROOT, 'src', 'sdk')
CFLAGS = ['--target=armv7em-none-eabihf', '-mcpu=cortex-m7', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-mthumb',
          '-mno-movt', '-O2', '-ffp-contract=off', '-ffreestanding', '-nostdlibinc', '-fno-pic', '-ffunction-sections',
          '-fdata-sections', '-fno-unwind-tables',
          '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-stack-protector', '-fno-sanitize=all',
          '-Wall', '-I', os.path.join(ROOT, 'src')]
REGIONS = ['ER_IROM', 'ER_SROM', 'ER_IRAM', 'ER_SRAM']
FLAGS = {'ER_IROM': 6, 'ER_SROM': 2, 'ER_IRAM': 3, 'ER_SRAM': 3}
ABS32 = {2, 38}                                  # R_ARM_ABS32, R_ARM_TARGET1
PC_REL = {3, 4, 5, 6, 7, 8, 9, 10, 11, 25, 26, 27, 28, 29, 30, 41, 42, 49, 50, 51, 52, 53, 54, 55, 56, 57, 81,
          82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 102, 103}
WVFX_MAGIC, MODULE_MAGIC = 0x58465657, 0xFFAA55AA


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('failed: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))


class Elf:
    def __init__(self, b):
        self.b = b
        shoff, = struct.unpack_from('<I', b, 32)
        shnum, shstrndx = struct.unpack_from('<HH', b, 48)
        self.secs = [list(struct.unpack_from('<10I', b, shoff + 40 * i)) for i in range(shnum)]
        so = self.secs[shstrndx][4]
        self.names = [b[so + s[0]:b.index(b'\0', so + s[0])].decode() for s in self.secs]
        self.index = {n: i for i, n in enumerate(self.names)}
        self.syms = []
        if '.symtab' in self.index:
            st = self.secs[self.index['.symtab']]
            strs = self.secs[st[6]]
            for j in range(st[5] // 16):
                nm, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', b, st[4] + 16 * j)
                self.syms.append((b[strs[4] + nm:b.index(b'\0', strs[4] + nm)].decode(), val, shndx))

    def data(self, i):
        s = self.secs[i]
        return None if s[1] == 8 else bytearray(self.b[s[4]:s[4] + s[5]])


# ---------------------------------------------------------------- build

def convert(raw):
    """the linked ELF -> the loader's form; (bytes, region sizes, relocations kept)"""
    e = Elf(raw)
    for name, _, shndx in e.syms[1:]:
        if shndx == 0 and name:
            sys.exit('undefined symbol %r: a block has no library (sdk/wvfx.h helpers, or write it)' % name)
    regions = {}
    for n in REGIONS:
        if n in e.index and e.secs[e.index[n]][5]:
            regions[n] = (e.index[n], e.secs[e.index[n]], e.data(e.index[n]))
    for n in ('ER_IRAM', 'ER_SRAM'):
        if n in regions:
            sys.exit('%s has %d bytes: mutable statics; a block keeps its state in state / buffer (sdk/wvfx.h)'
                     % (n, regions[n][1][5]))
    kept = {e.index[n] for n in REGIONS if n in e.index}
    for i, (n, s) in enumerate(zip(e.names, e.secs)):
        if s[1] in (1, 8) and s[2] & 2 and s[5] and i not in kept:
            sys.exit('allocated section %s outside the regions of src/sdk/wvfx.ld' % n)
    if 'ER_IROM' not in regions:
        sys.exit('no code')
    hdr = regions['ER_IROM'][2]
    if struct.unpack_from('<I', hdr, 4)[0] != WVFX_MAGIC or struct.unpack_from('<I', hdr, 0x24)[0] != MODULE_MAGIC:
        sys.exit('the header (sdk/wvfx_module.c) is not first in ER_IROM')

    def region_of(addr):
        n = REGIONS[addr >> 28] if addr >> 28 < 4 else None
        if n not in regions:
            return None
        s = regions[n][1]
        return n if s[3] <= addr <= s[3] + s[5] else None

    rels = []
    for n, s in zip(e.names, e.secs):
        if s[1] == 4:
            sys.exit('RELA relocations (%s) are not taken' % n)
        if s[1] != 9 or e.names[s[7]] not in regions:
            continue
        _, tsec, tdata = regions[e.names[s[7]]]
        for j in range(s[5] // 8):
            P, info = struct.unpack_from('<II', e.b, s[4] + 8 * j)
            t, sym = info & 0xff, e.syms[info >> 8]
            if t in (0, 40):
                continue
            if t in ABS32:
                o = P - tsec[3]
                T = struct.unpack_from('<I', tdata, o)[0]
                r = region_of(T & ~1)
                if r is None:
                    if sym[2] == 0xfff1:
                        continue
                    sys.exit('an address at %#x points outside the module (%#x, %s)' % (P, T, sym[0]))
                struct.pack_into('<I', tdata, o, T - (regions[r][1][3] & 0xf0000000))
                rels.append((P, REGIONS.index(r)))
            elif t in PC_REL:
                src = region_of(P)
                dst = region_of(sym[1] & ~1) if sym[2] not in (0, 0xfff1) else src
                if dst != src:
                    sys.exit('a PC-relative reference from %s to %s (%s): the loader cannot place it' % (src, dst, sym[0]))
            else:
                sys.exit('relocation type %d at %#x (%s): build without movw / movt and PIC' % (t, P, sym[0]))

    order = [n for n in REGIONS if n in regions]
    shstr = bytearray(b'\0')

    def sname(t):
        o = len(shstr)
        shstr.extend(t.encode() + b'\0')
        return o
    ehsize, phsize = 52, 32
    start = ehsize + phsize * len(order)
    body, sh, ph, index = bytearray(), [[0] * 10], [], {}
    for n in order:
        _, s, d = regions[n]
        while (start + len(body)) % 8:
            body.append(0)
        off = start + len(body)
        if d is not None:
            body += d
        index[n] = len(sh)
        sh.append([sname(n), 1 if d is not None else 8, FLAGS[n], s[3], off, s[5], 0, 0, 8, 0])
        ph.append((1, off, s[3], s[3], s[5] if d is not None else 0, s[5], {6: 5, 2: 4, 3: 6}[FLAGS[n]], 8))
    symtab, region_sym = bytearray(16), {}
    for n in order:
        region_sym[REGIONS.index(n)] = len(symtab) // 16
        symtab += struct.pack('<IIIBBH', 0, regions[n][1][3], 0, 3, 0, index[n])
    rel = b''.join(struct.pack('<II', P, region_sym[r] << 8 | 2) for P, r in sorted(rels))

    def add(name, typ, data, link=0, info=0, align=4, entsize=0):
        while (start + len(body)) % 4:
            body.append(0)
        off = start + len(body)
        body.extend(data)
        sh.append([sname(name), typ, 0, 0, off, len(data), link, info, align, entsize])
        return len(sh) - 1
    symi = add('.symtab', 2, symtab, 0, len(symtab) // 16, 4, 16)
    sh[symi][6] = add('.strtab', 3, b'\0', align=1)
    add('.rel.dyn', 9, rel, symi, 0, 4, 8)
    name_at = sname('.shstrtab')
    shi = add('.shstrtab', 3, bytes(shstr), align=1)
    sh[shi][0], sh[shi][5] = name_at, len(shstr)
    body[sh[shi][4] - start:] = bytes(shstr)
    while (start + len(body)) % 4:
        body.append(0)
    shoff = start + len(body)
    out = bytearray(b'\x7fELF\x01\x01\x01' + b'\0' * 9 + struct.pack(
        '<HHIIIIIHHHHHH', 2, 40, 1, 0, ehsize, shoff, 0x05000400, ehsize, phsize, len(ph), 40, len(sh), shi))
    for p in ph:
        out += struct.pack('<8I', *p)
    out += body
    for s in sh:
        out += struct.pack('<10I', *s)
    return bytes(out), {n: regions[n][1][5] for n in order}, len(rels)


def build(a):
    stem = os.path.splitext(os.path.basename(a.source))[0]
    out = a.o or os.path.join(ROOT, 'out', 'fx', stem + '.elf')
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), 'build', stem)
    os.makedirs(tmp, exist_ok=True)
    objs = []
    for src, extra in ((a.source, ['-D' + d for d in a.define or []]), (os.path.join(SDK, 'wvfx_module.c'), ['-DWVFX_DESC=' + (a.desc or stem + '_block')]),
                       (os.path.join(SDK, 'wvfx_rt.c'), ['-fno-builtin'])):
        o = os.path.join(tmp, os.path.splitext(os.path.basename(src))[0] + '.o')
        run([wvbuild.atfe('clang'), '-c', src, '-o', o] + CFLAGS + extra + os.environ.get('WVFX_CFLAGS', '').split())
        objs.append(o)
    linked = os.path.join(tmp, 'linked.elf')
    run([wvbuild.atfe('ld.lld'), '-T', os.path.join(SDK, 'wvfx.ld'), '--emit-relocs', '--gc-sections', '-static',
         '-o', linked] + objs)
    data, sizes, nrel = convert(open(linked, 'rb').read())
    open(out, 'wb').write(data)
    d = describe(data)
    print('%s: %d bytes, %s, %d relocations; %s (%#010x, menu %d, %d parameters, state %d, buffer %d)' % (
        out, len(data), ', '.join('%s %d' % (k, v) for k, v in sizes.items()), nrel, d['name'], d['id'], d['menu'],
        len(d['params']), d['state'], d['buffer']))


# ---------------------------------------------------------------- a new block's source

TEMPLATE = r'''/*
 * {name} ({tag} menu): a block of waveod's SDK (src/sdk/wvfx.h, docs/blocks.md), started by
 * `python tools/wvfx.py new`: a level and a one-pole tone, both moving over one block when a knob changes so
 * nothing clicks; bypassed, the signal passes as it is.
 *
 * Measure it on the pedal while you work (cycles per 64-frame block, the output's CRC; nothing is written to the
 * flash, docs/bench.md):
 *     waveod Studio, Desempenho > Arquivo .elf / .c > {file} > Vigiar     (measured again at every save)
 *     python tools/bench.py {file} --watch
 * Build it as a module, then install it from the Studio's Efeitos tab:
 *     python tools/wvfx.py build {file}
 *
 * Rules: everything an instance keeps lives in its state (or its buffer); no mutable statics (the same code runs in
 * two slots, and in a tail while the next preset loads). Constant tables are fine. The id must be unique.
 */
#include "sdk/wvfx.h"

enum {{ K_LEVEL, K_TONE }};

static const struct wvfx_param params[] = {{
    /* name, min, max, step, default, knob word, format, sync, pad, options, unit */
    {{ "Level", -24.0f, 12.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" }},
    {{ "Tone", 0.0f, 100.0f, 1.0f, 50.0f, K_TONE, 0, 0, 0, 0, "%.0f%%" }},
}};

struct state {{
    float rate;
    float gain, coef;               /* where the last block ended */
    float lp[2];                    /* the tone filter, per channel */
}};

/* tone 0..100 % -> the low pass's one-pole coefficient, 500 Hz .. 20 kHz */
static float tone_coef(float rate, float tone)
{{
    float hz = 500.0f * wvfx_exp2(wvfx_clamp(tone, 0.0f, 100.0f) * 0.0532f);
    return 1.0f - wvfx_exp2(-9.0647202f * hz / rate);     /* 1 - e^(-2 pi f / rate) */
}}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{{
    (void)buffer;
    struct state *s = state;
    s->rate = env->rate;
    s->gain = wvfx_db(knobs[K_LEVEL]);
    s->coef = tone_coef(s->rate, knobs[K_TONE]);
    s->lp[0] = s->lp[1] = 0.0f;
    return 0;
}}

static void process(void *state, const struct wvfx_io *io)
{{
    struct state *s = state;
    float g = io->enabled ? wvfx_db(io->knobs[K_LEVEL]) : 1.0f;
    float c = io->enabled ? tone_coef(s->rate, io->knobs[K_TONE]) : 1.0f;    /* 1: the filter passes all */
    float dg = wvfx_ramp_step(s->gain, g, io->frames), dc = wvfx_ramp_step(s->coef, c, io->frames);
    float gain = s->gain, coef = s->coef, l = s->lp[0], r = s->lp[1];
    float *x = io->io;
    for (int f = 0; f < io->frames; f++, x += 2) {{
        gain += dg;
        coef += dc;
        l += coef * (x[0] - l);
        r += coef * (x[1] - r);
        x[0] = gain * l;
        x[1] = gain * r;
    }}
    s->gain = g;
    s->coef = c;
    s->lp[0] = l;
    s->lp[1] = r;
}}

const struct wvfx_desc {stem}_block = {{              /* tools/wvfx.py build finds it by the file's name */
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_{tag}, 0x{n:04X}),
    .menu = WVFX_{tag},
    .nparams = sizeof params / sizeof params[0],
    .flags = 0,
    .name = "{name}",
    .category = "{category}",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
}};
'''
MENU_ENUM = ['DYN', 'FX', 'WAH', 'DRV', 'AMP', 'CAB', 'IR', 'EQ', 'MOD', 'DLY', 'RVB', 'CLONE', 'LOOP', 'SND', 'RTN',
             'VOL']                                  # sdk/wvfx.h enum wvfx_menu


def new(a):
    import re
    import zlib
    name = a.name.strip()
    if not name or len(name) > 23 or '"' in name or '\\' in name:
        sys.exit('o nome vai de 1 a 23 caracteres, sem aspas nem barra invertida')
    tag = a.menu.upper()
    tag = 'CLONE' if tag == 'CLN' else tag
    if tag not in MENU_ENUM:
        sys.exit('menu desconhecido: %s (%s)' % (a.menu, ' '.join(MENU_ENUM)))
    out = a.o or re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_') + '.c'
    stem = os.path.splitext(os.path.basename(out))[0]
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', stem):
        sys.exit('o nome do arquivo (%s) vira o do descritor (%s_block): use letras, números e _' % (out, stem))
    if os.path.exists(out) and not a.force:
        sys.exit('%s já existe (--force para substituir)' % out)
    n = a.id if a.id is not None else 0x8000 | (zlib.crc32(name.encode()) & 0x7FFF)   # 0x8000..: users' own
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(TEMPLATE.format(name=name, tag=tag, file=os.path.basename(out), stem=stem, n=n,
                                category=a.category or 'User'))
    print('%s: "%s", %s, id %#010x (%s_block)' % (out, name, tag, 0x57000000 | MENU_ENUM.index(tag) << 16 | n, stem))


# ---------------------------------------------------------------- the library (studio/waveod_studio/wvlib.py)

def describe(elf):
    m = wvlib.describe(elf)
    return dict(id=m.id, menu=m.menu, name=m.name, params=m.params, state=m.state, buffer=m.buffer)


def lib(a):
    renamed = {}
    for spec in a.as_ or []:
        path, rest = spec.split('=', 1)
        id_, name = rest.split(':', 1)
        renamed[os.path.abspath(path)] = (int(id_, 0), name)
    library = wvlib.Library()
    for path in a.modules:
        id_, name = renamed.get(os.path.abspath(path), (None, None))
        before = len(library)
        try:
            m = library.add(open(path, 'rb').read(), id_, name)
        except wvlib.LibraryError as e:
            sys.exit('%s: %s' % (path, e))
        if len(library) == before:
            sys.exit('%s: id %#x twice' % (path, m.id))
    img = library.pack()
    open(a.out, 'wb').write(img)
    print('%s: %d bytes, %d modules' % (a.out, len(img), len(library)))


def show(a):
    try:
        library = wvlib.Library.parse(open(a.lib, 'rb').read())
    except wvlib.LibraryError as e:
        sys.exit('%s: %s' % (a.lib, e))
    print('%s: %d modules' % (a.lib, len(library)))
    for m in library:
        own = wvlib.describe(m.elf)
        note = '' if (own.id, own.name) == (m.id, m.name) else '  (the module: %#010x %s)' % (own.id, own.name)
        print('  %-4s %#010x  %-20s %d parameters, module %d bytes%s' % (wvlib.MENU_TAGS[m.menu], m.id, m.name,
                                                                      len(m.params), len(m.elf), note))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('source')
    b.add_argument('-o')
    b.add_argument('--desc')
    b.add_argument('-D', dest='define', action='append', help='a define for the block''s source, NAME=VALUE')
    lb = sub.add_parser('lib')
    lb.add_argument('out')
    lb.add_argument('modules', nargs='+')
    lb.add_argument('--as', dest='as_', action='append')
    s = sub.add_parser('show')
    s.add_argument('lib')
    nw = sub.add_parser('new', help='a new block\'s source to start from')
    nw.add_argument('name', help='the block\'s name on the pedal (up to 23 characters)')
    nw.add_argument('--menu', default='FX', help='DYN FX WAH DRV AMP CAB IR EQ MOD DLY RVB CLONE LOOP SND RTN VOL')
    nw.add_argument('--category', help='the models screen\'s tab (default User)')
    nw.add_argument('--id', type=lambda v: int(v, 0), help='the id\'s low 16 bits (default from the name, 0x8000..)')
    nw.add_argument('-o', help='the source file (default: the name as a C identifier, .c)')
    nw.add_argument('--force', action='store_true')
    a = ap.parse_args()
    {'build': build, 'lib': lib, 'show': show, 'new': new}[a.cmd](a)


if __name__ == '__main__':
    main()
