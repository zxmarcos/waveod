"""The editor protocol server against the original's, message by message (differential test).

    python tools/check_editor.py FIRMWARE.bin STOCK_FW.bin [--elf ...] [--rea DIR] [--only NAME]

The same requests go to both:
  - the original app V1.2.2 (STOCK_FW.bin) booted by matribox-studio's uiemu (matribox.app.uiemu): its
    own dispatcher FUN_0003A084 runs on each transfer (the link layer's accessor FUN_00031944 is replaced to
    hand it over); replies are caught where they leave (FUN_00030A0C packets, FUN_0003568C notifications);
  - waveod (FIRMWARE.bin) in tools/emu.py, through the USB controller model, the USB MIDI port and the
    link layer, as the PC editor would talk to it.
Both start from the factory presets and settings. Replies must be equal byte for byte, except the bytes the
original leaves uninitialised (masked, listed with each check). Needs unicorn (PYTHONPATH) and the
matribox-studio package (pip install -e) for the original side; --rea DIR uses a folder holding the old flat
tools (uiemu.py, fwtool.py, htmidi.py) instead.
"""
import argparse
import os
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQ, DATA = 0x11, 0x12


# ---------------------------------------------------------------- the original

class Stock:
    DISPATCH, ACCESS, SEND, NOTIFY = 0x3a084, 0x31944, 0x30a0c, 0x3568c
    # what FUN_0003A084 does before the messages: reloading the effect modules after a change (FUN_000204F4)
    # and FUN_00020C44 belong to the audio side, which this emulation does not run
    HOUSEKEEPING = (0x204f4, 0x20c44)
    MALLOC, FREE, HEAP = 0x674, 0x6d2, 0x81c00000

    def __init__(self, path):
        self.ui = ui = uiemu.Ui(fwtool.Firmware(open(path, 'rb').read()))
        ui.start()
        uc = ui.uc
        self.pending, self.out, self.raw, self.brk = None, [], [], self.HEAP
        for addr, fn in ((self.ACCESS, self.on_access), (self.SEND, self.on_send), (self.NOTIFY, self.on_notify),
                         (self.MALLOC, self.on_malloc), (self.FREE, self.on_free)) + tuple(
                            (a, self.on_free) for a in self.HOUSEKEEPING):
            uc.hook_add(UC_HOOK_CODE, fn, begin=addr, end=addr)

    def ret(self, uc, value=0):
        uc.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)

    def on_access(self, uc, addr, size, ud):
        """FUN_00031944(buf, len, &flag): the transfer the link layer completed, once"""
        buf, n, pflag = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        if self.pending is None:
            return self.ret(uc, 0)
        data, flag = self.pending
        if buf == 0:
            return self.ret(uc, len(data))
        uc.mem_write(buf, data[:n])
        if pflag:
            uc.mem_write(pflag, struct.pack('<H', flag << 8))
        self.pending = None
        self.ret(uc, min(n, len(data)))

    def on_send(self, uc, addr, size, ud):
        p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
        m = bytes(uc.mem_read(p, n))
        cmd, z = struct.unpack_from('<II', m)
        try:
            body = htmidi.lzo1x_decompress(m[8:8 + z]) if z else b''
        except IndexError:
            body = m[8:8 + z]
        self.out.append((cmd, body))
        self.raw.append(m)
        self.ret(uc)

    def on_notify(self, uc, addr, size, ud):
        p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
        m = bytes(uc.mem_read(p, n))
        cmd, k = struct.unpack_from('<II', m)
        self.out.append((cmd, m[8:8 + k]))
        self.ret(uc)

    def on_malloc(self, uc, addr, size, ud):
        n = uc.reg_read(UC_ARM_REG_R0)
        p, self.brk = self.brk, (self.brk + n + 15) & ~7
        self.ret(uc, p)

    def on_free(self, uc, addr, size, ud):
        self.ret(uc)

    def raw_export(self, index):
        """preset `index` as the original sends it, compressed by its LZO1X-1 compressor"""
        self.request(htmidi.message(0x10000002, struct.pack('<I', index)), REQ)
        return self.raw[0]

    def request(self, transfer, flag):
        self.pending, self.out, self.raw, self.brk = (transfer, flag), [], [], self.HEAP
        self.ui.fn(self.DISPATCH, sp=0x20008000 - 0x40)
        return self.out


# ---------------------------------------------------------------- waveod

