"""waveod's circuit blocks (docs/drives.md) as modules in the whole firmware, in the ARM emulator.

    python tools/check_drives_fw.py out/QME-200_waveod.bin [--elf build/fw/waveod.elf]

The six blocks are built (tools/wvfx.py build), packed into a library (out/drives.bin) and installed over USB as
waveod Studio does; then, for each, a preset without other blocks plus that block (its defaults) plays the tone:
  - it loads and runs (not AUSENTE, no fault), its output finite and not silent, its cost per block;
  - Dark Muff Lite with its Volume on MOD 1 (an LFO square at 2 Hz, Volume 20 / 90): the level follows the
    modulator, so poll rebuilt the matrices in the firmware's main loop and the audio took the new set;
  - preset 01A with Dark Muff added: the cost on a full chain.
"""
import argparse
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_blocks as cb                                   # noqa: E402
from waveod_studio import controls, wvlib, wvpreset         # noqa: E402

ROOT = os.path.dirname(HERE)
BLOCKS = ['dark_muff', 'dark_muff_lite', 'green_drive', 'rodent_dist', 'round_fuzz', 'treble_boost']
MENU_DRV = 3
failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def with_drive(data, mod, knobs=None):
    """preset `data` with module `mod` (wvlib.describe) added at the end of its chain, its knobs at the defaults"""
    pr = wvpreset.Preset.decode(data)
    used = {s.slot for s in pr.slots() if s.menu >= 0}
    slot = next(i for i in range(wvpreset.SLOTS) if i not in used)
    k = [0.0] * wvpreset.KNOBS
    for p in mod.params:
        k[p['knob']] = p['default']
    for i, v in (knobs or {}).items():
        k[i] = float(v)
    body = struct.pack('<BBbhIB', slot, 1, MENU_DRV, 0, mod.id, wvpreset.KNOBS) + struct.pack('<15f', *k)
    pr.chunks = [(t, b) for t, b in pr.chunks if not (t == wvpreset.SLOT and b[0] == slot)]
    pr.chunks.append((wvpreset.SLOT, body))
    main = bytearray(pr.chunk(wvpreset.MAIN))
    count = main[6]
    main[7 + count] = slot
    main[6] = count + 1
    pr.set_chunk(wvpreset.MAIN, main)
    pr.name = mod.name.upper()[:12]
    return pr.encode(), slot


def levels(x, blocks):
    out = []
    for b in range(blocks):
        xs = x[64 * b:64 * b + 64]
        e = math.sqrt(sum(v * v for v in xs) / 64)
        out.append(20 * math.log10(e) if e > 0 else -240.0)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    elfs = []
    for b in BLOCKS:
        subprocess.run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'build', os.path.join(ROOT, 'src', 'blocks', b + '.c')],
                       check=True, capture_output=True)
        elfs.append(os.path.join(ROOT, 'out', 'fx', b + '.elf'))
    lib = os.path.join(ROOT, 'out', 'drives.bin')
    subprocess.run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'lib', lib] + elfs, check=True, capture_output=True)
    img = open(lib, 'rb').read()
    mods = [wvlib.describe(open(e, 'rb').read()) for e in elfs]

    cb.TONE_BLOCKS = 60
    w = cb.ce.Waveod(a.fw, a.elf)
    p = cb.Pedal(transport=cb.Transport(w), timeout=120)
    play = cb.Player(w)
    _, data = p.get(0)
    base = cb.empty(data)
    p.put(cb.EMPTY, base)
    p.write_library(img)
    got = p.library()
    check([m.id for m in got] == [m.id for m in mods] and got.pack() == img,
          'installed over USB: %d modules, %d bytes, read back byte for byte' % (len(got), len(img)))

    ref_l, _, ref_cost = play.run(p, cb.EMPTY, count=True)
    for i, m in enumerate(mods):
        pre, _ = with_drive(base, m)
        p.put(30 + i, pre)
        _, blocks = p.describe(30 + i)
        l, r, cost = play.run(p, 30 + i, count=True)
        rms = math.sqrt(sum(v * v for v in l) / len(l))
        finite = all(math.isfinite(v) for v in l + r)
        check([b.name for b in blocks] == [m.name] and finite and rms > 1e-4,
              '%-15s runs: %s, output %.1f dBFS rms; %d instructions per block (%+d)' % (
                  m.name, [b.name for b in blocks], 20 * math.log10(rms + 1e-30), cost, cost - ref_cost))

    lite = mods[BLOCKS.index('dark_muff_lite')]
    vol = next(p_['knob'] for p_ in lite.params if p_['name'] == 'Volume')
    pre, slot = with_drive(base, lite)
    pr = wvpreset.Preset.decode(pre)
    link = controls.Link(controls.MOD1, controls.ALL, controls.PRESS, controls.PARAM, slot, vol, controls.LINEAR,
                         0, 20.0, 90.0)
    pr.set_controls(controls.Table([link]))
    mm = pr.mods()
    mm[0] = dict(wvpreset.mod_default('lfo'), type='lfo', shape='square', rate=2.0)
    pr.set_mods(mm)
    p.put(40, pr.encode())
    cb.TONE_BLOCKS = 700                                    # 1 s: two periods
    l, _, _ = play.run(p, 40)
    lv = levels(l, cb.TONE_BLOCKS)
    lo, hi = min(lv[50:]), max(lv[50:])
    high = sum(1 for x in lv[50:] if x > (lo + hi) / 2)
    check(hi - lo > 10 and 200 < high < 450,
          'Dark Muff Lite, Volume on MOD 1 (square 2 Hz, 20 / 90): the level moves %.1f dB, high in %d of %d blocks'
          % (hi - lo, high, len(lv) - 50))
    cb.TONE_BLOCKS = 60

    full, _ = with_drive(data, mods[0])
    p.put(41, full)
    p.put(42, data)
    _, _, plain = play.run(p, 42, count=True)
    _, _, more = play.run(p, 41, count=True)
    check(more > plain, '01A: %d instructions per block, with Dark Muff %d (%+d)' % (plain, more, more - plain))
    if w.b.faults:
        check(False, 'faults: ' + '; '.join(w.b.faults))
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
