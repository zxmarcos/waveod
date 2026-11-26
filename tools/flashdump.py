"""Read the pedal's QSPI flash over USB while waveod runs (editor protocol group 0xF, src/editor/wv_msgs.c).

    python tools/flashdump.py OUT.bin                       the bootloader (0x000000-0x020000)
    python tools/flashdump.py OUT.bin --all                 the whole 8 MB (a few minutes)
    python tools/flashdump.py OUT.bin --start 0x3A0000 --length 0x6A000
    python tools/flashdump.py --summary DUMP.bin            what an 8 MB dump holds where (no pedal)

Read only: the pedal writes nothing. Close the PC editor / Studio first (they hold the MIDI port). Needs the
USB MIDI transport of waveod Studio (studio/waveod_studio/usb/htmidi.py). After a dump of the whole flash, --summary shows
the bytes other than 0xFF in each area and in the gaps between the update sections, which tells whether the
bootloader erases a whole partition or only what it writes.
"""
import argparse
import os
import struct
import sys
import time

CMD_READ = 0x1F000000
CHUNK = 0x2000
FLASH = 0x800000

# (name, start, end) of the flash areas (src/fw/flash_map.h, research/NOTES.md)
AREAS = (
    ('bootloader', 0x000000, 0x020000),
    ('app partition (section 0)', 0x020000, 0x40A000),
    ('Clone captures', 0x40A000, 0x440000),
    ('factory presets (section 3)', 0x440000, 0x480000),
    ('effect library (section 1)', 0x480000, 0x700000),
    ('factory IRs (section 2)', 0x700000, 0x753000),
    ('user IRs', 0x753000, 0x780000),
    ('record store', 0x780000, 0x800000),
)


def num(s):
    return int(s, 0)


def read_chunk(port, htmidi, off, n, tries=4):
    for k in range(tries):
        try:
            r = htmidi.transact(port, CMD_READ, struct.pack('<II', off, n), flag=htmidi.FLAG_REQ, want=CMD_READ,
                                timeout=4.0, gap=0.0, log=lambda *a: None)
        except (TimeoutError, RuntimeError) as e:
            err = e
            time.sleep(0.2)
            continue
        if len(r) >= 8:
            got_off, got_n = struct.unpack_from('<II', r, 0)
            if got_off == off and got_n == n and len(r) == 8 + n:
                return r[8:]
            err = 'reply for %#x+%#x, wanted %#x+%#x (%d bytes)' % (got_off, got_n, off, n, len(r))
        else:
            err = 'short reply (%d bytes)' % len(r)
    raise RuntimeError('reading %#08x: %s' % (off, err))


def dump(a):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'studio'))
    from waveod_studio.usb import htmidi
    start, length = (0, FLASH) if a.all else (a.start, a.length)
    if start < 0 or length <= 0 or start + length > FLASH:
        sys.exit('range %#x+%#x is outside the 8 MB flash' % (start, length))
    out = bytearray()
    t0 = time.time()
    with htmidi.Port(a.port) as port:
        print('port: %s' % port.name)
        for off in range(start, start + length, CHUNK):
            n = min(CHUNK, start + length - off)
            out += read_chunk(port, htmidi, off, n)
            done = off + n - start
            rate = done / max(time.time() - t0, 1e-3) / 1024
            print('\r%06X  %5.1f%%  %.0f KB/s' % (off + n, 100.0 * done / length, rate), end='', flush=True)
    print()
    with open(a.out, 'wb') as f:
        f.write(out)
    print('%s: %d bytes from %#08x' % (a.out, len(out), start))
    if len(out) == FLASH:
        summary(bytes(out))


def used(data, lo, hi):
    """bytes other than 0xFF in [lo, hi) and the last of them"""
    seg = data[lo:hi]
    n = len(seg) - seg.count(0xFF)
    last = lo + len(seg.rstrip(b'\xff')) - 1 if n else None
    return n, last


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


def summary(data):
    if len(data) != FLASH:
        sys.exit('--summary wants a dump of the whole flash (%d bytes)' % FLASH)
    print('\n%-28s %-17s %10s  %s' % ('area', 'range', 'not FF', 'last'))
    for name, lo, hi in AREAS:
        n, last = used(data, lo, hi)
        print('%-28s %06X-%06X %10d  %s' % (name, lo, hi, n, '%06X' % last if last is not None else '-'))
    # the gaps: after this app's image, after the original's app, after the library and the factory IRs
    end = 0x20000
    for k in range(2):
        fa, n = u32(data, 0x20010 + 12 * k), u32(data, 0x20014 + 12 * k)
        if 0x20000 <= fa and fa + n < 0x40A000:
            end = max(end, fa + n)
    lib = u32(data, 0x48000C)
    lib = 0x480000 + lib if 0 < lib < 0x280000 else 0x700000
    print('\ngaps between what the update sections hold:')
    for name, lo, hi in (('app partition after this image', end, 0x3A0000),
                         ("after the original's app", 0x3A0000, 0x40A000),
                         ('library partition after the library', lib, 0x700000),
                         ('after the factory IRs', 0x73F000, 0x753000)):
        n, last = used(data, lo, hi)
        print('  %-38s %06X-%06X %10d  %s' % (name, lo, hi, n, '%06X' % last if last is not None else '-'))
    n, last = used(data, 0, 0x20000)
    print('\nbootloader: %d bytes used, last at %s; FlexSPI config tag %r (FCFB expected), IVT word at 0x1000 %08X'
          % (n, '%06X' % last if last is not None else '-', bytes(data[0:4]), u32(data, 0x1000)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?', help='file to write (or the dump to read with --summary)')
    ap.add_argument('--start', type=num, default=0)
    ap.add_argument('--length', type=num, default=0x20000)
    ap.add_argument('--all', action='store_true', help='the whole 8 MB')
    ap.add_argument('--port', help='MIDI port name or number (default: the pedal)')
    ap.add_argument('--summary', metavar='DUMP', help='only summarize an 8 MB dump')
    a = ap.parse_args()
    if a.summary:
        summary(open(a.summary, 'rb').read())
    elif a.out:
        dump(a)
    else:
        ap.error('give OUT.bin (or --summary DUMP.bin)')


if __name__ == '__main__':
    main()
