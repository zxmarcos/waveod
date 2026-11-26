"""waveod's modulation blocks (docs/chorus.md, docs/modfx.md: Chorus, Flanger, Phaser, Rotary, Vibe, Tremolo) and\nthe Delay (docs/delay.md) as ARM modules, alone and in the whole firmware.

    python tools/check_modfx.py [blocks...] [--fw out/QME-200_waveod.bin [--elf build/fw/waveod.elf]]

1. Each block is built as a module (tools/wvfx.py build) and for the PC (tools/circuits/wvfx_host.c); at each
   setting both run 0.5 s of the guitar DI (out/circuits/di.f32; a pluck when there is none), the right channel
   a different take than the left. Pass: the module's output is the PC build's bit for bit. Reported: the
   instructions per block (tools/wvfx_emu.py --count).
2. With --fw: the modules are packed into a library, installed over USB in the emulated firmware as waveod
   Studio does, and each plays a tone in a preset of its own (it loads, not AUSENTE, no fault, finite, not
   silent, its cost over the empty preset), then once more after preset 01A's chain.

The sound against the reference implementations is checked outside this repository.
"""
import argparse
import math
import os
import re
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'out', 'modfx')
FS = 44100
MENU_MOD = 8
sys.path.insert(0, HERE)
import wvbuild                                                      # noqa: E402

# knobs in parameter order; the settings each block is held to
CHORUS = lambda typ, wave=0, rate=1.0, depth=50.0, delay=10.0, mix=50.0, phase=90.0, hicut=20000.0, sync=0: \
    [rate, depth, delay, mix, phase, wave, hicut, 0.0, sync, typ]
FLANGER = lambda typ, wave=0, rate=0.5, depth=70.0, fb=60.0, delay=1.0, width=10.0, mix=50.0, hicut=20000.0, sync=0: \
    [rate, depth, fb, delay, width, mix, 90.0, wave, hicut, 0.0, sync, typ]
PHASER = lambda typ, st=4, wave=1, rate=1.0, fb=40.0, lo=200.0, hi=2000.0, sync=0: \
    [rate, 80.0, fb, st, lo, hi, 50.0, 90.0, wave, 0.0, sync, typ]
ROTARY = lambda rate=6.5, low=0.9, drive=0.05, spread=50.0, mix=100.0: \
    [rate, low, 25.0, 25.0, 0.0, 90.0, drive, spread, mix, 0.0]
