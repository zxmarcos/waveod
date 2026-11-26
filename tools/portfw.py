"""A firmware whose effect library runs waveod's ports instead of the original's modules.

    python tools/portfw.py BASE.bin OUT.bin [--elf build/fw/waveod.elf] [--keep NAME ...] [--jobs N]

Every entry of the original's effect library (section 1) that has a port in tools/check_stock.py (BLOCKS: the
original's id -> the block's source, checked bit for bit there) gets the port built as a module (tools/wvfx.py
build) in place of the original's ELF; the entry's header stays as it was (id, name, parameters with their
defaults, tab), so every preset plays on it with the same knobs. The firmware loads a block built as a module
from any library entry (engine/chain.c load_module), so the app needs no change. --keep NAME leaves a port's
entry on the original's module (to compare one block against its port on the pedal).

Entries without a port keep their module, and the 13 own entries of a research base (Split / Path B / Merge, the
Dark blocks) stay as they are. The original's code that is left never runs with waveod's defaults:
  - FX Loop, SND, RTN: waveod's own blocks run them (blocks/fx_loop.c, WVFX_REPLACE);
  - Cab (Supero 1x6) and User IR: waveod's Cab / IR engine (engine/cabir_block.c; on unless switched off).
The Clone entry gets its port (blocks/clone/clone.c) like the others; a NAM capture plays on the NAM engine.

Then the application goes in as tools/mkfw.py does (section 0 from --elf; sections 2-3 and the drum sounds from
BASE). The intermediate image (BASE with the new library) is OUT minus .bin plus _base.bin.
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import check_stock                                  # noqa: E402

MAX_ELF = 0x8000                                    # the app's staging buffer for a module (fw/library.h)
MAX_INDEX = 0x1000                                  # the index the app copies to RAM
MAX_LIBRARY = 0x700000 - 0x480000                   # section 1's flash partition


def sections(fw):
    if fw[:4] != b'FRMW' or struct.unpack_from('<I', fw, 4)[0] != len(fw) - 0x30:
        sys.exit('não é um firmware FRMW')
    out = []
    for i in range(4):
        off, size = struct.unpack_from('<II', fw, 0x30 + 8 * i)
        out.append(fw[0x30 + off:0x30 + off + size])
    return out


def frmw(header, secs):
    table, pos = bytearray(), 8 * len(secs)
    for s in secs:
        table += struct.pack('<II', pos, len(s))
        pos += len(s)
    body = bytes(table) + b''.join(secs)
    hdr = bytearray(header)
    struct.pack_into('<I', hdr, 4, len(body))
    struct.pack_into('<H', hdr, 0x0E, sum(body) & 0xFF)          # the FRMW checksum (only its low byte is checked)
    return bytes(hdr) + body


def parse_library(blob):
    """(menus: [[entry index]], entries: [bytes]) of section 1 (fw/library.h)"""
    cat_off, idx_size, total = struct.unpack_from('<3I', blob, 4)
    ncat, = struct.unpack_from('<I', blob, 0x14)
    if total != len(blob) or cat_off != 0x18 + 4 * ncat:
        sys.exit('biblioteca de efeitos inválida')
    lists = []
    for c in range(ncat):
        lo, = struct.unpack_from('<I', blob, 0x18 + 4 * c)
        n, = struct.unpack_from('<I', blob, lo)
        lists.append(list(struct.unpack_from('<%dI' % n, blob, lo + 4)))
    offs = sorted({o for lst in lists for o in lst})
    if offs[0] != idx_size:
        sys.exit('biblioteca de efeitos inválida (índice)')
    by_off, entries = {}, []
    for o in offs:
        size, = struct.unpack_from('<I', blob, o)
        by_off[o] = len(entries)
        entries.append(blob[o:o + size])
    return [[by_off[o] for o in lst] for lst in lists], entries


def pack_library(menus, entries):
    ncat = len(menus)
    idx = bytearray(struct.pack('<6I', 0, 0x18 + 4 * ncat, 0, 0, 0, ncat)) + bytes(4 * ncat)
    list_pos = []
    for lst in menus:
        list_pos.append(len(idx))
        idx += struct.pack('<I', len(lst)) + bytes(4 * len(lst))
    if len(idx) > MAX_INDEX:
        sys.exit('índice da biblioteca grande demais')
    offs, pos = [], len(idx)
    for e in entries:
        offs.append(pos)
        pos += len(e)
    for c, lst in enumerate(menus):
        struct.pack_into('<I', idx, 0x18 + 4 * c, list_pos[c])
        for k, ei in enumerate(lst):
            struct.pack_into('<I', idx, list_pos[c] + 4 + 4 * k, offs[ei])
    struct.pack_into('<II', idx, 8, len(idx), pos)
    out = bytes(idx) + b''.join(entries)
    if len(out) > MAX_LIBRARY:
        sys.exit('biblioteca maior que a partição (%d bytes)' % len(out))
    return out


def entry_id(e):
    return struct.unpack_from('<I', e, 8)[0]


def entry_name(e):
    nparams, (name_o,) = e[16], struct.unpack_from('<H', e, 20)
    strtab = 24 + 24 * nparams
    return e[strtab + name_o:e.index(b'\0', strtab + name_o)].decode('latin-1')


def with_elf(e, elf):
    hdr_size, = struct.unpack_from('<I', e, 4)
    hdr = bytearray(e[:hdr_size])
    struct.pack_into('<I', hdr, 0, hdr_size + len(elf))
    return bytes(hdr) + elf


def build(name, src, outdir):
    elf = os.path.join(outdir, name + '.elf')
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'wvfx.py'), 'build',
                        os.path.join(ROOT, 'src', 'blocks', src), '-o', elf], capture_output=True, text=True)
    if r.returncode:
        return name, None, r.stdout + r.stderr
    return name, open(elf, 'rb').read(), ''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base')
    ap.add_argument('out')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--keep', action='append', default=[], help='a port (check_stock name) left on the original')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()

    fw = open(a.base, 'rb').read()
    secs = sections(fw)
    menus, entries = parse_library(secs[1])
    ports = {eid: (name, src) for name, (src, _desc, eid, _scen) in check_stock.BLOCKS.items()
             if name not in a.keep}
    outdir = os.path.join(ROOT, 'out', 'portfw')
    os.makedirs(outdir, exist_ok=True)

    want = [(ports[entry_id(e)][0], ports[entry_id(e)][1]) for e in entries if entry_id(e) in ports]
    elfs = {}
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for name, elf, err in pool.map(lambda t: build(t[0], t[1], outdir), want):
            if elf is None:
                sys.exit('wvfx.py build %s falhou:\n%s' % (name, err))
            if len(elf) > MAX_ELF:
                sys.exit('%s: o módulo tem %d bytes, o pedal aceita %d' % (name, len(elf), MAX_ELF))
            elfs[name] = elf

    replaced, kept, old_bytes, new_bytes = 0, [], 0, 0
    for i, e in enumerate(entries):
        eid = entry_id(e)
        if eid in ports:
            elf = elfs[ports[eid][0]]
            old_bytes += len(e) - struct.unpack_from('<I', e, 4)[0]
            new_bytes += len(elf)
            entries[i] = with_elf(e, elf)
            replaced += 1
        else:
            kept.append('%#010x %s' % (eid, entry_name(e)))
    missing = sorted(set(ports) - {entry_id(e) for e in entries})
    if missing:
        sys.exit('ports sem entrada na biblioteca da base: %s' % ', '.join('%#010x' % m for m in missing))
    secs[1] = pack_library(menus, entries)

    base_out = os.path.splitext(a.out)[0] + '_base.bin'
    open(base_out, 'wb').write(frmw(fw[:0x30], secs))
    print('%d entradas com o port (módulos %d -> %d bytes), %d como estavam:' % (replaced, old_bytes, new_bytes,
                                                                                 len(kept)))
    for k in kept:
        print('  ' + k)
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'mkfw.py'), base_out, a.out, '--elf', a.elf])
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
