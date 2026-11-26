"""The drum machine's sounds and patterns, taken from the base firmware for tools/mkfw.py.

They are the original application's (V1.2.2), in its SDRAM image:
  0x8001AF78  sample bank: u32 count (13), u32 offset[count], u32 length_ms[count], then 16-bit mono PCM at
              44.1 kHz (offsets from the bank's start; FUN_00040CE0 reads it)
  0x800BC310  pattern bank: u32 count (100), u32 offset[count], then patterns of up to 200 u16 events
              (FUN_00040EA0 / FUN_00040D94), in the order of the genres
waveod's pack (src/engine/drums.h): "WDRM", u32 version 1, u32 sample bank offset, u32 size, u32 pattern bank
offset, u32 size, then both banks unchanged. A base that is itself a waveod build gives its pack back.
"""
import struct

SDRAM_BASE = 0x80000000
SAMPLES_VA, PATTERNS_VA = 0x8001AF78, 0x800BC310
SAMPLE_COUNT, PATTERN_COUNT, PATTERN_BYTES = 13, 100, 400
MAGIC, VERSION, HEADER = b'WDRM', 1, 24


def sdram_image(mcu):
    """the SDRAM image of an MCU section (the second load triple)"""
    fa, size, dst = struct.unpack_from('<3I', mcu, 0x1c)
    if dst != SDRAM_BASE:
        return None
    return mcu[fa - 0x20000:fa - 0x20000 + size]


def from_original(img):
    def at(va, n):
        o = va - SDRAM_BASE
        return img[o:o + n] if 0 <= o and o + n <= len(img) else None

    head = at(SAMPLES_VA, 4 + 8 * SAMPLE_COUNT)
    if not head or struct.unpack_from('<I', head)[0] != SAMPLE_COUNT:
        return None
    offs = struct.unpack_from('<%dI' % SAMPLE_COUNT, head, 4)
    lens = struct.unpack_from('<%dI' % SAMPLE_COUNT, head, 4 + 4 * SAMPLE_COUNT)
    if list(offs) != sorted(offs):
        return None
    samples_size = offs[-1] + (lens[-1] * 44 >> 6) * 128        # chunks of 64 samples, as FUN_00040CE0
    samples = at(SAMPLES_VA, samples_size)
    phead = at(PATTERNS_VA, 4 + 4 * PATTERN_COUNT)
    if not samples or not phead or struct.unpack_from('<I', phead)[0] != PATTERN_COUNT:
        return None
    poffs = struct.unpack_from('<%dI' % PATTERN_COUNT, phead, 4)
    patterns = at(PATTERNS_VA, poffs[-1] + PATTERN_BYTES)
    if not patterns:
        return None
    return pack(samples, patterns)


def pack(samples, patterns):
    pad = -len(samples) % 4
    out = MAGIC + struct.pack('<5I', VERSION, HEADER, len(samples), HEADER + len(samples) + pad, len(patterns))
    return out + samples + b'\0' * pad + patterns


def from_waveod(img):
    i = img.find(MAGIC + struct.pack('<2I', VERSION, HEADER))
    if i < 0:
        return None
    _, _, s_off, s_size, p_off, p_size = struct.unpack_from('<4s5I', img, i)
    return img[i:i + p_off + p_size]


def extract(mcu):
    """the pack for the base firmware's MCU section, or None"""
    img = sdram_image(mcu)
    if img is None:
        return None
    return from_original(img) or from_waveod(img)
