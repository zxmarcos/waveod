"""The USB device in emulation: enumeration, audio class requests, isochronous streaming, MIDI.

    python tools/check_usb.py FIRMWARE.bin [--elf build/fw/waveod.elf]

Boots the firmware in tools/emu.py, whose ChipIdea model runs the real driver (board/qme200/usb_dcd.c), and
plays the host: bus reset, descriptors (checked against the original's identity and layout), address,
configuration, the UAC2 clock / feature unit requests, both streams at 24 bits (recording packet sizes and
contents, playback reaching the converters through the monitor and re-amp paths), and the MIDI endpoints.
Needs the 'unicorn' package (PYTHONPATH).
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE = bytes.fromhex('12010002ef020140ef84820201010102' '0301')


def setup(rtype, req, value=0, index=0, length=0):
    return struct.pack('<BBHHH', rtype, req, value, index, length)


class Checks:
    def __init__(self):
        self.failed = 0

    def __call__(self, ok, what, detail=''):
        print('%s  %s%s' % ('ok  ' if ok else 'FAIL', what, (': ' + detail) if detail and not ok else ''))
        self.failed += not ok


def descriptors(cfg):
    out, i = [], 0
    while i < len(cfg):
        n = cfg[i]
        if n < 2:
            return None
        out.append(cfg[i:i + n])
        i += n
    return out if i == len(cfg) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    check = Checks()
    b = emu.Board(a.fw, a.elf)
    check(b.run_ms(400), 'boot', '; '.join(b.faults))
    u = b.usb
    check(b.regs.get(emu.USB_USBCMD, 0) & 1 == 1, 'attached (USBCMD.RS)')
    check(emu.USB_IRQ in b.nvic, 'USB interrupt enabled')

    u.bus_reset()
    d = u.control(setup(0x80, 6, 0x0100, 0, 64))
    check(d == DEVICE, 'device descriptor', d.hex() if d else 'stalled')
    check(u.control(setup(0x00, 5, 9)) == b'', 'SET_ADDRESS')
    check(b.regs.get(emu.USB1 + 0x154, 0) == (9 << 25 | 1 << 24), 'address register')
    q = u.control(setup(0x80, 6, 0x0600, 0, 10))
    check(q is not None and len(q) == 10 and q[1] == 6, 'device qualifier')
    head = u.control(setup(0x80, 6, 0x0200, 0, 9))
    check(head is not None and head[:2] == b'\x09\x02', 'configuration header')
    total = struct.unpack_from('<H', head, 2)[0] if head else 0
    cfg = u.control(setup(0x80, 6, 0x0200, 0, 0xffff))
    check(cfg is not None and len(cfg) == total == 0x1b5, 'configuration 6-In 4-Out (437 bytes)',
          '%s bytes' % (len(cfg) if cfg else None))
    ds = descriptors(cfg or b'')
    check(ds is not None, 'configuration parses')
    if ds:
        eps = sorted(d[2] for d in ds if d[1] == 5)
        check(eps == [0x01, 0x01, 0x03, 0x81, 0x81, 0x83], 'endpoints', str([hex(e) for e in eps]))
        ifs = sorted({(d[2], d[3]) for d in ds if d[1] == 4})
        check(ifs == [(0, 0), (1, 0), (1, 1), (1, 2), (2, 0), (2, 1), (2, 2), (3, 0)], 'interfaces')
    names = []
    for i in range(1, 7):
        s = u.control(setup(0x80, 6, 0x0300 | i, 0x409, 255))
        names.append(s[2:].decode('utf-16-le') if s else None)
    check(names[:2] == ['Sonicake', 'Matribox II PRO'] and names[5] == 'Matribox II PRO MIDI', 'strings', str(names))
    check(u.control(setup(0x80, 6, 0x0307, 0x409, 255)) is None, 'no string 7 (stall)')
    check(u.control(setup(0x80, 6, 0x0f00, 0, 5)) is None, 'no BOS descriptor (stall)')

    check(u.control(setup(0x00, 9, 1)) == b'', 'SET_CONFIGURATION 1')
    check(u.control(setup(0x80, 8, 0, 0, 1)) == b'\x01', 'GET_CONFIGURATION')
    check(u.next_td(0x03) is not None, 'MIDI OUT armed')

    # UAC2: clock source 0x11, feature units 0x09 (recording) / 0x0c (playback)
    f = u.control(setup(0xa1, 1, 0x0100, 0x1100, 4))
    check(f == struct.pack('<I', 44100), 'clock: 44.1 kHz', f.hex() if f else 'stalled')
    r = u.control(setup(0xa1, 2, 0x0100, 0x1100, 14))
    check(r == bytes.fromhex('010044ac000044ac000000000000'), 'clock range')
    # the rate the range has is taken; another is refused, and the clock goes on reporting the one it runs at
    check(u.control(setup(0x21, 1, 0x0100, 0x1100, 4), struct.pack('<I', 44100)) == b'', 'clock: SET 44.1 kHz')
    check(u.control(setup(0x21, 1, 0x0100, 0x1100, 4), struct.pack('<I', 48000)) is None, 'clock: SET 48 kHz stalls')
    f = u.control(setup(0xa1, 1, 0x0100, 0x1100, 4))
    check(f == struct.pack('<I', 44100), 'clock: still 44.1 kHz after SET 48 kHz', f.hex() if f else 'stalled')
    v = u.control(setup(0xa1, 1, 0x0201, 0x0900, 2))
    check(v == b'\x64\x00', 'recording channel 1 volume 100')
    check(u.control(setup(0x21, 1, 0x0201, 0x0c00, 2), b'\x32\x00') == b'', 'playback channel 1 volume -> 50')
    check(u.control(setup(0xa1, 1, 0x0201, 0x0c00, 2)) == b'\x32\x00', 'playback channel 1 volume reads 50')
    check(u.control(setup(0x21, 1, 0x0201, 0x0c00, 2), b'\x64\x00') == b'', 'back to 100')
    check(u.control(setup(0xa1, 1, 0x0208, 0x0900, 2)) is None, 'no recording channel 8 (stall)')

    # recording at 24 bits (alternate setting 2): 6 channels x 4 bytes, 5 or 6 frames per microframe
    check(u.control(setup(0x01, 11, 2, 1)) == b'', 'recording on (interface 1 alt 2)')
    check(u.control(setup(0x01, 11, 2, 2)) == b'', 'playback on (interface 2 alt 2)')
    phase, frames, sizes, bad, rec = 0, 0, [], 0, []
    out_frames, uframes = [], 0.0
    for blk in range(150):
        # 64 frames of playback per block: channel 1 a 500 Hz tone, channel 3 silent
        while sum(out_frames) < 64 * (blk + 1) + 80:
            n = 6 if len(out_frames) % 2 else 5
            pk = b''
            for i in range(n):
                t = sum(out_frames) + i
                pk += struct.pack('<4i', int(0.2 * 2 ** 31 * math.sin(2 * math.pi * 500 * t / 44100)), 0, 0, 0)
            if not u.transfer_out(0x01, pk):
                break
            out_frames.append(n)
        b.audio_block(phase, freq=0.0, level=0.0)
        phase += 1
        uframes += 64 / 44100 * 8000                # the microframes of one block (11.6)
        while uframes >= 1:
            uframes -= 1
            p = u.transfer_in(0x81)
            if p is None:
                break
            if len(p) % 24:
                bad += 1
            sizes.append(len(p) // 24)
            rec.append(p)
    check(b.faults == [], 'no faults while streaming', '; '.join(b.faults))
    check(bad == 0, 'recording packets hold whole frames')
    steady = sizes[100:]
    check(steady and set(steady) <= {5, 6}, 'recording packets of 5 or 6 frames', str(sorted(set(sizes))))
    rate = sum(steady) / len(steady) * 8000 if steady else 0
    check(abs(rate - 44100) < 300, 'recording rate', '%.0f frames/s' % rate)
    # the playback tone reaches the outputs (monitor path) and the final-mix recording channels 5 / 6
    _, dac = b.audio_block(phase, freq=0.0, level=0.0)
    peak = max(abs(x) for x in dac[0::4]) / 2 ** 31
    check(peak > 0.01, 'playback tone at the outputs', 'peak %.4f' % peak)
    mix = [struct.unpack_from('<6i', p, 24 * i)[4] for p in rec[-40:] for i in range(len(p) // 24)]
    check(mix and max(abs(x) for x in mix) > 0.05 * 2 ** 31, 'playback tone in recording channel 5')
    check(u.control(setup(0x01, 11, 0, 1)) == b'' and u.control(setup(0x01, 11, 0, 2)) == b'', 'streams off')

    # "2-In 2-Out": asked for through the editor protocol (global setting 0x0600 = 1, a SysEx on the MIDI port)
    msg = struct.pack('<II', 0x11000001, 12) + bytes([8 + 17]) + struct.pack('<II', 0x0600, 1) + b'\x11\0\0'
    nib = bytes(x for byte in msg for x in (byte >> 4, byte & 15))
    sysex = bytes([0xF0, 0x21, 0x25, 0x4D, 0x50, 0, 0, sum(nib) & 0x7f, 0x12, len(msg) & 0x7f, len(msg) >> 7, 0, 0]) + \
        nib + b'\xf7'
    ev = b''
    for i in range(0, len(sysex), 3):
        part = sysex[i:i + 3]
        cin = 0x4 if len(sysex) - i > 3 else 0x4 + len(part)
        ev += bytes([cin]) + part.ljust(3, b'\0')
    check(u.transfer_out(0x03, ev), 'mode change sent over MIDI')
    b.run_ms(50)
    check(b.regs.get(emu.USB_USBCMD, 0) & 1 == 0, 'the pedal leaves the bus')
    b.run_ms(400)
    check(b.regs.get(emu.USB_USBCMD, 0) & 1 == 1, 'and comes back')
    u.bus_reset()
    u.control(setup(0x00, 5, 3))
    cfg = u.control(setup(0x80, 6, 0x0200, 0, 0xffff))
    ds = descriptors(cfg or b'')
    check(cfg is not None and len(cfg) == 0xf7 and ds is not None, 'configuration 2-In 2-Out (247 bytes)')
    if ds:
        check(sorted({d[2] for d in ds if d[1] == 4}) == [0, 1, 2], 'three interfaces, no MIDI')
    check(u.control(setup(0x00, 9, 1)) == b'' and u.next_td(0x03) is None, 'configured, no MIDI endpoint')
    check(u.control(setup(0x01, 11, 1, 1)) == b'', 'stereo recording on')
    got = []
    for blk in range(30):
        b.audio_block(phase, freq=0.0, level=0.0)
        phase += 1
        for _ in range(11):
            p = u.transfer_in(0x81)
            if p is not None:
                got.append(len(p))
    check(got and all(n % 8 == 0 for n in got) and set(n // 8 for n in got[40:]) <= {5, 6},
          'stereo packets of 5-6 frames x 8 bytes', str(sorted(set(got))))

    print('%d failed' % check.failed)
    sys.exit(1 if check.failed else 0)


if __name__ == '__main__':
    main()
