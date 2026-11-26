"""Pack build/fw/waveod.elf into a QME-200 update file.

    python tools/mkfw.py BASE.bin [OUT.bin] [--elf build/fw/waveod.elf]

The application replaces the MCU section (0) of BASE; the effect library, IRs and factory presets (sections
1-3) come from BASE unchanged, so any firmware made by the research tools (Matribox Studio, NAM Clone,
parallel routing...) can be the base.

MCU section format (read by the bootloader, which stores the section at flash 0x20000):
    ff x 8, "V122", 0 x 4, then (flash address, size, load address) for the ITCM image and the SDRAM image,
    then both images; flash address = 0x20000 + offset inside the section. Padded to 0x80.
FRMW checksum: u16 @0x0E = 8-bit sum of every byte from 0x30 (only the low byte is checked).
The drum machine's sounds and patterns come from BASE too (tools/drumpack.py) and are written into the reserved
region __drum_pack_start..__drum_pack_end of the SDRAM image.

waveod only: the bootloader erases the 4 KB sectors a section's data reaches (research/NOTES.md, "Dump da flash"),
so the MCU section must end below waveod's effect library at 0x2A0000 (and the preset store at 0x3A3000 after it)
or an update would erase them. The firmware is refused when it would. (Matribox Studio's own packer, for the original app, is not this one.)
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
FLASH_BASE = 0x20000
WV_STORE = 0x3A3000             # waveod's preset store (src/fw/flash_map.h FLASH_WV_STORE)
WV_LIBRARY = 0x2A0000           # waveod's effect library before it (FLASH_WV_LIBRARY): the image stays below
MAX_PACKET = 0x100              # the bootloader writes whole packets (180 bytes on V1.0.0) and erases whole 4 KB sectors
ITCM_LIMIT = 0x58000            # effect module code pool starts here
SDRAM_BASE, SDRAM_LIMIT = 0x80000000, 0x81300000
SECTIONS = 4


def read_frmw(data):
    if data[:4] != b'FRMW' or struct.unpack_from('<I', data, 4)[0] != len(data) - 0x30:
        sys.exit('not a FRMW update file')
    secs = []
    for i in range(SECTIONS):
        off, size = struct.unpack_from('<II', data, 0x30 + 8 * i)
        secs.append(data[0x30 + off:0x30 + off + size])
    return bytearray(data[:0x30]), secs


def pack_frmw(header, secs):
    table, pos = bytearray(), 8 * SECTIONS
    for s in secs:
        table += struct.pack('<II', pos, len(s))
        pos += len(s)
    body = bytes(table) + b''.join(secs)
    struct.pack_into('<I', header, 4, len(body))
    struct.pack_into('<H', header, 0x0e, sum(body) & 0xff)
    return bytes(header) + body


def elf_images(path):
    e = open(path, 'rb').read()
    if e[:4] != b'\x7fELF' or e[4] != 1:
        sys.exit('%s: not an ELF32 file' % path)
    phoff, = struct.unpack_from('<I', e, 28)
    phnum, = struct.unpack_from('<H', e, 44)
    itcm, sdram = bytearray(), bytearray()
    for i in range(phnum):
        typ, off, va, pa, fs, ms, fl, al = struct.unpack_from('<8I', e, phoff + 32 * i)
        if typ != 1 or not fs:
            continue
        if pa + fs <= ITCM_LIMIT:
            img, base = itcm, 0
        elif SDRAM_BASE <= pa and pa + fs <= SDRAM_LIMIT:
            img, base = sdram, SDRAM_BASE
        else:
            sys.exit('load segment %#x..%#x is outside the ITCM and SDRAM images' % (pa, pa + fs))
        end = pa - base + fs
        if len(img) < end:
            img.extend(b'\0' * (end - len(img)))
        img[pa - base:end] = e[off:off + fs]
    sp, reset = struct.unpack_from('<II', itcm, 0)
    if sp != 0x20008000 or not (reset & 1) or reset >= ITCM_LIMIT:
        sys.exit('vector table must start the ITCM image (sp %#x, reset %#x)' % (sp, reset))
    return bytes(itcm), bytes(sdram), e


def symbols(e):
    """name -> (value, size) of the ELF symbol table"""
    shoff, = struct.unpack_from('<I', e, 32)
    shnum, shstrndx = struct.unpack_from('<HH', e, 48)
    secs = [struct.unpack_from('<10I', e, shoff + 40 * i) for i in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:
            continue
        strtab = secs[s[6]]
        for j in range(s[5] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', e, s[4] + 16 * j)
            o = strtab[4] + nm
            name = e[o:e.index(b'\0', o)].decode()
            if name:
                out[name] = (val, size)
    return out


def add_drum_pack(sdram, syms, base_mcu):
    """the SDRAM image grown over the reserved drum pack region, filled from the base firmware"""
    import drumpack
    start, end = syms['__drum_pack_start'][0] - SDRAM_BASE, syms['__drum_pack_end'][0] - SDRAM_BASE
    if len(sdram) > start:
        sys.exit('the SDRAM image runs into the drum pack region')
    pack = drumpack.extract(base_mcu) or b''
    if len(pack) > end - start:
        sys.exit('drum pack of %d bytes does not fit the reserved %d' % (len(pack), end - start))
    img = bytearray(sdram) + b'\0' * (start - len(sdram))
    img += pack if pack else b'\0' * 32         # an empty header: no drum machine
    return bytes(img), len(pack)


def mcu_section(itcm, sdram):
    hdr = b'\xff' * 8 + b'V122' + b'\0' * 4
    hdr += struct.pack('<6I', FLASH_BASE + 0x28, len(itcm), 0, FLASH_BASE + 0x28 + len(itcm), len(sdram), SDRAM_BASE)
    sec = hdr + itcm + sdram
    return sec + b'\0' * (-len(sec) % 0x80)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base')
    ap.add_argument('out', nargs='?', default=os.path.join(ROOT, 'out', 'QME-200_waveod.bin'))
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()

    itcm, sdram, e = elf_images(a.elf)
    syms = symbols(e)
    vec = struct.unpack_from('<176I', itcm, 0)
    for name, idx in (('reset_handler', 1), ('systick_handler', 15), ('dma0_dma16_irq', 16)):
        want = syms.get(name, (None,))[0]
        if want is None or (vec[idx] & ~1) != (want & ~1):
            sys.exit('vector %d is %#x, expected %s at %s' % (idx, vec[idx], name, want))

    data = open(a.base, 'rb').read()
    header, secs = read_frmw(data)
    if header[16:23] != b'QME-200':
        sys.exit('base firmware is not for the QME-200')
    sdram, drums = add_drum_pack(sdram, syms, secs[0])
    secs[0] = mcu_section(itcm, sdram)
    reach = FLASH_BASE + len(secs[0]) + MAX_PACKET
    reach = (reach + 0xfff) & ~0xfff
    if reach > WV_LIBRARY:
        sys.exit('the application section (%d bytes) would reach %#x: the bootloader would erase sectors of the '
                 "effect library at %#x" % (len(secs[0]), reach, WV_LIBRARY))
    out = pack_frmw(header, secs)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, 'wb').write(out)

    ok = out[:4] == b'FRMW' and out[16:23] == b'QME-200' and sum(out[0x30:]) & 0xff == out[0x0e]
    print('ITCM image  %7d bytes (limit %d, %d free before the module pool)' % (len(itcm), ITCM_LIMIT,
                                                                             ITCM_LIMIT - len(itcm)))
    print('SDRAM image %7d bytes' % len(sdram))
    print('app section %7d bytes, erases up to %#x (%d KB before the effect library at %#x)' % (
        len(secs[0]), reach, (WV_LIBRARY - reach) // 1024, WV_LIBRARY))
    print('drum machine: %s' % ('%d bytes from the base firmware' % drums if drums else
                                'NOT FOUND in the base firmware (the drum machine will say so)'))
    for name in ('__bss_start', '__bss_end', '__sdram_bss_start', '__sdram_bss_end'):
        if name in syms:
            print('  %-18s %#010x' % (name, syms[name][0]))
    print('%s: %d bytes, sections 1-3 from %s, editor checks %s' % (a.out, len(out), os.path.basename(a.base),
                                                                    'OK' if ok else 'FAILED'))


if __name__ == '__main__':
    main()
