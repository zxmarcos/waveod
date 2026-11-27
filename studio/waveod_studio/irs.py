"""User IRs: the 20 slots the pedal's User IR blocks play (and waveod's own Cab / IR engine reads too).

A slot holds 1024 samples at 44.1 kHz (23.2 ms) as int32 with 24-bit values, 4096 bytes, written with the original
editor's message (0x14000001, src/editor/media_msgs.c) into the small file system at 0x753000 (src/fw/ir_fs.c).
The WAV conversion is Matribox Studio's (matribox/usb/irwav.py, HW-confirmed): any PCM / float WAV, the first
channel, resampled to 44.1 kHz with a windowed sinc, a short fade-out when the IR is longer than 1024 samples,
optional peak normalisation to the level of the factory cabs.

waveod reads the flash for the PC, so what a slot really holds comes back from the pedal (user_slots: the file
system parsed as src/fw/ir_store.c does).
"""
import math
import os
import struct

from waveod_studio.i18n import tr

RATE = 44100
SAMPLES = 1024                      # 16 partitions of 64, 23.2 ms
DATA_SIZE = SAMPLES * 4
SLOTS, NAME_MAX = 20, 16            # the pedal keeps 17 bytes with the terminator
FULL = 1 << 23
FACTORY_PEAK = 8287945              # peak of the factory cabs (-0.105 dBFS)
FADE = 64                           # samples of half-cosine fade when a longer IR is cut
FS_BASE, FS_SIZE = 0x753000, 0x2D000                # src/fw/flash_map.h FLASH_USER_IR_FS
FS_MAGIC = 0x5AFA
FORMATS = {1: 'PCM', 3: 'float'}