def midi_events(sysex):
    """a SysEx as USB MIDI event packets (cable 0)"""
    out, i = bytearray(), 0
    while len(sysex) - i > 3:
        out += bytes([0x04]) + sysex[i:i + 3]
        i += 3
    rest = sysex[i:]
    out += bytes([0x04 + len(rest)]) + rest + bytes(3 - len(rest))
    return bytes(out)


CIN_LEN = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1]


class Waveod:
    def __init__(self, fw, elf, flash=None):
        self.b = b = emu.Board(fw, elf, flash)
        # both pedals at the top (between the calibrated maximum and the toe switch): where the original
        # leaves them at start-up (FUN_000399F0 sets the positions to 1.0)
        b.pots.update(EXP1=220 << 4, EXP2=220 << 4)
        assert b.run_ms(400), b.faults
        u = self.u = b.usb
        u.bus_reset()
        setup = lambda *a: struct.pack('<BBHHH', *a)
        assert u.control(setup(0x00, 5, 7, 0, 0)) == b''
        assert u.control(setup(0x00, 9, 1, 0, 0)) == b''
        self.stream = bytearray()
        self.rx = htmidi.Reassembler()
        self.phase = 0                      # audio blocks played (play)
        b.run_ms(800)                       # the pedals settle (their first report goes out)
        self.collect()

    def play(self, seconds):
        """audio blocks interleaved with the main loop, as on the pedal (the test tone, or what the emulated
        cable brings back: emu.Board.set_loopback)"""
        for _ in range(max(1, int(seconds / 0.0014512))):
            self.b.audio_block(self.phase)
            self.phase += 1
            self.b.run_ms(1.4512)

    def latency_cable(self, route, extra=7):
        """the cable of the latency test's route (0 OUT L, 1 OUT R, 2 SEND; others: none), `extra` samples beyond
        the buffers' 128, as the converters would add"""
        self.b.set_loopback({0: 'L', 1: 'R', 2: 'SEND'}.get(route), extra)

    def send(self, sysex):
        ev = midi_events(sysex)
        for at in range(0, len(ev), 512):
            for _ in range(400):
                if self.u.transfer_out(0x03, ev[at:at + 512]):
                    break
                self.b.run_ms(1)
            else:
                raise RuntimeError('the pedal stopped taking MIDI data')

    def collect(self):
        got = []
        while True:
            d = self.u.transfer_in(0x83)
            if d is None:
                break
            for i in range(0, len(d) - 3, 4):
                self.stream += d[i + 1:i + 1 + CIN_LEN[d[i] & 0x0f]]
        while 0xF7 in self.stream:
            e = self.stream.index(0xF7)
            s = self.stream.rfind(0xF0, 0, e)
            if s >= 0:
                r = self.rx.feed(bytes(self.stream[s:e + 1]))
                if r and r[0] != htmidi.FLAG_NAK:
                    got.append((r[1], r[2]))
                elif r:
                    got.append(('NAK', r[2]))
            del self.stream[:e + 1]
        return got

    def request(self, transfer, flag, quiet_ms=260, max_ms=3000):
        for p in htmidi.packets(transfer, flag):
            self.send(p)
        out, idle, t = [], 0, 0
        while t < max_ms and idle < quiet_ms:
            self.b.run_ms(5)
            t += 5
            new = self.collect()
            out += new
            idle = 0 if new else idle + 5
        if self.b.faults:
            raise RuntimeError('; '.join(self.b.faults))
        return out


# ---------------------------------------------------------------- what may differ

def preset_masks(body):
    """the preset reply: version, author / info of block 0x0101 and five bytes of block 0x0108 are not set; the
    name's bytes after its zero are what an earlier, longer name left (waveod's own presets do not keep them)"""
    n = len(body)
    if n != 0x4bb:
        return []
    info = 4 + 19 + 12                      # index, block 0x0100, the 0x0101 header
    ctrl = 4 + 19 + (12 + 0x69 - 4) + (12 + 10 - 4) + (12 + 0x0f - 4) + (12 + 0x4c - 4) + (12 + 0x2e6 - 4) + \
        (12 + 0x5f - 4) + 12
    return list(range(info, info + 4)) + list(range(info + 4 + 20, info + 4 + 87)) + list(range(ctrl, ctrl + 5)) + \
        name_masks(body, info + 4, 1, 20)


