"""waveod's circuit blocks (docs/drives.md) against their circuits and on the ARM.

    python tools/check_drives.py [block ...] [--quick] [--no-arm]

For every block:
  1. the block built for the PC (tools/circuits/wvfx_host.c) and the whole circuit in the reference simulator
     (tools/circuits/circuit.py + refsim.c: every node, every device, double precision, Newton to 1e-9 V, at 8x the
     rate) are fed the same signal at a few knob settings: sines of 220 Hz at three levels and 1.5 s of a guitar DI
     (out/circuits/di.f32, or a synthetic pluck when there is none). The reference's output comes down to 44.1 kHz
     band-limited; the block's output is aligned on it (its oversampling filters' delay, found to 0.01 sample).
     Reported: the difference in dB of the reference (16 kHz band), and the harmonics 2..5 of the sines, dB re the
     fundamental, block / circuit;
  2. the module (tools/wvfx.py build) in the ARM emulator (tools/wvfx_emu.py) on 0.5 s of the same DI: its output
     against the PC build's, and the instructions per block.
Pass: the DI within -30 dB of the circuit at every setting, the harmonics within 3 dB where they are above -50 dB re
the fundamental, the fundamental within 1 dB, the module the PC build's output bit for bit. Dark Muff Lite, a
chosen simplification (no even harmonics), is held to -20 dB and its odd harmonics.
"""
import argparse
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, 'circuits'))
import drives                                               # noqa: E402
import bigmuff                                              # noqa: E402

sys.path.insert(0, HERE)
import wvbuild                                              # noqa: E402

OUT =os.path.join(ROOT, 'out', 'circuits')
FS = 44100
UP = 8
VIN = 2.5                                                   # circuits/drive.h DRIVE_VIN
LOG = lambda k: (81.0 ** k - 1.0) / 80.0                    # circuits/drive.h drive_log
LIN = lambda k: k


# what a block is held to: the DI's difference (dB), the harmonics compared (2..5) above loor dB within 	ol dB,
# the fundamental within h1 dB
STRICT = dict(di=-30.0, harmonics=(2, 3, 4, 5), floor=-50.0, tol=3.0, h1=1.0)
# Dark Muff Lite: Q4 and Q3 linearised on purpose (research/pulse NOTES.md 15): no even harmonics, the odd ones
# and the level kept
LITE = dict(di=-20.0, harmonics=(3, 5), floor=-50.0, tol=3.0, h1=2.0)


def muff_full(rot):
    return bigmuff.build('rams73', rot['sustain'], rot['tone'], rot['volume'])


#  block: (circuit (rotations -> Circuit), knobs [(name, taper)], settings to check (knob values 0..100; a value past
#  the circuit's knobs goes to the block's next parameter: Quality 1 = 4x, 0 = 2x))
BLOCKS = {
    'dark_muff': (muff_full, [('sustain', LIN), ('tone', LIN), ('volume', LIN)],
                  [(70, 40, 50), (100, 80, 50), (20, 20, 50)]),
    'dark_muff_lite': (muff_full, [('sustain', LIN), ('tone', LIN), ('volume', LIN)],
                       [(70, 40, 50), (100, 80, 50), (20, 20, 50)], LITE),
    'green_drive': (drives.green, [('drive', LOG), ('tone', LIN), ('level', LOG)],
                    [(50, 50, 70), (100, 80, 70), (0, 20, 70)]),
    'rodent_dist': (drives.rodent, [('distortion', LOG), ('filter', LOG), ('volume', LOG)],
                    [(50, 30, 70, 1), (100, 0, 50, 1), (20, 80, 70, 1), (50, 30, 70, 0)]),
    'round_fuzz': (drives.fuzz, [('fuzz', lambda k: 1.0 - k), ('volume', LOG)],
                   [(100, 70, 1), (50, 70, 1), (10, 70, 1), (100, 70, 0)]),
    'treble_boost': (drives.treble, [('boost', LOG)], [(70,), (100,), (30,)]),
}
LEVELS = (0.01, 0.05, 0.25)                                 # sine peaks, full scale = VIN volts

failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit('failed: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r.stdout


def host_exe(blk):
    exe = os.path.join(OUT, blk + '_host.exe')
    return wvbuild.host_compile(exe, [os.path.join(HERE, 'circuits', 'wvfx_host.c')],
                                ['-I', os.path.join(ROOT, 'src'), '-DWVFX_SRC="../../src/blocks/%s.c"' % blk,
                                 '-DWVFX_DESC=%s_block' % blk] + os.environ.get('HOST_CFLAGS', '').split())


def block_out(exe, x, knobs, tag):
    fi, fo = os.path.join(OUT, tag + '_in.f32'), os.path.join(OUT, tag + '_out.f32')
    np.repeat(x[:, None], 2, 1).astype(np.float32).tofile(fi)
    run([exe, fi, fo] + ['%g' % k for k in knobs])
    return np.fromfile(fo, np.float32)[0::2].astype(np.float64)


def band(x, n, lo=20.0, hi=16000.0):
    X = np.fft.rfft(x, n)
    f = np.fft.rfftfreq(n, 1 / FS)
    X[(f < lo) | (f > hi)] = 0
    return X


FADE_IN, FADE_OUT, PAD = 256, 1024, 8192


def prepared(x):
    """x faded in and out and followed by silence: the FFT resampling sees no jump where the signal wraps around"""
    x = np.array(x, np.float64)
    x[:FADE_IN] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(FADE_IN) / FADE_IN)
    x[-FADE_OUT:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(FADE_OUT) / FADE_OUT)
    return np.concatenate([x, np.zeros(PAD)])


def window(n_signal, skip):
    """the samples compared: after the settling, before the fade-out"""
    return slice(skip, n_signal - FADE_OUT)