# ---------------------------------------------------------------------- WAV
def read_wav(path):
    """-> dict(rate, channels, bits, fmt, frames, samples = first channel as floats in -1..1)"""
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 12 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(tr('py.wav_not_riff'))
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from('<I', data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = body
        elif cid == b'data':
            raw = body
            if fmt is not None:
                break
        pos += 8 + size + (size & 1)
    if fmt is None or len(fmt) < 16:
        raise ValueError(tr('py.wav_no_fmt'))
    if raw is None:
        raise ValueError(tr('py.wav_no_data'))
    tag, ch, rate, _bps, align, bits = struct.unpack_from('<HHIIHH', fmt)
    if tag == 0xFFFE and len(fmt) >= 26:
        tag = struct.unpack_from('<H', fmt, 24)[0]          # WAVE_FORMAT_EXTENSIBLE: the sub-format GUID
    if tag not in FORMATS:
        raise ValueError(tr('py.wav_format', tag='%#x' % tag))
    if ch < 1 or rate < 8000:
        raise ValueError(tr('py.wav_header', channels=ch, rate=rate))
    width = bits // 8
    if tag == 1 and width not in (1, 2, 3, 4) or tag == 3 and width not in (4, 8):
        raise ValueError(tr('py.wav_bits', bits=bits, fmt=FORMATS[tag]))
    align = align or width * ch
    frames = len(raw) // align
    out = []
    for k in range(frames):
        o = k * align
        if tag == 3:
            v = struct.unpack_from('<f' if width == 4 else '<d', raw, o)[0]
        elif width == 1:
            v = (raw[o] - 128) / 128
        elif width == 2:
            v = struct.unpack_from('<h', raw, o)[0] / 32768
        elif width == 3:
            v = int.from_bytes(raw[o:o + 3], 'little', signed=True) / FULL
        else:
            v = struct.unpack_from('<i', raw, o)[0] / 2147483648
        out.append(v if math.isfinite(v) else 0.0)
    return dict(rate=rate, channels=ch, bits=bits, fmt=FORMATS[tag], frames=frames, samples=out)


def resample(x, src, dst=RATE, n_out=None):
    """windowed-sinc (Blackman, 32 zero crossings) resampling of x from src to dst Hz"""
    if src == dst:
        return list(x[:n_out] if n_out else x)
    ratio = src / dst
    fc = min(1.0, dst / src) * 0.97                         # cut a bit below the lower Nyquist
    half = 32 / fc
    total = int(len(x) / ratio)
    n_out = min(total, n_out) if n_out else total
    out = []
    for n in range(n_out):
        t = n * ratio
        lo, hi = max(0, int(math.ceil(t - half))), min(len(x) - 1, int(t + half))
        acc = 0.0
        for k in range(lo, hi + 1):
            d = k - t
            u = d / half
            if abs(u) >= 1:
                continue
            w = 0.42 + 0.5 * math.cos(math.pi * u) + 0.08 * math.cos(2 * math.pi * u)
            s = fc if d == 0 else math.sin(math.pi * fc * d) / (math.pi * d)
            acc += x[k] * s * w
        out.append(acc)
    return out


def convert(path, normalize=False):
    """a .wav as the slot will hold it: (1024 floats at 44.1 kHz, what was done: rate, channels, bits, fmt, ms,
    peak_db, kept (share of the energy in the 1024 samples), resampled, cut, normalized, gain_db, clipped)"""
    w = read_wav(path)
    src = w['samples']
    peak_in = max((abs(v) for v in src), default=0.0)
    if peak_in <= 0:
        raise ValueError(tr('py.wav_silent'))
    keep = int(round(SAMPLES * w['rate'] / RATE))
    total = sum(v * v for v in src)
    x = resample(src, w['rate'], RATE, SAMPLES + 1)
    cut = len(x) > SAMPLES
    x = x[:SAMPLES]
    if cut:
        for k in range(FADE):
            x[SAMPLES - FADE + k] *= 0.5 + 0.5 * math.cos(math.pi * (k + 1) / FADE)
    x += [0.0] * (SAMPLES - len(x))
    peak = max(abs(v) for v in x)
    if peak <= 0:
        raise ValueError(tr('py.ir_silent_start'))
    gain = FACTORY_PEAK / FULL / peak if normalize else 1.0
    x = [v * gain for v in x]
    return x, dict(rate=w['rate'], channels=w['channels'], bits=w['bits'], fmt=w['fmt'], frames=w['frames'],
                   ms=w['frames'] * 1000.0 / w['rate'], peak_db=20 * math.log10(peak_in),
                   kept=sum(v * v for v in src[:keep]) / total if total > 0 else 0.0,
                   resampled=w['rate'] != RATE, cut=cut, normalized=bool(normalize), gain_db=20 * math.log10(gain),
                   clipped=sum(1 for v in x if abs(v) >= 1.0))


def pack(samples):
    """1024 floats -> the slot's 4096 bytes (int32, 24-bit values)"""
    return struct.pack('<%di' % SAMPLES, *(max(-FULL, min(FULL - 1, int(round(v * FULL)))) for v in samples))


def unpack(data):
    return [v / FULL for v in struct.unpack('<%di' % SAMPLES, data[:DATA_SIZE])]


def default_name(path):
    return os.path.splitext(os.path.basename(path))[0].encode('latin-1', 'replace').decode('latin-1')[:NAME_MAX]


# ---------------------------------------------------------------------- the file system (src/fw/ir_store.c)
def user_slots(read):
    """the user IR file system -> [None or dict(name, data)] per slot, read the way the firmware reads it: three
    directory sectors {u16 0x5AFA, .., u16 sector size @4, .., u8 directory index @0xB, u8 entries @0xC, u8 entry
    size @0xD}, entries marked 'Z' with the data block at u16 @2 and the name at 8. read(offset, n) gives bytes of
    the area (offsets from FS_BASE): only the directory sectors and the slots' blocks are read."""
    out = [None] * SLOTS
    head = read(0, 0x10)
    if len(head) < 0x10 or struct.unpack_from('<H', head)[0] >> 7 != FS_MAGIC >> 7:
        return out
    sector = struct.unpack_from('<H', head, 4)[0]
    esize, cap = head[0x0d], head[0x0c]
    if not sector or sector * 3 > FS_SIZE or not esize or (sector - 0x10) // esize == 0:
        return out
    per = (sector - 0x10) // esize
    area = read(0, 3 * sector)
    dirs = {}
    for s in range(3):
        h = s * sector
        if struct.unpack_from('<H', area, h)[0] == FS_MAGIC:
            dirs[area[h + 0x0b]] = h                        # the last sector with that index wins, as in ir_store.c
    for n in range(min(cap, SLOTS)):
        h = dirs.get(n // per)
        if h is None:
            continue
        e = h + 0x10 + (n % per) * esize
        if e + esize > len(area) or area[e] != ord('Z'):
            continue
        block = struct.unpack_from('<H', area, e + 2)[0]
        off = sector * block
        if not block or off + DATA_SIZE > FS_SIZE:
            continue
        name = area[e + 8:e + 8 + 17].split(b'\0')[0].split(b'\xff')[0].decode('latin-1')
        out[n] = dict(name=name, data=bytes(read(off, DATA_SIZE)))
    return out