def name_masks(body, first, count, width=17):
    """bytes after the terminating zero of fixed-width names"""
    m = []
    for i in range(count):
        f = body[first + width * i:first + width * (i + 1)]
        z = f.find(b'\0')
        if z >= 0:
            m += range(first + width * i + z + 1, first + width * (i + 1))
    return m


def masks(cmd, body):
    sub = struct.unpack_from('<I', body)[0] if len(body) >= 4 else None
    if cmd in (0x10000000, 0x10000002):
        return preset_masks(body)
    if cmd == 0x10000001 and sub == 0x01040000:
        return [len(body) - 1]              # the request buffer's byte the original echoes
    if cmd == 0x10000001 and sub == 0x01050000:
        return list(range(len(body) - 4, len(body)))
    if cmd == 0x10000001 and sub == 0x01010000:
        return list(range(4, 8)) + list(range(28, 95))
    if cmd == 0x10000001 and len(body) == 0x15 and sub == 0x11:     # name read: two bytes unset after the u16
        return [2, 3]
    if cmd == 0x10000001 and len(body) == 0x12 and sub & 0xffff0000 != 0x01010000:   # style read
        return [0, 1, 2, 3]
    if cmd == 0x10000001 and sub == 0x01080000:
        return list(range(4, 9))
    if cmd == 0x10000001 and sub == 0x010A0000:
        return name_masks(body, 4 + 960, 240) + name_masks(body, 4 + 960 + 4080, 240, 10)
    if cmd == 0x11000000:
        return [0, 1, 2, 3, 4, len(body) - 1]
    if cmd == 0x14000000:
        return name_masks(body, 160, 20)
    if cmd == 0x18000000:
        return name_masks(body, 80, 10) + [250, 251]
    return []


def describe(msgs):
    return ', '.join('%s:%s' % (c if isinstance(c, str) else hex(c), b[:12].hex()) for c, b in msgs)


def same(a, b):
    if len(a) != len(b):
        return False, 'counts %d / %d\n      ours   %s\n      theirs %s' % (len(a), len(b), describe(a), describe(b))
    for (ca, ba), (cb, bb) in zip(a, b):
        if ca != cb or len(ba) != len(bb):
            return False, 'cmd %s len %d / cmd %s len %d' % (
                ca if isinstance(ca, str) else hex(ca), len(ba), cb if isinstance(cb, str) else hex(cb), len(bb))
        skip = set(masks(ca, bb))
        diff = [i for i in range(len(ba)) if ba[i] != bb[i] and i not in skip]
        if diff:
            i = diff[0]
            return False, 'cmd %#x: %d bytes differ: %s' % (ca, len(diff), ', '.join(
                '%#x %02x/%02x' % (k, ba[k], bb[k]) for k in diff[:24]))
    return True, ''


# ---------------------------------------------------------------- the requests

def transfer(cmd, *words, payload=None):
    if payload is None:
        payload = b''.join(struct.pack('<I', w & 0xffffffff) for w in words)
    return htmidi.message(cmd, payload)


def scenarios():
    s = [('edited preset', transfer(0x10000000, 0), REQ)]
    for i in (0, 1, 7, 119, 120, 239):
        s.append(('export %d' % i, transfer(0x10000002, i), REQ))
    s += [
        ('list', transfer(0x10000001, 0x010A0000), REQ),
        ('index', transfer(0x10000001, 0x010A0001), REQ),
        ('dimensions', transfer(0x10000001, 0x01000000), REQ),
        ('info', transfer(0x10000001, 0x01010000), REQ),
        ('name', transfer(0x10000001, 0x01010001, 3), REQ),
        ('quick knobs', transfer(0x10000001, 0x01020000), REQ),
        ('footswitches', transfer(0x10000001, 0x01030000), REQ),
        ('chain', transfer(0x10000001, 0x01040000), REQ),
        ('parameters', transfer(0x10000001, 0x01050000), REQ),
        ('knob 2/1', transfer(0x10000001, 0x01050001, 2, 1), REQ),
        ('switch 3', transfer(0x10000001, 0x01050002, 3), REQ),
        ('volume', transfer(0x10000001, 0x01050003), REQ),
        ('expression', transfer(0x10000001, 0x01070000), REQ),
        ('ctrl', transfer(0x10000001, 0x01080000), REQ),
        ('ctrl 1/2', transfer(0x10000001, 0x01080001, 1, 2), REQ),
        ('version', transfer(0x10000001, 0x01090000), REQ),
        ('settings', transfer(0x11000000), REQ),
        ('global eq', transfer(0x16000000), REQ),
        ('global eq switch', transfer(0x16000001), REQ),
        ('global eq 5', transfer(0x16000002, 5), REQ),
        ('drums', transfer(0x13000000), REQ),
        ('user irs', transfer(0x14000000), REQ),
        ('clone slots', transfer(0x18000000), REQ),
    ]
    for sub in (0x0500, 0x0600, 0x0700, 0x0800, 0x1100, 0x1300, 0x1301, 0x1400, 0x1500, 0x2100, 0x2200, 0x2300,
                0x2301, 0x2400, 0x2500, 0x3100, 0x3200, 0x3300, 0x3400, 0x3401, 0x4101, 0x4200, 0x5100, 0x5200,
                0x6100, 0x6103, 0x6200, 0x6300, 0x6400, 0x8100, 0x8200, 0x8400, 0x8500, 0x8600):
        s.append(('setting %#06x' % sub, transfer(0x11000001, sub), REQ))
    return s + writes()


