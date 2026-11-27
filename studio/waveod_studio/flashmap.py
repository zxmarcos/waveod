"""What an 8 MB dump of the QME-200 flash holds where (src/fw/flash_map.h; waveod/tools/flashdump.py --summary has
the coarser map from before waveod's library and preset store)."""
import struct

from waveod_studio.i18n import tr

FLASH = 0x800000
BOOTLOADER = 0x20000
# (id, start, end); the names (py.area_<id>) and what each is for, in a line (py.area_<id>_note) are the catalog's
AREAS = (
    ('boot', 0x000000, 0x020000),
    ('app', 0x020000, 0x2A0000),
    ('wvlib', 0x2A0000, 0x3A3000),
    ('wvpresets', 0x3A3000, 0x40A000),
    ('clone', 0x40A000, 0x440000),
    ('factory', 0x440000, 0x480000),
    ('effects', 0x480000, 0x700000),
    ('factory_irs', 0x700000, 0x753000),
    ('user_irs', 0x753000, 0x780000),
    ('records', 0x780000, 0x800000),
)


def name(area):
    """an area's name, as the summary shows it"""
    return tr('py.area_%s' % area)


def note(area):
    """what an area is for, in a line (the page's summary)"""
    return tr('py.area_%s_note' % area)


def used(data, lo, hi):
    seg = data[lo:hi]
    n = len(seg) - seg.count(0xFF)
    return n, (lo + len(seg.rstrip(b'\xff')) - 1 if n else None)


def areas(data):
    """[(id, name, start, end, bytes not 0xFF, last such byte or None)] for the areas of a whole-flash dump"""
    if len(data) != FLASH:
        raise ValueError(tr('py.area_need_full'))
    return [(area, name(area), lo, hi) + used(data, lo, hi) for area, lo, hi in AREAS]


def summary(data):
    """[(name, start, end, bytes not 0xFF, last such byte or None)] for the areas of a whole-flash dump"""
    return [x[1:] for x in areas(data)]


def app_end(data):
    """where the application image in the dump ends (the bootloader's pairs at 0x20010)"""
    end = 0x20000
    for k in range(2):
        fa, n = struct.unpack_from('<II', data, 0x20010 + 12 * k)
        if 0x20000 <= fa and fa + n < 0x40A000:
            end = max(end, fa + n)
    return end


def details(data):
    """a word on what some areas hold: {area id: text}"""
    out = {}
    end = app_end(data)
    if end > 0x20000:
        out['app'] = tr('py.area_app_image', end='%06X' % end, kb=(end - 0x20000 + 1023) // 1024)
        rest, last = used(data, end, 0x2A0000)
        if rest:
            out['app'] += tr('py.area_app_rest', kb=(rest + 1023) // 1024)
    lib = data[0x2A0000:0x3A3000]
    if lib[:4] == b'WVLB':
        try:
            from waveod_studio import wvlib
            n = len(wvlib.Library.parse(lib))
            out['wvlib'] = tr('py.area_modules_one' if n == 1 else 'py.area_modules_other', n=n)
        except Exception:                           # noqa: BLE001 - a broken library is still worth naming
            out['wvlib'] = tr('py.area_lib_broken')
    elif lib.count(0xFF) != len(lib):
        out['wvlib'] = tr('py.area_lib_leftovers')
    return out
