"""MIDI in emulation: the TRS port, USB MIDI, the clock timer.

    python tools/check_midi.py FIRMWARE.bin [--elf build/fw/waveod.elf]

Boots the firmware in tools/emu.py (LPUART1 and GPT2 modelled, the USB controller as for check_usb.py), plays
MIDI into the TRS input and the USB port, and reads what happened from the outputs: the thru bytes, the
notifications the pedal sends the editor (preset, volume, block switches), and the clock timer's compare
register (its spacing is the tempo the pedal runs at). Settings are changed through the editor protocol, the
way the PC editor does. Needs the 'unicorn' package (PYTHONPATH).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CIN_LEN = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1]
SUB_INDEX, SUB_SWITCH, SUB_VOLUME = 0x010A0001, 0x01050002, 0x01050003
CMD_PRESET_SUB, CMD_GLOBAL_SUB = 0x10000001, 0x11000001


def setup(rtype, req, value=0, index=0, length=0):
    return struct.pack('<BBHHH', rtype, req, value, index, length)


class Checks:
    def __init__(self):
        self.failed = 0

    def __call__(self, ok, what, detail=''):
        print('%s  %s%s' % ('ok  ' if ok else 'FAIL', what, (': ' + detail) if detail and not ok else ''))
        self.failed += not ok


def lzo_literals(z):
    """the pedal's LZO1X streams are one literal run (editor/lzo.c lzo1x_store)"""
    if not z:
        return b''
    if z[0] > 17:
        return z[1:1 + z[0] - 17]
    i, k = 1, 0
    while z[i] == 0:
        k, i = k + 1, i + 1
    return z[i + 1:i + 1 + 18 + 255 * k + z[i]]


