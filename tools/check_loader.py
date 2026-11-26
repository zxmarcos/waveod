"""Cross-check the C module loader against an independent Python implementation.

    build/host/waveod-tests FIRMWARE.bin --dump out/modules.txt
    python tools/check_loader.py FIRMWARE.bin out/modules.txt

The Python side follows the research repository's effemu.py (itself a port of the original app's loader):
sections are appended to their pool in section-header order, then SHT_REL relocations are applied. The C
loader places sections at their ELF address offsets instead; both must give identical bytes.
"""
import struct
import sys

POOLS = {0: 0x00058000, 1: 0x81301000, 2: 0x20204000, 3: 0x81321000}


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def library_entries(fw):
    off, size = struct.unpack_from('<II', fw, 0x30 + 8)
    lib = fw[0x30 + off:0x30 + off + size]
    ncat, = struct.unpack_from('<I', lib, 0x14)
    for m in range(ncat):
        lo, = struct.unpack_from('<I', lib, 0x18 + 4 * m)
        n, = struct.unpack_from('<I', lib, lo)
        for i in range(n):
            eo, = struct.unpack_from('<I', lib, lo + 4 + 4 * i)
            total, hdr, eid = struct.unpack_from('<III', lib, eo)
            yield m, i, eid, lib[eo + hdr:eo + total]


def load(e, index):
    shoff, = struct.unpack_from('<I', e, 32)
    shnum, shstrndx = struct.unpack_from('<HH', e, 48)
    secs = [struct.unpack_from('<10I', e, shoff + 40 * i) for i in range(shnum)]
    stro = secs[shstrndx][4]
    names = [e[stro + s[0]:e.index(b'\0', stro + s[0])].decode() for s in secs]
    mem = {k: bytearray() for k in POOLS}
    for n, s in zip(names, secs):
        typ, flags, addr, off, size = s[1], s[2], s[3], s[4], s[5]
        if typ == 1 and ((flags & 6) == 6 or flags == 2) and addr in (0, 0x10000000):
            mem[addr >> 28] += e[off:off + size]
        elif typ in (1, 8) and (flags & 3) == 3 and n[:7] in ('ER_IRAM', 'ER_SRAM'):
            r = 2 if n.startswith('ER_IRAM') else 3
            mem[r] += e[off:off + size] if typ == 1 else bytes(size)
    for s in secs:
        if s[1] != 9:
            continue
        symoff = secs[s[6]][4]
        for j in range(s[5] // 8):
            r_off, r_info = struct.unpack_from('<II', e, s[4] + 8 * j)
            _, value, _, info, _, shndx = struct.unpack_from('<IIIBBH', e, symoff + 16 * (r_info >> 8))
            if shndx == 0 or (info & 0xf) not in (1, 2, 3):
                continue
            S = POOLS[value >> 28] + (value & 0x0fffffff)
            P = POOLS[r_off >> 28] + (r_off & 0x0fffffff)
            buf, at = mem[r_off >> 28], r_off & 0x0fffffff
            w, = struct.unpack_from('<I', buf, at)
            t = r_info & 0xff
            if t in (2, 23):
                w = (w + S) & 0xffffffff
            elif t == 3:
                w = (w + S - P) & 0xffffffff
            elif t in (21, 22):
                w = S
            elif t in (47, 48):
                v = (S & 0xffff) if t == 47 else (S >> 16)
                hw1, hw2 = w & 0xffff, w >> 16
                hw1 = (hw1 & 0xfbf0) | ((v >> 12) & 0xf) | (((v >> 11) & 1) << 10)
                hw2 = (hw2 & 0x8f00) | (((v >> 8) & 7) << 12) | (v & 0xff)
                w = hw1 | (hw2 << 16)
            elif t == 0:
                continue
            else:
                raise ValueError('relocation type %d' % t)
            struct.pack_into('<I', buf, at, w)
    struct.pack_into('<I', mem[0], 0, 0xFFAA0000 | index)
    return mem


def main():
    fw = open(sys.argv[1], 'rb').read()
    got = {}
    for line in open(sys.argv[2]):
        key, eid, *regions = line.split()
        got[key] = regions
    bad = n = 0
    for m, i, eid, elf in library_entries(fw):
        key = '%d.%d' % (m, i)
        mem = load(elf, i)
        want = ['%x:%08x' % (len(mem[r]), fnv1a(mem[r]) if mem[r] else 0) for r in range(4)]
        n += 1
        if got.get(key) != want:
            bad += 1
            print('%s %08x differs: C %s, Python %s' % (key, eid, got.get(key), want))
    print('%d modules compared, %d differ' % (n, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
