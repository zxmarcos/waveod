"""What an 8 MB dump of the QME-200 flash holds where (src/fw/flash_map.h; waveod/tools/flashdump.py --summary has
the coarser map from before waveod's library and preset store)."""
import struct

FLASH = 0x800000
BOOTLOADER = 0x20000
AREAS = (
    ('Bootloader', 0x000000, 0x020000),
    ('App (seção 0)', 0x020000, 0x2A0000),
    ('Biblioteca do waveod', 0x2A0000, 0x3A3000),
    ('Presets do waveod', 0x3A3000, 0x40A000),
    ('Capturas Clone', 0x40A000, 0x440000),
    ('Presets de fábrica (seção 3)', 0x440000, 0x480000),
    ('Biblioteca de efeitos (seção 1)', 0x480000, 0x700000),
    ('IRs de fábrica (seção 2)', 0x700000, 0x753000),
    ('IRs de usuário', 0x753000, 0x780000),
    ('Record store original', 0x780000, 0x800000),
)
# what each area is for, in a line (the page's summary)
NOTES = {
    'Bootloader': 'o modo de atualização; nenhuma atualização o regrava',
    'App (seção 0)': 'o firmware em si (waveod ou o original)',
    'Biblioteca do waveod': 'os módulos de efeito instalados pela aba Efeitos',
    'Presets do waveod': 'presets, ritmos e controles do waveod',
    'Capturas Clone': 'os 10 slots do Clone (capturas originais ou NAM)',
    'Presets de fábrica (seção 3)': 'os 120 presets do arquivo de firmware',
    'Biblioteca de efeitos (seção 1)': 'os efeitos do firmware original',
    'IRs de fábrica (seção 2)': 'os IRs de gabinete do firmware',
    'IRs de usuário': 'os 20 IRs gravados pelo usuário',
    'Record store original': 'ajustes globais, mapa de presets e presets do usuário do original',
}


def used(data, lo, hi):
    seg = data[lo:hi]
    n = len(seg) - seg.count(0xFF)
    return n, (lo + len(seg.rstrip(b'\xff')) - 1 if n else None)


def summary(data):
    """[(name, start, end, bytes not 0xFF, last such byte or None)] for the areas of a whole-flash dump"""
    if len(data) != FLASH:
        raise ValueError('o resumo precisa de um dump dos 8 MB')
    return [(name, lo, hi) + used(data, lo, hi) for name, lo, hi in AREAS]


def app_end(data):
    """where the application image in the dump ends (the bootloader's pairs at 0x20010)"""
    end = 0x20000
    for k in range(2):
        fa, n = struct.unpack_from('<II', data, 0x20010 + 12 * k)
        if 0x20000 <= fa and fa + n < 0x40A000:
            end = max(end, fa + n)
    return end


def details(data):
    """a word on what some areas hold: {area name: text}"""
    out = {}
    end = app_end(data)
    if end > 0x20000:
        out['App (seção 0)'] = 'imagem até %06X (%d KB)' % (end, (end - 0x20000 + 1023) // 1024)
        rest, last = used(data, end, 0x2A0000)
        if rest:
            out['App (seção 0)'] += '; depois dela, %d KB de outro firmware' % ((rest + 1023) // 1024)
    lib = data[0x2A0000:0x3A3000]
    if lib[:4] == b'WVLB':
        try:
            from waveod_studio import wvlib
            n = len(wvlib.Library.parse(lib))
            out['Biblioteca do waveod'] = '%d módulo%s' % (n, '' if n == 1 else 's')
        except Exception:                           # noqa: BLE001 - a broken library is still worth naming
            out['Biblioteca do waveod'] = 'biblioteca com erro'
    elif lib.count(0xFF) != len(lib):
        out['Biblioteca do waveod'] = 'sem biblioteca: restos de outro firmware'
    return out