def f32(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def edit_read(tag):
    return [('%s: edited preset' % tag, transfer(0x10000000, 0), REQ)]


def writes():
    """changes, each followed by what it should have changed (the state carries on from one to the next)"""
    sub = lambda *w: transfer(0x10000001, *w)
    name = lambda s, n=17: s.encode().ljust(n, b'\0')
    s = []
    s += [('switch 2 on', sub(0x01050002, 2, 1), DATA), ('switch 2 off', sub(0x01050002, 2, 0), DATA)]
    s += [('knob 3/1 = 55.5', sub(0x01050001, 3, 1, f32(55.5)), DATA),
          ('knob 3/1 = -5 (clamped)', sub(0x01050001, 3, 1, f32(-5.0)), DATA),
          ('knob 6/0 = 7.25', sub(0x01050001, 6, 0, f32(7.25)), DATA)]
    s += [('volume 60', sub(0x01050003, 60), DATA), ('pan -20', sub(0x01050005, 0xec), DATA)]
    s += edit_read('after knobs')
    s += [('quick knob 0 -> slot 5', sub(0x01020001, 0, 5), DATA),
          ('quick knob 0 parameter 2', sub(0x01020002, 0, 2), DATA),
          ('footswitch A tap', sub(0x01030001, 0, 8), DATA),
          ('footswitch B hold', sub(0x01030002, 1, 7), DATA),
          ('footswitch combo 1', sub(0x01030003, 1, 6), DATA),
          ('exp 1A target 0 -> slot 3 parameter 0', sub(0x01070002, 0, 0, 3, 0), DATA),
          ('exp 1A target 0 max 70', sub(0x01070004, 0, 0, f32(70.0)), DATA),
          ('exp 1A target 0 min 10', sub(0x01070003, 0, 0, f32(10.0)), DATA),
          ('ctrl 2 member 4', sub(0x01080001, 2, 4, 1), DATA)]
    s += edit_read('after assignments')
    chain = lambda old, new, menu, sid: transfer(0x10000001, payload=struct.pack('<Ibbb', 0x01040001, old, new, menu)
                                                 + struct.pack('<I', sid))
    s += [('chain: move 1 -> 3', chain(1, 3, 2, 0x5000008), DATA),
          ('chain: remove position 2', chain(2, -1, 0, 0), DATA),
          ('chain: insert a drive at 1', chain(-1, 1, 3, 0x3000009), DATA),
          ('chain: replace position 0', chain(0, 0, 4, 0x700002f), DATA)]
    s += edit_read('after chain edits') + [('chain after edits', sub(0x01040000), REQ)]
    s += [('activate 5', sub(0x010A0001, 5), DATA)] + edit_read('preset 5')
    s += [('rename 7', transfer(0x10000001, payload=struct.pack('<II', 0x01010001, 7) + name('Renamed Seven')),
           DATA), ('list after rename', sub(0x010A0000), REQ)]
    s += [('save as 9', transfer(0x05000001, payload=struct.pack('<I', 9) + name('Saved Nine', 20)), DATA),
          ('export 9', transfer(0x10000002, 9), REQ)]
    s += [('move 3 -> 6', transfer(0x05000003, 3, 6), DATA), ('delete 4', transfer(0x05000005, 4), DATA),
          ('copy 1 -> 8', transfer(0x05000006, 1, 8), DATA), ('list after moves', sub(0x010A0000), REQ),
          ('index after moves', sub(0x010A0001), REQ)]
    s += [('import 2 (compressed by the original)', 'IMPORT', DATA), ('export 2', transfer(0x10000002, 2), REQ)]
    g = lambda w, v: transfer(0x11000001, w, v & 0xffffffff)
    for w, v in ((0x0500, 1), (0x1100, 2), (0x1301, 1), (0x2100, -7), (0x2100, 30), (0x2200, 2), (0x2301, 1),
                 (0x2400, 120), (0x2500, 1), (0x3100, 5), (0x3200, -3), (0x3300, 0), (0x3401, 0), (0x4101, 1),
                 (0x4202, 9), (0x5100, 1), (0x5200, 1), (0x6102, 20), (0x6200, 2), (0x6300, 3), (0x6400, 1),
                 (0x8200, 2 << 16 | 3), (0x8300, 4), (0x8600, 70), (0x8500, 1), (0x8400, 130)):
        s.append(('set %#06x = %d' % (w, v), g(w, v), DATA))
    s += [('settings after sets', transfer(0x11000000), REQ), ('drums after sets', transfer(0x13000000), REQ)]
    s += [('eq on', transfer(0x16000001, 0, 0, 0, 1), DATA), ('eq 4 = 3000', transfer(0x16000002, 4, f32(3000.0)), DATA),
          ('eq 6 = 99 (clamped)', transfer(0x16000002, 6, f32(99.0)), DATA), ('eq after sets', transfer(0x16000000), REQ)]
    # user IRs and Clone captures: {slot, 0, 0, name[17] (padded to +0x20), data}
    media = lambda cmd, slot, nm, n, seed: transfer(cmd, payload=struct.pack('<III', slot, 0, 0) + name(nm).ljust(0x14,
                                                     b'\0') + bytes((i * seed + 7) & 0xff for i in range(n)))
    s += [('user ir 2 written', media(0x14000001, 2, 'Mesa 4x12 V30', 0x1000, 3), DATA),
          ('user ir 2 rewritten', media(0x14000001, 2, 'Mesa 4x12 G12', 0x1000, 5), DATA),
          ('user ir 25 (no such slot)', media(0x14000001, 25, 'Nope', 0x1000, 9), DATA),
          ('user ir refused (word 3)', transfer(0x14000001, 4, 1, 0), DATA),
          ('user ir 2 renamed', transfer(0x14000002, payload=struct.pack('<I', 2) + name('Cab Two')), DATA),
          # at most 0x2000 - 48 bytes: the original's decompression buffer ends at 0x2023CDE0 (tools/nam_clone.py)
          ('clone 1 written', media(0x18000001, 1, 'Tweed Deluxe', 0x2000 - 48, 11), DATA),
          ('clone 1 renamed', transfer(0x18000002, payload=struct.pack('<I', 1) + name('Tweed 57')), DATA),
          ('user irs after writes', transfer(0x14000000), REQ), ('clone slots after writes', transfer(0x18000000), REQ),
          ('clone 1 deleted', transfer(0x11000001, 0x9000, 1), DATA),
          ('clone slots after delete', transfer(0x18000000), REQ)]
    return s


def main():
    global uiemu, fwtool, htmidi
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('stock')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--rea', help='folder with the old flat tools instead of the matribox package')
    ap.add_argument('--only')
    ap.add_argument('--writes', action='store_true', help='only the changes (the reads before them change nothing)')
    a = ap.parse_args()
    if a.rea:
        sys.path.insert(0, a.rea)
        import uiemu, fwtool, htmidi       # noqa: E401
    else:
        from matribox.app import uiemu
        from matribox.firmware import fwtool
        from matribox.usb import htmidi
    theirs_app = Stock(a.stock)
    ours_app = Waveod(a.fw, a.elf)
    failed = 0
    for name, t, flag in (writes() if a.writes else scenarios()):
        if a.only and not any(o in name for o in a.only.split(',')):
            continue
        if t == 'IMPORT':                   # preset 2 as the original exports it, its own compression included,
            t = theirs_app.raw_export(2)    # imported over itself (the index is inside the compressed data)
        theirs = theirs_app.request(t, flag)
        ours = ours_app.request(t, flag)
        ok, why = same(ours, theirs)
        print('%s  %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ': ' + why))
        failed += not ok
    print('%d failed' % failed)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