class Pedal:
    def __init__(self, fw, elf):
        self.b = b = emu.Board(fw, elf)
        b.pots.update(EXP1=220 << 4, EXP2=220 << 4)     # pedals at rest: no expression notifications
        self.ok = b.run_ms(400)
        u = self.u = b.usb
        u.bus_reset()
        u.control(setup(0x00, 5, 7))
        u.control(setup(0x00, 9, 1))
        self.stream, self.rx, self.events = bytearray(), {}, []
        b.run_ms(800)
        self.collect()
        self.events.clear()
        b.uart_tx.clear()

    def usb_send(self, data):
        """MIDI bytes as USB event packets (channel messages, or a SysEx)"""
        ev = bytearray()
        if data[0] == 0xF0:
            i = 0
            while len(data) - i > 3:
                ev += bytes([0x04]) + data[i:i + 3]
                i += 3
            rest = data[i:]
            ev += bytes([0x04 + len(rest)]) + rest + bytes(3 - len(rest))
        else:
            i = 0
            while i < len(data):
                n = 1 if data[i] >= 0xF8 else 2 if data[i] >> 4 in (0xC, 0xD) else 3
                cin = 0xF if data[i] >= 0xF8 else data[i] >> 4
                ev += bytes([cin]) + data[i:i + n] + bytes(3 - n)
                i += n
        for at in range(0, len(ev), 512):
            for _ in range(200):
                if self.u.transfer_out(0x03, bytes(ev[at:at + 512])):
                    break
                self.b.run_ms(1)

    def set_global(self, sub, value):
        """a global setting through the editor protocol (0x11000001 {sub, value}, flag 0x12)"""
        payload = struct.pack('<Ii', sub, value)
        msg = struct.pack('<II', CMD_GLOBAL_SUB, len(payload) + 4) + bytes([len(payload) + 17]) + payload + b'\x11\0\0'
        nib = bytes(x for byte in msg for x in (byte >> 4, byte & 15))
        self.usb_send(bytes([0xF0, 0x21, 0x25, 0x4D, 0x50, 0, 0, sum(nib) & 0x7f, 0x12, len(msg) & 0x7f,
                             len(msg) >> 7, 0, 0]) + nib + b'\xf7')
        self.wait(250)

    def collect(self):
        """IN transfers: notifications decoded into self.rx[(cmd, word0)] (latest), MIDI events kept"""
        while True:
            d = self.u.transfer_in(0x83)
            if d is None:
                break
            for i in range(0, len(d) - 3, 4):
                cin = d[i] & 0x0f
                if cin in (0x4, 0x5, 0x6, 0x7):
                    self.stream += d[i + 1:i + 1 + CIN_LEN[cin]]
                else:
                    self.events.append(bytes(d[i + 1:i + 1 + CIN_LEN[cin]]))
        while 0xF7 in self.stream:
            e = self.stream.index(0xF7)
            s = self.stream.rfind(0xF0, 0, e)
            p = bytes(self.stream[s:e + 1]) if s >= 0 else b''
            del self.stream[:e + 1]
            if len(p) < 14 or p[8] != 0x12:
                continue
            nib = p[13:-1]
            body = bytes(nib[2 * i] << 4 | nib[2 * i + 1] for i in range(len(nib) // 2))
            if (p[11] | p[12] << 7) != 0 or len(body) != (p[9] | p[10] << 7):
                continue                    # one-packet transfers are all this test expects
            at = 0
            while at + 8 <= len(body):
                cmd, z = struct.unpack_from('<II', body, at)
                words = lzo_literals(body[at + 8:at + 8 + z])
                if len(words) >= 4:
                    w = struct.unpack('<%dI' % (len(words) // 4), words[:len(words) // 4 * 4])
                    self.rx[(cmd, w[0])] = w[1:]
                at += 8 + z

    def wait(self, ms):
        self.b.run_ms(ms)
        self.collect()

    def notified(self, cmd, word):
        return self.rx.pop((cmd, word), None)

    def trs(self, data, ms=200):
        self.b.uart_in(data)
        self.wait(ms)

    def clock_interval(self):
        """one run of the clock timer: the spacing (24 MHz ticks) it set for the next"""
        before = self.b.regs.get(emu.GPT2_OCR1, 0)
        self.b.call(self.b.sym('gpt2_irq'))
        return (self.b.regs.get(emu.GPT2_OCR1, 0) - before) & 0xffffffff


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    check = Checks()
    p = Pedal(a.fw, a.elf)
    b = p.b
    check(p.ok, 'boot', '; '.join(b.faults))
    check(b.regs.get(emu.UART + 0x10) == 0x1f000050, 'LPUART1 at 31250 baud like the original',
          hex(b.regs.get(emu.UART + 0x10, 0)))
    check(b.regs.get(emu.UART_CTRL, 0) & 0x2c0000 == 0x2c0000, 'receiver, transmitter, receive interrupt')
    check(emu.UART_IRQ in b.nvic and 101 in b.nvic, 'UART and clock timer interrupts enabled')

    # program change on the TRS input: factory settings (both ports, omni)
    p.trs(b'\xc0\x07')
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) == (7,), 'TRS program change 7 -> preset 7')
    check(bytes(b.uart_tx) == b'\xc0\x07', 'thru on the TRS output', bytes(b.uart_tx).hex())
    check(b'\xc0\x07' in p.events, 'thru on USB', str(p.events))
    p.trs(b'\xb0\x00\x01\xc0\x03')
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) == (123,), 'bank 1, program 3 -> preset 123')
    p.trs(b'\xb0\x00\x00\xc0\x07')
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) == (7,), 'bank 0 again, preset 7 (factory, with blocks)')
    p.trs(b'\xb0\x07\x30')
    check(p.notified(CMD_PRESET_SUB, SUB_VOLUME) == (48,), 'CC 7 -> preset volume 48')
    p.trs(b'\xb0\x2b\x7f')
    p.trs(b'\xb0\x2b\x00')
    sw = [k for k in p.rx if k[0] == CMD_PRESET_SUB and k[1] == SUB_SWITCH]
    check(sw and p.rx[sw[-1]][1] == 0, 'CC 43 = 0 -> first block off', str(sw))

    # the input channel, set through the editor protocol
    p.set_global(0x6100, 2)
    check(p.notified(CMD_GLOBAL_SUB, 0x6100) == (2,), 'TRS input channel 2 (editor)')
    p.trs(b'\xc0\x05')
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) is None, 'channel 1 ignored')
    p.trs(b'\xc1\x05')
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) == (5,), 'channel 2 -> preset 5')
    b.uart_tx.clear()
    p.events.clear()
    p.usb_send(b'\xc0\x09')
    p.wait(200)
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) == (9,), 'USB program change (omni) -> preset 9')
    check(bytes(b.uart_tx) == b'\xc0\x09' and b'\xc0\x09' in p.events, 'USB input thru on TRS and back on USB')
    p.set_global(0x6200, 0)
    p.usb_send(b'\xc0\x0a')
    p.wait(200)
    check(p.notified(CMD_PRESET_SUB, SUB_INDEX) is None, 'MIDI source TRS only: USB ignored')
    p.set_global(0x6200, 2)
    p.set_global(0x6100, 0)

    # clock out on both ports
    p.set_global(0x6400, 3)
    b.uart_tx.clear()
    p.events.clear()
    gaps = [p.clock_interval() for _ in range(3)]
    p.wait(5)
    bpm = round(2500000 * 24 * 3 / sum(gaps))
    check(40 <= bpm <= 300 and max(gaps) - min(gaps) <= 24, 'clock timer spacing at a tempo', str(gaps))
    check(b.uart_tx.count(0xf8) == 3, 'three clocks on TRS', bytes(b.uart_tx).hex())
    check(p.events.count(b'\xf8') == 3, 'three clocks on USB', str(p.events))

    # clock in: TRS clock source, 100 BPM (a clock every 25 ms)
    p.set_global(0x6300, 1)
    for _ in range(40):
        b.uart_in(b'\xf8')
        b.run_ms(25)
    p.wait(50)
    g = p.clock_interval()
    check(abs(g - 600000) <= 24, 'TRS clock at 100 BPM -> tempo 100 (clock out 25 ms)', str(g))
    p.trs(b'\xb0\x44\x01\xb0\x45\x10')        # 144 BPM by CC: refused while the clock source is external
    g = p.clock_interval()
    check(abs(g - 600000) <= 24, 'tempo CC refused with an external clock source', str(g))
    p.set_global(0x6300, 0)
    p.trs(b'\xb0\x44\x01\xb0\x45\x10')
    g = p.clock_interval()
    check(abs(g - 2500000 * 24 / 144) <= 48, 'CC 68 / 69 -> 144 BPM with the internal clock', str(g))
    for _ in range(40):                       # and the MIDI clock is not taken then
        b.uart_in(b'\xf8')
        b.run_ms(25)
    p.wait(50)
    g = p.clock_interval()
    check(abs(g - 2500000 * 24 / 144) <= 48, 'internal clock source: MIDI clock ignored', str(g))
    check(b.faults == [], 'no faults', '; '.join(b.faults))

    print('%d failed' % check.failed)
    sys.exit(1 if check.failed else 0)


if __name__ == '__main__':
    main()
