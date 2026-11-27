"""waveod's effect library (waveod/src/fw/wv_library.h, docs/blocks.md): the blocks built as modules
(waveod/tools/wvfx.py build) that the pedal keeps in its flash.

    lib = Library.parse(image)          # what the pedal has (Pedal.library())
    lib.add(open('echo.elf', 'rb').read())
    lib.remove(0x57090007)
    image = lib.pack()                  # for Pedal.write_library()

The image is the container of the original's effect library with "WVLB" and a CRC-32 of everything after the
header; each entry has the block's name, menu and parameters (read from the module's descriptor when it is
added) and the module ELF.
"""
import struct
import zlib

from waveod_studio.i18n import tr

MAGIC = b'WVLB'
WVFX_MAGIC, WVFX_ABI = 0x58465657, 1
MENUS = 16
NO_STRING = 0xFFFF
P_OPTIONS, P_UNIT = 0x40, 0x80                  # parameter record f1: its first field names the options / unit
REGIONS = 4
MENU_TAGS = ('DYN', 'FX', 'WAH', 'DRV', 'AMP', 'CAB', 'IR', 'EQ', 'MOD', 'DLY', 'RVB', 'CLN', 'LOOP', 'SND', 'RTN',
             'VOL')


class LibraryError(ValueError):
    pass


class Module:
    """one block: id, menu, name, category, flags, params [dict], state / buffer (bytes per instance), elf"""

    def __init__(self, **kw):
        self.__dict__.update(kw)

    def __repr__(self):
        return 'Module(%#010x %s %s, %d parameters)' % (self.id, MENU_TAGS[self.menu], self.name, len(self.params))


# ---------------------------------------------------------------- a module's descriptor

def _elf_sections(elf):
    shoff, = struct.unpack_from('<I', elf, 32)
    shnum, shstrndx = struct.unpack_from('<HH', elf, 48)
    secs = [struct.unpack_from('<10I', elf, shoff + 40 * i) for i in range(shnum)]
    so = secs[shstrndx][4]
    names = [elf[so + s[0]:elf.index(b'\0', so + s[0])].decode() for s in secs]
    return names, secs