def reference(circ, x):
    """the circuit's output for x (1x, full-scale units) at UP x the rate, back at 1x band-limited"""
    n = len(x)
    X = np.fft.rfft(x)
    xu = np.fft.irfft(X, n * UP) * UP * VIN                 # band-limited to the original band
    y = circ.tran(xu, FS * UP, 'out', tag='ref')[:, 0]
    Y = np.fft.rfft(y)[:n // 2 + 1]
    return np.fft.irfft(Y, n) / UP / VIN


def aligned_error(y, ref, w):
    """the block's output (the whole signal, faded and padded: shifting it wraps silence around) advanced by the
    delay that fits the reference best over the slice w, both band-limited: (difference in dB of the reference,
    the delay)"""
    n = len(ref)
    Y, R = band(y, n), band(ref, n)
    f = np.fft.rfftfreq(n, 1 / FS)
    r = np.fft.irfft(R, n)[w]
    s = np.sum(r * r)

    def err(d):
        a = np.fft.irfft(Y * np.exp(2j * np.pi * f * d / FS), n)[w]
        return np.sum((a - r) ** 2)
    best = min((err(d), d) for d in np.arange(0, 64, 0.5))
    best = min([best] + [(err(d), d) for d in np.arange(best[1] - 0.5, best[1] + 0.5, 0.02)])
    return 10 * np.log10(best[0] / max(s, 1e-30)), best[1]


def harmonics(y, f0, w):
    y = y[w]
    n = len(y) - len(y) % int(round(FS / f0 * 10))
    y = y[:n] * np.hanning(n)
    Y = np.abs(np.fft.rfft(y))
    k0 = f0 * n / FS
    h = np.array([Y[int(round(k0 * i))] for i in range(1, 6)])
    return 20 * np.log10(h[1:] / h[0] + 1e-12), 20 * np.log10(h[0] * 2 / np.sum(np.hanning(n)) + 1e-12)


def di():
    p = os.path.join(OUT, 'di.f32')
    if os.path.exists(p):
        x = np.fromfile(p, np.float32)[0::2].astype(np.float64)
        return x[int(3.5 * FS):int(5.0 * FS)]               # playing (it starts after 3.5 s of silence)
    t = np.arange(int(1.5 * FS)) / FS                       # plucked notes: decaying harmonics
    x = np.zeros_like(t)
    for i, (f0, t0) in enumerate(((110.0, 0.0), (164.8, 0.5), (220.0, 1.0))):
        tt = np.clip(t - t0, 0, None)
        for h in range(1, 12):
            x += (t >= t0) * 0.3 / h * np.exp(-tt * (2 + h)) * np.sin(2 * np.pi * f0 * h * tt)
    return x


def check_block(blk, quick, arm):
    circ_of, knobs, settings = BLOCKS[blk][:3]
    rule = BLOCKS[blk][3] if len(BLOCKS[blk]) > 3 else STRICT
    exe = host_exe(blk)
    x_di = di()
    if quick:
        settings = settings[:1]
    skip = 4096
    for s in settings:
        rot = {name: taper(v / 100.0) for (name, taper), v in zip(knobs, s)}
        circ = circ_of(rot)
        circ.dc()
        label = '%-14s %s' % (blk, '/'.join('%d' % v for v in s))
        x = prepared(x_di)
        w = window(len(x_di), skip)
        y = block_out(exe, x, s, blk)
        ref = reference(circ, x)
        e, d = aligned_error(y, ref, w)
        rms = 20 * np.log10(np.sqrt(np.mean(ref[w] ** 2)) + 1e-30)
        check(e < rule['di'], '%s DI      %6.1f dB from the circuit (delay %.2f, output %.1f dBFS rms)' % (label, e, d, rms))
        for lv in ((LEVELS[1],) if quick else LEVELS):
            n = skip + 8192 + FADE_OUT
            x = prepared(lv * np.sin(2 * np.pi * 220.0 * np.arange(n) / FS))
            w = window(n, skip)
            y = block_out(exe, x, s, blk)
            ref = reference(circ, x)
            hb, fb = harmonics(y, 220.0, w)
            hr, fr = harmonics(ref, 220.0, w)
            ok = all(abs(hb[k - 2] - hr[k - 2]) < rule['tol'] for k in rule['harmonics'] if hr[k - 2] > rule['floor']) \
                and abs(fb - fr) < rule['h1']
            check(ok, '%s %4.2f FS  H1 %+6.1f/%+6.1f dB  H2..5 %s' % (
                label, lv, fb, fr, ' '.join('%6.1f/%6.1f' % (a, b) for a, b in zip(hb, hr))))
    if not arm:
        return
    run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'build', os.path.join(ROOT, 'src', 'blocks', blk + '.c')])
    x = x_di[:int(0.5 * FS)]
    fi, fa, fb = (os.path.join(OUT, blk + t) for t in ('_arm_in.f32', '_arm.f32', '_pc.f32'))
    np.repeat(x[:, None], 2, 1).astype(np.float32).tofile(fi)
    s = settings[0]
    run([exe, fi, fb] + ['%g' % k for k in s])
    out = run([sys.executable, os.path.join(HERE, 'wvfx_emu.py'), os.path.join(ROOT, 'out', 'fx', blk + '.elf'), fi, fa]
              + ['%g' % k for k in s] + ['--count', '4'])
    ya, yb = np.fromfile(fa, np.float32), np.fromfile(fb, np.float32)
    check(np.array_equal(ya, yb), '%-14s ARM module: %s; %s' % (blk, 'the PC build\'s output bit for bit' if np.array_equal(ya, yb)
                                                                 else 'DIFFERS', out.strip()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('blocks', nargs='*')
    ap.add_argument('--quick', action='store_true', help='one setting, one sine level')
    ap.add_argument('--no-arm', action='store_true')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for blk in a.blocks or BLOCKS:
        check_block(blk, a.quick, not a.no_arm)
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
