"""waveod's modulators (docs/modulators.md) in the ARM emulator: a modulator moving a parameter while the audio runs.

    python tools/check_mods.py out/QME-200_waveod.bin [--elf build/fw/waveod.elf]

The whole firmware runs in tools/emu.py, reached over USB like waveod Studio. A preset without other blocks gets
Stereo Util with its Gain linked to MOD 1 (-24 dB at the modulator's 0, 0 dB at its 1); the output level per
block, against the same preset with the modulator off, must follow the modulator:

  LFO square 5 Hz           0 dB for 100 ms, -24 dB for 100 ms, ...
  LFO square, 1/4 synced    the same at 120 BPM: 250 ms each
  ENV, sens 0               the tone at -14 dBFS: 0.77 of the way, -5.6 dB
  SEQ 4 steps at 10 Hz      0 / 25 / 50 / 100 %: -24, -18, -12, 0 dB for 100 ms each

and the modulators come back from the pedal as stored (get).
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_blocks as cb                                   # noqa: E402
from waveod_studio import controls, wvpreset                # noqa: E402

BLOCK_MS = 64 / 44.1
failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def preset_with(base, mod):
    """`base` (no blocks) + Stereo Util in slot 0, its Gain on MOD 1 from -24 to 0 dB, MOD 1 = mod (or none)"""
    pr = wvpreset.Preset.decode(cb.with_blocks(base, [({}, True)]))
    link = controls.Link(controls.MOD1, controls.ALL, controls.PRESS, controls.PARAM, 0, cb.K_GAIN, controls.LINEAR,
                         0, -24.0, 0.0)
    pr.set_controls(controls.Table([link]))
    mods = pr.mods()
    if mod:
        mods[0] = dict(wvpreset.mod_default(mod['type']), **mod)
    pr.set_mods(mods)
    pr.name = 'MOD'
    return pr.encode()


def levels(left, blocks):
    """dB per block (RMS)"""
    out = []
    for b in range(blocks):
        xs = left[64 * b:64 * b + 64]
        e = math.sqrt(sum(x * x for x in xs) / 64)
        out.append(20 * math.log10(e) if e > 0 else -240.0)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(cb.emu.ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    cb.TONE_BLOCKS = 600                                     # 870 ms
    w = cb.ce.Waveod(a.fw, a.elf)
    p = cb.Pedal(transport=cb.Transport(w), timeout=60)
    play = cb.Player(w)
    _, data = p.get(0)
    base = cb.empty(data)
    p.put(cb.EMPTY, base)

    cases = [
        ('LFO square 5 Hz', dict(type='lfo', shape='square', rate=5.0),
         lambda t: 0.0 if (t % 200) < 100 else -24.0),
        ('LFO square 1/4 synced', dict(type='lfo', shape='square', sync=5),
         lambda t: 0.0 if (t % 500) < 250 else -24.0),
        ('ENV sens 0', dict(type='env', sens=0, attack=10, release=200), None),
        ('SEQ 4 steps 10 Hz', dict(type='seq', steps=4, rate=10.0, step=[0, 25, 50, 100] + [0] * 12),
         lambda t: (-24.0, -18.0, -12.0, 0.0)[int(t // 100) % 4]),
    ]
    p.put(21, preset_with(base, None))
    for i, (_, mod, _) in enumerate(cases):
        p.put(22 + i, preset_with(base, mod))

    ref_l, _, ref_cost = play.run(p, 21, count=True)
    ref = levels(ref_l, cb.TONE_BLOCKS)
    # blocks 25 apart see the tone (200 samples a period) at the same place
    check(abs(ref[599] - ref[299]) < 0.01, 'modulator off: steady, %.2f dB per block' % ref[599])
    tone_db = 20 * math.log10(0.2)
    for i, (name, mod, want) in enumerate(cases):
        l, _, cost = play.run(p, 22 + i, count=True)
        got = levels(l, cb.TONE_BLOCKS)
        rel = [g - r for g, r in zip(got, ref)]
        if want is None:                                     # ENV: a steady tone, a steady value
            v = (tone_db + 60.0) / 60.0
            expect = -24.0 + 24.0 * v
            settled = rel[200:]
            err = max(abs(x - expect) for x in settled)
            check(err < 0.3, '%-22s %.2f dB (want %.2f, the tone at %.1f dBFS), off by %.2f dB; %d instructions (%+d)'
                  % (name, sum(settled) / len(settled), expect, tone_db, err, cost, cost - ref_cost))
            continue
        # The modulator's clock starts at the load, before the warm-up, so where it is when the tone starts is not
        # known: every offset is tried and the best kept. The block a value changes in, and the one after (Stereo
        # Util moves over a block), are left out.
        best = None
        for off in range(0, 1000):
            wrong = counted = 0
            for b, x in enumerate(rel):
                t0, t1 = b * BLOCK_MS + off, (b + 1) * BLOCK_MS + off
                if want(t0) != want(t1 - 0.01) or want(t0 - BLOCK_MS) != want(t0):
                    continue
                counted += 1
                wrong += abs(x - want(t0)) > 0.1
            if best is None or wrong < best[0]:
                best = (wrong, counted, off)
        check(best[0] == 0, '%-22s %d blocks away from a change all at the expected level (%d wrong; offset %d ms); '
              '%d instructions (%+d)' % (name, best[1], best[0], best[2], cost, cost - ref_cost))

    _, back = p.get(22 + 3)
    m = wvpreset.Preset.decode(back).mods()[0]
    check(m['type'] == 'seq' and m['steps'] == 4 and m['step'][:4] == [0, 25, 50, 100] and m['rate'] == 10.0,
          'the pedal keeps the modulators: %s' % {k: m[k] for k in ('type', 'steps', 'rate')})
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