def describe(elf):
    """the block's descriptor read from a module ELF (tools/wvfx.py build): a Module. Its pointers are offsets in
    their region, each with a relocation naming the region."""
    elf = bytes(elf)
    if elf[:4] != b'\x7fELF':
        raise LibraryError(tr('py.lib_not_elf'))
    names, secs = _elf_sections(elf)
    regions = {}
    for r, n in enumerate(('ER_IROM', 'ER_SROM', 'ER_IRAM', 'ER_SRAM')):
        if n in names:
            s = secs[names.index(n)]
            regions[r] = elf[s[4]:s[4] + s[5]] if s[1] != 8 else bytes(s[5])
    relocated = {}
    if '.rel.dyn' in names and '.symtab' in names:
        s, st = secs[names.index('.rel.dyn')], secs[names.index('.symtab')]
        for j in range(s[5] // 8):
            P, info = struct.unpack_from('<II', elf, s[4] + 8 * j)
            value, = struct.unpack_from('<I', elf, st[4] + 16 * (info >> 8) + 4)
            relocated[P] = value >> 28

    def word(addr):
        d = regions.get(addr >> 28)
        if d is None or (addr & 0x0fffffff) + 4 > len(d):
            raise LibraryError(tr('py.lib_addr', addr='%#x' % addr))
        return struct.unpack_from('<I', d, addr & 0x0fffffff)[0]

    def pointer(addr):
        w = word(addr)
        if addr not in relocated:
            if w:
                raise LibraryError(tr('py.lib_reloc', addr='%#x' % addr))
            return 0
        return relocated[addr] << 28 | w

    def string(addr):
        if not addr:
            return None
        d, o = regions[addr >> 28], addr & 0x0fffffff
        return d[o:d.index(b'\0', o)].decode('latin-1')

    def packed(addr):
        if not addr:
            return None
        d, o = regions[addr >> 28], addr & 0x0fffffff
        return d[o:d.index(b'\0\0', o) + 1].decode('latin-1').split('\0')[:-1]

    if 0 not in regions or word(4) != WVFX_MAGIC:
        raise LibraryError(tr('py.lib_not_block'))
    da = pointer(12)
    if word(da) != WVFX_MAGIC or word(da + 4) != WVFX_ABI:
        raise LibraryError(tr('py.lib_abi', abi=word(da + 4)))
    d = regions[da >> 28]
    o = (da & 0x0fffffff) + 12
    menu, nparams, flags = d[o], d[o + 1], d[o + 2]
    params = []
    pa = pointer(da + 24)
    for k in range(nparams):
        p = pa + 32 * k
        rd, ro = regions[p >> 28], p & 0x0fffffff
        mn, mx, step, df = struct.unpack_from('<4f', rd, ro + 4)
        params.append(dict(name=string(pointer(p)), min=mn, max=mx, step=step, default=df, knob=rd[ro + 20],
                           format=rd[ro + 21], sync=rd[ro + 22], options=packed(pointer(p + 24)),
                           unit=string(pointer(p + 28))))
    return Module(id=word(da + 8), menu=menu, flags=flags, name=string(pointer(da + 16)),
                  category=string(pointer(da + 20)) or '', params=params, state=word(da + 28),
                  buffer=word(da + 32), elf=elf)


# ---------------------------------------------------------------- the library

class Library:
    def __init__(self, modules=None):
        self.modules = list(modules or [])

    def __iter__(self):
        return iter(self.modules)

    def __len__(self):
        return len(self.modules)

    def find(self, id_):
        return next((m for m in self.modules if m.id == id_), None)

    def add(self, elf, id_=None, name=None):
        """a module ELF into the library (one with the same id is replaced); its Module"""
        m = describe(elf)
        if id_ is not None:
            m.id = id_
        if name is not None:
            m.name = name
        if m.id >> 24 != 0x57 or (m.id >> 16 & 0xff) != m.menu or m.menu >= MENUS:
            raise LibraryError(tr('py.lib_bad_id', id='%#x' % m.id, menu=m.menu))
        self.modules = [x for x in self.modules if x.id != m.id] + [m]
        return m

    def remove(self, id_):
        self.modules = [x for x in self.modules if x.id != id_]

    # ------------------------------------------------------------------ bytes
    def pack(self):
        blobs, by_menu = [], [[] for _ in range(MENUS)]
        for m in self.modules:
            strings = bytearray()

            def s(text):
                if text is None:
                    return NO_STRING
                o = len(strings)
                strings.extend(text.encode('latin-1') + b'\0')
                return o
            name, cat = s(m.name), s(m.category or '')
            recs = bytearray()
            for p in m.params:
                extra, f1 = NO_STRING, 0
                if p['options']:
                    extra, f1 = len(strings), P_OPTIONS
                    strings.extend(b''.join(x.encode('latin-1') + b'\0' for x in p['options']) + b'\0')
                elif p['unit']:
                    extra, f1 = s(p['unit']), P_UNIT
                recs += struct.pack('<HH4fBBBB', extra, s(p['name']), p['min'], p['max'], p['step'], p['default'],
                                    p['knob'], f1, p['sync'], p['format'] or 0xFF)
            hdr = bytearray(struct.pack('<IIIiBBBBHH', 0, 0, m.id, -1, len(m.params), 0, m.flags, 0, name, cat))
            hdr += recs + strings
            while len(hdr) % 8:
                hdr.append(0xFF)
            blob = hdr + m.elf
            while len(blob) % 8:
                blob.append(0xFF)
            struct.pack_into('<II', blob, 0, len(hdr) + len(m.elf), len(hdr))
            by_menu[m.menu].append(len(blobs))
            blobs.append(blob)
        head = 0x18 + 4 * MENUS
        lists, at = [], head
        for m in range(MENUS):
            lists.append(at)
            at += 4 + 4 * len(by_menu[m])
        index_size = (at + 7) & ~7
        offsets, at = [], index_size
        for b in blobs:
            offsets.append(at)
            at += len(b)
        img = bytearray(struct.pack('<4sIIIII', MAGIC, head, index_size, at, 0, MENUS))
        img += b''.join(struct.pack('<I', x) for x in lists)
        for m in range(MENUS):
            img += struct.pack('<I', len(by_menu[m])) + b''.join(struct.pack('<I', offsets[i]) for i in by_menu[m])
        img += b'\xff' * (index_size - len(img))
        for b in blobs:
            img += b
        struct.pack_into('<I', img, 0x10, zlib.crc32(bytes(img[0x18:])))
        return bytes(img)

    @classmethod
    def parse(cls, img):
        """the library in `img` (b'' or anything invalid: LibraryError, except b'' = empty)"""
        img = bytes(img)
        if not img:
            return cls()
        if len(img) < 0x18 or img[:4] != MAGIC:
            raise LibraryError(tr('py.lib_not_lib'))
        head, index_size, total, crc, menus = struct.unpack_from('<IIIII', img, 4)
        if total > len(img) or zlib.crc32(img[0x18:total]) != crc:
            raise LibraryError(tr('py.lib_crc'))
        lib = cls()
        for menu in range(menus):
            lst, = struct.unpack_from('<I', img, 0x18 + 4 * menu)
            n, = struct.unpack_from('<I', img, lst)
            for i in range(n):
                off, = struct.unpack_from('<I', img, lst + 4 + 4 * i)
                size, hdr, id_, _, nparams, _, flags, _, name_at, cat_at = struct.unpack_from('<IIIiBBBBHH', img, off)
                strings = off + 24 + 24 * nparams

                def s(at):
                    return None if at == NO_STRING else img[strings + at:img.index(b'\0', strings + at)].decode('latin-1')
                params = []
                for k in range(nparams):
                    extra, nm, mn, mx, step, df, knob, f1, sync, fmt = struct.unpack_from('<HH4fBBBB', img,
                                                                                          off + 24 + 24 * k)
                    options = unit = None
                    if extra != NO_STRING and f1 & P_OPTIONS:
                        o = strings + extra
                        options = img[o:img.index(b'\0\0', o) + 1].decode('latin-1').split('\0')[:-1]
                    elif extra != NO_STRING and f1 & P_UNIT:
                        unit = s(extra)
                    params.append(dict(name=s(nm), min=mn, max=mx, step=step, default=df, knob=knob,
                                       format=0 if fmt == 0xFF else fmt, sync=sync, options=options, unit=unit))
                elf = img[off + hdr:off + size]
                own = describe(elf)
                lib.modules.append(Module(id=id_, menu=menu, flags=flags, name=s(name_at), category=s(cat_at) or '',
                                          params=params, state=own.state, buffer=own.buffer, elf=elf))
        return lib
