"""waveod's own effect blocks (docs/blocks.md) in the ARM emulator: a built-in running in the chain.

    python tools/check_blocks.py out/QME-200_waveod.bin [--elf build/fw/waveod.elf]

The whole firmware runs in tools/emu.py, reached through its USB MIDI port like waveod Studio. Presets are
stored with chains of Stereo Util only, loaded, and fed the same tone; the reference is the preset without
blocks (the input and output filters, the levels). Stereo Util is linear, so the output must be the
reference's through its matrix:

  gain -6 dB, polarity Invert L          L = -g x ref L, R = g x ref R      g = 10^(-6/20)
  balance R50, then a second one: Swap   L = ref R, R = 0.5 x ref L         (two instances, each with its state)
  the block off                          the reference

(Preset 01A cannot be the reference: some of its stock modules keep state across loads, so it never plays the
same twice.) 01A with Stereo Util added gives the block's cost. describe must name the block and its parameters.
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'studio'))
import check_editor as ce                                   # noqa: E402
import emu                                                  # noqa: E402
from waveod_studio.usb import htmidi                        # noqa: E402
from waveod_studio import wvlib, wvpreset                   # noqa: E402
from waveod_studio.protocol import EDITED, Pedal            # noqa: E402

ce.htmidi = htmidi
MENU_VOL = 15
STEREO_UTIL = 0x570F0001
K_GAIN, K_BALANCE, K_WIDTH, K_POLARITY, K_CHANNELS = range(5)
FREQ = 44100 / 200                  # a period of 200 samples: blocks 50 apart start the tone at the same place
WARM_BLOCKS = 40                    # after the load: the fade in and the chain settle
TONE_BLOCKS = 60
EMPTY = 20                          # where the preset without blocks is stored
WV_LIBRARY = 0x2A0000               # fw/flash_map.h FLASH_WV_LIBRARY
MISSING = 0x570F01FF                # an id of waveod's that nothing has

failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


class Transport:
    def __init__(self, w):
        self.w, self.stream = w, bytearray()

    def send(self, sysex):
        self.w.send(sysex)

    def receive(self):
        self.w.b.run_ms(5)
        while True:
            d = self.w.u.transfer_in(0x83)
            if d is None:
                break
            for i in range(0, len(d) - 3, 4):
                self.stream += d[i + 1:i + 1 + ce.CIN_LEN[d[i] & 0x0f]]
        out = []
        while 0xF7 in self.stream:
            e = self.stream.index(0xF7)
            s = self.stream.rfind(0xF0, 0, e)
            if s >= 0:
                out.append(bytes(self.stream[s:e + 1]))
            del self.stream[:e + 1]
        return out

    def close(self):
        pass


def empty(data):
    """preset `data` without blocks, and without its links (01A's EXP 1A moves knob 0 of slot 1: with the pedal
    at the top it would turn a Stereo Util there to +12 dB)"""
    pr = wvpreset.Preset.decode(data)
    drop = (wvpreset.SLOT, wvpreset.SCENES, wvpreset.CONTROLS, wvpreset.QK, wvpreset.EXP, wvpreset.CTRL)
    pr.chunks = [(t, b) for t, b in pr.chunks if t not in drop]
    main = bytearray(pr.chunk(wvpreset.MAIN))
    main[6] = 0
    main[7:7 + wvpreset.SLOTS] = b'\xff' * wvpreset.SLOTS
    pr.set_chunk(wvpreset.MAIN, main)
    pr.name = 'VAZIO'
    return pr.encode()


def with_blocks(data, blocks):
    """preset `data` with Stereo Utils added at the end of its chain: blocks = [(knobs {index: value}, on[, id])]"""
    pr = wvpreset.Preset.decode(data)
    for blk in blocks:
        knobs, on = blk[:2]
        id_ = blk[2] if len(blk) > 2 else STEREO_UTIL
        used = {s.slot for s in pr.slots() if s.menu >= 0}
        slot = next(i for i in range(wvpreset.SLOTS) if i not in used)
        k = [0.0] * wvpreset.KNOBS
        k[K_WIDTH] = 100.0
        for i, v in knobs.items():
            k[i] = float(v)
        body = struct.pack('<BBbhIB', slot, int(on), MENU_VOL, 0, id_, wvpreset.KNOBS) + struct.pack('<15f', *k)
        pr.chunks = [(t, b) for t, b in pr.chunks if not (t == wvpreset.SLOT and b[0] == slot)]
        pr.chunks.append((wvpreset.SLOT, body))
        main = bytearray(pr.chunk(wvpreset.MAIN))
        count = main[6]
        main[7 + count] = slot
        main[6] = count + 1
        pr.set_chunk(wvpreset.MAIN, main)
    pr.name = 'BLOCOS'
    return pr.encode()


class Player:
    def __init__(self, w):
        self.w = w

    def run(self, p, index, count=False):
        """load preset `index` (after the empty one, which cuts any tail ringing on), let it settle, then the
        tone: (left, right, median instructions per block)"""
        p.load(EMPTY)
        p.load(index)
        return self.now(count)

    def now(self, count=False):
        """what plays now, after a settling silence"""
        b = self.w.b
        base = ((self.w.phase + WARM_BLOCKS) // 50 + 1) * 50
        for ph in range(self.w.phase, base):                # silence while it loads and fades in
            b.audio_block(ph, level=0.0)
            b.run_ms(1.4512)
        left, right, cost = [], [], []
        for i in range(TONE_BLOCKS):
            n, out = b.audio_block(base + i, freq=FREQ, level=0.2, count=count)
            cost.append(n)
            left += [v / 2 ** 31 for v in out[0::4]]
            right += [v / 2 ** 31 for v in out[2::4]]
            b.run_ms(1.4512)
        self.w.phase = base + TONE_BLOCKS
        if b.faults:
            sys.exit('FAULT: ' + '; '.join(b.faults))
        return left, right, sorted(cost)[len(cost) // 2] if count else 0


def diff_db(got, want):
    """the difference against the expected output, in dB of the expected output"""
    e = math.sqrt(sum((g - w) ** 2 for g, w in zip(got, want)) / len(want))
    s = math.sqrt(sum(w * w for w in want) / len(want))
    return 20 * math.log10(e / s) if e > 0 else -240.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(emu.ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--wvlib', help='a library (tools/wvfx.py lib) with Stereo Util as a module under --module-id: '
                                    'installed over USB as waveod Studio does, then removed')
    ap.add_argument('--preload', action='store_true', help='--wvlib in the flash from start-up instead')
    ap.add_argument('--module-id', type=lambda s: int(s, 0), default=0x570F0101)
    a = ap.parse_args()
    img = open(a.wvlib, 'rb').read() if a.wvlib else None
    w = ce.Waveod(a.fw, a.elf, {WV_LIBRARY: img} if img and a.preload else None)
    p = Pedal(transport=Transport(w), timeout=60)
    play = Player(w)

    _, data = p.get(0)
    base = empty(data)
    p.put(EMPTY, base)
    g = 10 ** (-6 / 20)
    cases = [
        ('gain -6 dB, Invert L', [({K_GAIN: -6, K_POLARITY: 1}, True)],
         lambda l, r: ([-g * x for x in l], [g * x for x in r])),
        ('balance R50 + Swap', [({K_BALANCE: 50}, True), ({K_CHANNELS: 4}, True)],
         lambda l, r: (r, [0.5 * x for x in l])),
        ('the block off', [({K_GAIN: -6, K_POLARITY: 1}, False)], lambda l, r: (l, r)),
    ]
    for i, (_, blocks, _) in enumerate(cases):
        p.put(21 + i, with_blocks(base, blocks))
    p.put(25, with_blocks(data, [({K_GAIN: -6}, True)]))
    p.put(26, data)
    if a.wvlib:                                     # the same cases on the module: the built-in's output, bit for bit
        for i, (_, blocks, _) in enumerate(cases[:2]):
            p.put(31 + i, with_blocks(base, [b + (a.module_id,) for b in blocks]))
    p.put(33, with_blocks(base, [({K_GAIN: -6, K_POLARITY: 1}, True, MISSING)]))

    _, blocks = p.describe(25)
    util = next((b for b in blocks if b.name == 'Stereo Util'), None)
    check(util is not None and [x.name for x in util.params] == ['Gain', 'Balance', 'Width', 'Polarity', 'Channels'],
          'describe 01A + Stereo Util: %s' % (', '.join('%s (%s)' % (b.name, len(b.params)) for b in blocks)))

    ref_l, ref_r, ref_cost = play.run(p, EMPTY, count=True)
    again_l, again_r, _ = play.run(p, EMPTY)
    level = max(abs(x) for x in ref_l + ref_r)
    check(level > 0.01 and diff_db(again_l, ref_l) < -200 and diff_db(again_r, ref_r) < -200,
          'reference (no blocks): peak %.3f, the same when played again; %d instructions per block' % (level, ref_cost))
    builtin = []
    for i, (name, _, expect) in enumerate(cases):
        l, r, cost = play.run(p, 21 + i, count=True)
        builtin.append((l, r, cost))
        want_l, want_r = expect(ref_l, ref_r)
        dl, dr = diff_db(l, want_l), diff_db(r, want_r)
        check(dl < -100 and dr < -100, '%-22s L %7.1f dB  R %7.1f dB from the expected output; %d instructions (%+d)'
              % (name, dl, dr, cost, cost - ref_cost))
    if a.wvlib and not a.preload:                   # installed while preset 31 plays: it starts without a reload
        p.load(EMPTY)
        p.load(31)
        _, blocks = p.describe(EDITED)
        check([b.name for b in blocks] == ['AUSENTE'], 'before installing: %s' % [b.name for b in blocks])
        p.write_library(img)
        lib = p.library()
        check([(m.id, m.name) for m in lib] == [(a.module_id, 'Stereo Util M')] and lib.pack() == img,
              'installed over USB: %s, read back byte for byte' % list(lib))
        l, r, _ = play.now()
        check(l == builtin[0][0] and r == builtin[0][1], 'the edited preset plays the module without a reload')
    if a.wvlib:
        _, blocks = p.describe(31)
        check([b.name for b in blocks] == ['Stereo Util M'] and len(blocks[0].params) == 5,
              'describe, the module: %s' % ', '.join('%s (%d)' % (b.name, len(b.params)) for b in blocks))
        for i, (name, _, _) in enumerate(cases[:2]):
            l, r, cost = play.run(p, 31 + i, count=True)
            bl, br, bcost = builtin[i]
            check(l == bl and r == br, 'module: %-14s the built-in\'s output %s; %d instructions (built-in %d)'
                  % (name, 'bit for bit' if l == bl and r == br else 'DIFFERS (%.1f dB)' % diff_db(l, bl), cost, bcost))
    if a.wvlib and not a.preload:                   # removed: missing again, kept in the preset
        p.write_library(wvlib.Library().pack())
        _, blocks = p.describe(31)
        _, kept = p.get(31)
        l, r, _ = play.run(p, 31)
        check([b.name for b in blocks] == ['AUSENTE'] and len(p.library()) == 0 and
              any(s.id == a.module_id for s in wvpreset.Preset.decode(kept).slots()) and l == ref_l and r == ref_r,
              'removed (an empty library): %s, the preset keeps it, the signal passes' % [b.name for b in blocks])
    _, kept = p.get(33)
    slot = next((s for s in wvpreset.Preset.decode(kept).slots() if s.id == MISSING), None)
    _, blocks = p.describe(33)
    l, r, _ = play.run(p, 33)
    check(slot is not None and slot.knobs[K_GAIN] == -6 and l == ref_l and r == ref_r and
          [b.name for b in blocks] == ['AUSENTE'],
          'not installed (%#x): kept with its knobs, shown as %s, the signal passes' % (MISSING, [b.name for b in blocks]))
    _, _, plain = play.run(p, 26, count=True)
    _, _, more = play.run(p, 25, count=True)
    check(more > plain, '01A: %d instructions per block, with Stereo Util %d (%+d)' % (plain, more, more - plain))
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