VIBE = lambda typ, rate=1.0, fb=0.0, wave=0, phase=0.0, sync=0: [rate, 50.0, fb, 53.0, phase, wave, 0.0, sync, typ]
TREMOLO = lambda wave=1, rate=5.0, phase=0.0, mode=0, sync=0, bal=0.0: [rate, 80.0, wave, phase, 0.0, bal, sync, mode]
DELAY = lambda typ, time=400.0, fb=30.0, mix=30.0, mr=0.5, md=0.0, sync=0: [typ, time, fb, mix, 0.0, 20.0, 20000.0, mr, md, sync]
REVERB = lambda typ, tm=3.2, q=0, pd=0.0, mix=30.0, spill=1: [typ, tm, pd, mix, 0.0, 3950.0, 70.0, q, spill]
PLATE = lambda typ, tm=1.5, q=0, pd=0.0, mix=30.0, spill=1: [typ, tm, pd, mix, 0.0, 5200.0, 20.0, q, spill]
SPRING = lambda typ, tm=2.0, pd=0.0, mix=30.0, lc=400.0, spill=1: [typ, tm, pd, mix, 0.0, 3000.0, lc, spill]
BLOCKS = {
    'chorus': [('Digital Stereo', CHORUS(1)), ('Analog Stereo', CHORUS(3)), ('Compact', CHORUS(4, hicut=6000.0)),
               ('Octet', CHORUS(8)), ('Space 3', CHORUS(11)), ('Analog Octet', CHORUS(13)),
               ('Small Analog', CHORUS(19)), ('Stereo Rack, triangle', CHORUS(22, wave=1)),
               ('Digital Stereo synced 1/8', CHORUS(1, rate=7.0, sync=1))],
    'flanger': [('Digital Stereo', FLANGER(1)), ('Analog Stereo, High Cut 4k', FLANGER(3, hicut=4000.0)),
                ('Thru-Zero', FLANGER(4)), ('Vintage Box (clip, exp)', FLANGER(8)), ('Compact BBD', FLANGER(9)),
                ('Electric (output low cut)', FLANGER(10)), ('Step', FLANGER(14)), ('Square', FLANGER(16)),
                ('Exp Sweep', FLANGER(22)), ('Wide Trip (spread)', FLANGER(28)), ('synced 1/4, fb -90', FLANGER(1, rate=4.0, sync=1, fb=-90.0))],
    'phaser': [('Digital Stereo 4', PHASER(1)), ('Small 90 (astable, JFET)', PHASER(3)), ('Fed 90 8', PHASER(4, st=8)),
               ('Lamp (photocell)', PHASER(8, wave=0)), ('Saw Sweep 12', PHASER(9, st=12)), ('Big Sweep 2, fb 90', PHASER(11, st=2, fb=90.0)),
               ('synced 1/8', PHASER(1, rate=7.0, sync=1))],
    'rotary': [('defaults', ROTARY()), ('slow', ROTARY(rate=0.8)), ('driven', ROTARY(drive=20.0)),
               ('stopped, spread -200', ROTARY(rate=0.0, spread=-200.0)), ('mix 50', ROTARY(mix=50.0))],
    'vibe': [('Lamp', VIBE(0)), ('Exp', VIBE(1)), ('Bright Lamp, fb 60, phase 90', VIBE(2, fb=60.0, phase=90.0)),
             ('Lamp triangle fast', VIBE(0, rate=6.0, wave=1)), ('synced 1/4', VIBE(0, rate=4.0, sync=1))],
    'tremolo': [('triangle', TREMOLO()), ('sine, phase 90', TREMOLO(wave=0, phase=90.0)), ('square', TREMOLO(wave=2)),
                ('panner sine', TREMOLO(wave=0, mode=1)), ('synced 1/8, balance -50', TREMOLO(rate=7.0, sync=1, bal=-50.0))],
    'delay': [('Digital Mono', DELAY(0)), ('Digital Stereo', DELAY(1)), ('Analog Stereo (compander)', DELAY(3)),
              ('Tape Wide (LFO 2)', DELAY(4)), ('Ping-Pong', DELAY(5)), ('Sweep', DELAY(7)), ('Ducking', DELAY(8)),
              ('Early Digital (bits)', DELAY(9)), ('Ambient (diffuser)', DELAY(11)), ('Bucket Deluxe', DELAY(12)),
              ('Breeze (mod diffuser)', DELAY(19)), ('Trem Echo', DELAY(21)), ('Rotating', DELAY(23)),
              ('synced 1/4, mod', DELAY(1, time=4.0, sync=1, mr=2.0, md=40.0)), ('30 ms fb -90', DELAY(0, time=30.0, fb=-90.0))],
    'reverb': [('Hall Large Economy', REVERB(5)), ('Hall Large Normal', REVERB(5, q=1)), ('Hall Large High', REVERB(5, q=2)),
               ('Hall Large Ultra', REVERB(5, q=3)), ('Room Small 0.65 s', REVERB(0, tm=0.65, q=1)),
               ('Cloud A 24 s', REVERB(38, tm=24.0, q=1)), ('Echo Hall predelay 120', REVERB(44, tm=3.9, pd=120.0)),
               ('Ambience no spillover', REVERB(15, tm=0.8, spill=0))],
    'plate': [('Tight Plate Economy', PLATE(0)), ('Mid Plate Normal', PLATE(1, q=1)), ('Big Plate High', PLATE(2, q=2)),
              ('Singer Plate Ultra', PLATE(5, tm=2.6, q=3)), ('Warm Plate (EQ)', PLATE(4, tm=3.0)),
              ('Slap Plate predelay 120', PLATE(6, pd=120.0)), ('Bright Plate no spillover', PLATE(7, spill=0))],
    'spring': [('Short Spring', SPRING(0)), ('Long Spring (3 springs)', SPRING(2)), ('Valve Tank (tone circuit)', SPRING(4, lc=128.0)),
               ('Rack Spring (6 pairs)', SPRING(5, tm=2.5, lc=350.0)), ('Classic Tank (3 pairs)', SPRING(7, lc=33.0)),
               ('Wide Spring predelay 80', SPRING(8, pd=80.0))],
}
failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit('failed: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r.stdout


def di():
    p = os.path.join(ROOT, 'out', 'circuits', 'di.f32')
    if os.path.exists(p):
        x = np.fromfile(p, np.float32)[0::2]
        return x[int(3.5 * FS):int(4.0 * FS)], x[int(6.0 * FS):int(6.5 * FS)]
    t = np.arange(FS // 2) / FS
    l = sum(0.3 / h * np.exp(-t * (2 + h)) * np.sin(2 * np.pi * 110.0 * h * t) for h in range(1, 12))
    return l, np.roll(l, 3000)


def arm_vs_pc(blk):
    src = os.path.join(ROOT, 'src', 'blocks', blk + '.c')
    elf = os.path.join(ROOT, 'out', 'fx', blk + '.elf')
    run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'build', src, '-o', elf])
    exe = os.path.join(OUT, blk + '_host.exe')
    wvbuild.host_compile(exe, [os.path.join(HERE, 'circuits', 'wvfx_host.c')],
                         ['-I', os.path.join(ROOT, 'src'), '-DWVFX_SRC="../../src/blocks/%s.c"' % blk,
                          '-DWVFX_DESC=%s_block' % blk])
    l, r = di()
    fi = os.path.join(OUT, blk + '_in.f32')
    np.stack([l, r], 1).astype(np.float32).tofile(fi)
    for name, k in BLOCKS[blk]:
        fa, fb = os.path.join(OUT, blk + '_arm.f32'), os.path.join(OUT, blk + '_pc.f32')
        ks = ['%.9g' % v for v in k]
        run([exe, fi, fb] + ks)
        out = run([sys.executable, os.path.join(HERE, 'wvfx_emu.py'), elf, fi, fa] + ks + ['--count', '8'])
        ya, yb = np.fromfile(fa, np.float32), np.fromfile(fb, np.float32)
        same = np.array_equal(ya, yb)
        m = re.search(r'median (\d+)', out)
        check(same and np.all(np.isfinite(yb)), '%-8s %-26s ARM = PC %s; %s instructions per block' % (
            blk, name, 'bit for bit' if same else 'DIFFERS', m.group(1) if m else '?'))
    return elf


