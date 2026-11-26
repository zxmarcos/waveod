"""User IRs without a pedal (waveod_studio/irs.py): WAV conversion, the slot's bytes, the user IR file system read
the way src/fw/ir_store.c reads it. python tests/test_irs.py"""
import math
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from waveod_studio import irs        # noqa: E402

failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


def wav(path, x, rate, bits=24, channels=1, fmt=1):
    """x: floats in -1..1 (the first channel; the others get half of it)"""
    frames = []
    for v in x:
        for c in range(channels):
            s = v if c == 0 else v / 2
            if fmt == 3:
                frames.append(struct.pack('<f', s))
            elif bits == 16:
                frames.append(struct.pack('<h', int(s * 32767)))
            else:
                frames.append(int(s * 8388607).to_bytes(3, 'little', signed=True))
    pcm = b''.join(frames)
    width = 4 if fmt == 3 else bits // 8
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, fmt, channels, rate, rate * width * channels, width * channels, width * 8) +
                b'data' + struct.pack('<I', len(pcm)) + pcm)


def fs_image(slots, sector=0x1000, esize=0x20):
    """a user IR file system: one directory sector, entries 'Z' {u16 block @2, name @8}, data blocks after it"""
    area = bytearray(b'\xff' * irs.FS_SIZE)
    struct.pack_into('<H', area, 0, irs.FS_MAGIC)
    struct.pack_into('<H', area, 4, sector)
    area[0x0b], area[0x0c], area[0x0d] = 0, irs.SLOTS, esize
    for n, (name, data) in slots.items():
        e = 0x10 + n * esize
        block = 3 + n
        area[e] = ord('Z')
        struct.pack_into('<H', area, e + 2, block)
        area[e + 8:e + 8 + 17] = name.encode('latin-1').ljust(17, b'\0')
        area[block * sector:block * sector + irs.DATA_SIZE] = data
    return bytes(area)


def main():
    tmp = tempfile.mkdtemp(prefix='wvirs_')
    imp = [math.exp(-k / 60.0) * math.cos(k * 0.3) * 0.5 for k in range(800)]
    p = os.path.join(tmp, 'a.wav')
    wav(p, imp, 44100)
    x, info = irs.convert(p)
    check(len(x) == irs.SAMPLES and not info['resampled'] and not info['cut'] and abs(x[10] - imp[10]) < 1e-6,
          '24-bit mono 44.1 kHz: as it is (%d samples, sample 10 %.6f)' % (len(x), x[10]))
    data = irs.pack(x)
    check(len(data) == irs.DATA_SIZE and max(abs(a - b) for a, b in zip(irs.unpack(data), x)) < 1.5 / irs.FULL,
          'packed into %d bytes, back within one step' % len(data))

    long = [math.exp(-k / 400.0) * math.sin(k * 0.21) for k in range(3000)]
    wav(p, long, 48000, bits=16, channels=2)
    x, info = irs.convert(p, normalize=True)
    peak = max(abs(v) for v in x)
    check(info['resampled'] and info['cut'] and info['channels'] == 2 and abs(x[-1]) < 1e-3,
          '16-bit stereo 48 kHz: resampled, cut with a fade (last sample %.5f)' % x[-1])
    check(abs(peak - irs.FACTORY_PEAK / irs.FULL) < 1e-9, 'normalised to the factory cabs\' peak')
    wav(p, imp, 96000, fmt=3)
    x, info = irs.convert(p)
    check(info['fmt'] == 'float' and info['resampled'] and 0 < info['kept'] <= 1, 'float 96 kHz: kept %.1f%% of the energy'
          % (100 * info['kept']))
    wav(p, [0.0] * 100, 44100)
    try:
        irs.convert(p)
        check(False, 'silence refused')
    except ValueError as e:
        check(True, 'silence refused: %s' % e)
    with open(p, 'wb') as f:
        f.write(b'not a wav')
    try:
        irs.convert(p)
        check(False, 'a file that is not a WAV refused')
    except ValueError as e:
        check(True, 'a file that is not a WAV refused: %s' % e)

    d3, d7 = irs.pack([0.1] * irs.SAMPLES), irs.pack([-0.2] * irs.SAMPLES)
    area = fs_image({3: ('CAB A', d3), 7: ('MEU IR 7', d7)})
    reads = []

    def read(off, n):
        reads.append((off, n))
        return area[off:off + n]
    s = irs.user_slots(read)
    check([i for i, v in enumerate(s) if v] == [3, 7] and s[3]['name'] == 'CAB A' and s[3]['data'] == d3 and
          s[7]['data'] == d7, 'file system: slots 4 and 8 with their names and data')
    check(sum(n for _, n in reads) == 0x10 + 3 * 0x1000 + 2 * irs.DATA_SIZE,
          'only the directory and the two blocks read (%d KB)' % (sum(n for _, n in reads) // 1024))
    check(irs.user_slots(lambda off, n: b'\xff' * n) == [None] * irs.SLOTS, 'an empty (erased) area: no slots')
    print('%d failed' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