def with_block(data, mod, menu, knobs=None):
    """preset `data` with module `mod` (wvlib.describe) added at the end of its chain, its knobs at the defaults"""
    from waveod_studio import wvpreset
    pr = wvpreset.Preset.decode(data)
    used = {s.slot for s in pr.slots() if s.menu >= 0}
    slot = next(i for i in range(wvpreset.SLOTS) if i not in used)
    k = [0.0] * wvpreset.KNOBS
    for p in mod.params:
        k[p['knob']] = p['default']
    for i, v in (knobs or {}).items():
        k[i] = float(v)
    body = struct.pack('<BBbhIB', slot, 1, menu, 0, mod.id, wvpreset.KNOBS) + struct.pack('<15f', *k)
    pr.chunks = [(t, b) for t, b in pr.chunks if not (t == wvpreset.SLOT and b[0] == slot)]
    pr.chunks.append((wvpreset.SLOT, body))
    main = bytearray(pr.chunk(wvpreset.MAIN))
    count = main[6]
    main[7 + count] = slot
    main[6] = count + 1
    pr.set_chunk(wvpreset.MAIN, main)
    pr.name = mod.name.upper()[:12]
    return pr.encode()


def firmware(fw, elf_fw, elfs):
    sys.path.insert(0, HERE)
    import check_blocks as cb
    from waveod_studio import wvlib
    lib = os.path.join(OUT, 'modfx.bin')
    run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'lib', lib] + elfs)
    img = open(lib, 'rb').read()
    mods = [wvlib.describe(open(e, 'rb').read()) for e in elfs]
    cb.TONE_BLOCKS = 60
    w = cb.ce.Waveod(fw, elf_fw)
    p = cb.Pedal(transport=cb.Transport(w), timeout=120)
    play = cb.Player(w)
    _, data = p.get(0)
    base = cb.empty(data)
    p.put(cb.EMPTY, base)
    p.write_library(img)
    got = p.library()
    check([m.id for m in got] == [m.id for m in mods] and got.pack() == img,
          'installed over USB: %d modules, %d bytes, read back byte for byte' % (len(got), len(img)))
    _, _, ref_cost = play.run(p, cb.EMPTY, count=True)
    for i, m in enumerate(mods):
        p.put(30 + i, with_block(base, m, m.menu))
        _, blocks = p.describe(30 + i)
        l, r, cost = play.run(p, 30 + i, count=True)
        rms = math.sqrt(sum(v * v for v in l) / len(l))
        finite = all(math.isfinite(v) for v in l + r)
        check([b.name for b in blocks] == [m.name] and finite and rms > 1e-4,
              '%-8s runs: %s, output %.1f dBFS rms; %d instructions per block (%+d)' % (
                  m.name, [b.name for b in blocks], 20 * math.log10(rms + 1e-30), cost, cost - ref_cost))
        p.put(41, with_block(data, m, m.menu))
        p.put(42, data)
        _, _, plain = play.run(p, 42, count=True)
        l, r, more = play.run(p, 41, count=True)
        check(more > plain and all(math.isfinite(v) for v in l + r),
              '%-8s after 01A: %d instructions per block, with it %d (%+d)' % (m.name, plain, more, more - plain))
    if w.b.faults:
        check(False, 'faults: ' + '; '.join(w.b.faults))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('blocks', nargs='*')
    ap.add_argument('--fw')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    elfs = [arm_vs_pc(b) for b in a.blocks or BLOCKS]
    if a.fw:
        firmware(a.fw, a.elf, elfs)
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
