"""waveod's Cab / IR engine (src/engine/cabir.c) against the original's (library entry 5.0 / 6.0, CabIrProcess).

    python tools/check_cab.py STOCK_FW.bin [--seconds 0.6] [--only N]

Every factory cab (and one User IR slot, filled with a cab's data) goes through both engines on the same
blocks: a guitar-like signal, the knobs moving on the way (VOLUME up and back, LOW CUT and HIGH CUT to other
values). The original runs in Unicorn (matribox's module emulator, tools/check_block.py's Runner); waveod's
through the unit tests' program, `waveod-tests --cab-apply OPS OUT` (tools/wvbuild.py). Reported: the difference under the signal, in dB, over the
whole run, and above 30 Hz: float rounding differs (FFT order), and the 19 Hz low cut turns that into a slow drift
under 30 Hz (about -75 dB); the audible part is judged against --min-db. All 61 factory cabs and a User IR: 82 to
88 dB above 30 Hz.
Needs matribox-studio (pip install -e), unicorn and the host build (tools/wvbuild.py).
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_block as B
import wvbuild

from matribox.emulator import chain as C
from matribox.firmware import fwtool
from matribox.usb import qmepreset as Q

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOB_VOL, KNOB_LOW, KNOB_HIGH = 1, 5, 6


def schedule(blocks):
    """knobs per block: defaults, then volume up, the cuts moved, volume back"""
    out = []
    for b in range(blocks):
        k = [0.0] * 15
        k[KNOB_VOL], k[KNOB_LOW], k[KNOB_HIGH] = 50.0, 19.0, 20001.0
        f = b / blocks
        if f >= 0.3:
            k[KNOB_VOL] = 85.0
        if f >= 0.5:
            k[KNOB_LOW], k[KNOB_HIGH] = 160.0, 6500.0
        if f >= 0.75:
            k[KNOB_VOL], k[KNOB_LOW], k[KNOB_HIGH] = 30.0, 40.0, 12000.0
        out.append(k)
    return out


def stock(resolver, menu, eid, x, knobs):
    r = B.Runner(resolver, menu, eid, knobs[0])
    out = []
    for b, k in enumerate(knobs):
        r.uc.mem_write(r.K, struct.pack('<15f', *k))
        y, _ = r.process(x[64 * b:64 * (b + 1)], 64)
        out.append(y)
    return np.concatenate(out)


def native(ir, x, knobs):
    d = tempfile.mkdtemp()
    ops, res = os.path.join(d, 'ops.bin'), os.path.join(d, 'out.bin')
    with open(ops, 'wb') as f:
        f.write(struct.pack('<I', len(ir) // 4) + ir)
        f.write(struct.pack('<II15f', 0, 1, *knobs[0]))
        for b, k in enumerate(knobs):
            f.write(struct.pack('<II15f', 64, 1, *k))
            f.write(np.ascontiguousarray(x[64 * b:64 * (b + 1)], np.float32).tobytes())
    wvbuild.tests(['--cab-apply', ops, res], check=True, stdout=subprocess.DEVNULL)
    return np.fromfile(res, np.float32).reshape(-1, 2)


def db(ref, got, above=0.0):
    """the difference under the signal (dB); above: only the difference's content above that frequency"""
    r, g = ref.astype(np.float64), got.astype(np.float64)
    if above:
        e = np.fft.rfft(g - r, axis=0)
        e[np.fft.rfftfreq(len(r), 1 / 44100.0) < above] = 0
        err = float(np.sum(np.abs(e) ** 2)) * 2 / len(r)
        sig = float(np.sum(np.abs(np.fft.rfft(r, axis=0)) ** 2)) * 2 / len(r)
        return math.inf if err == 0 else 10 * math.log10(sig / err)
    e = float(np.sqrt(np.mean((g - r) ** 2)))
    s = float(np.sqrt(np.mean(r ** 2)))
    return math.inf if e == 0 else 20 * math.log10(s / e) if s else -math.inf


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--seconds', type=float, default=0.6)
    ap.add_argument('--only', type=int, help='only this cab (0-based)')
    ap.add_argument('--min-db', type=float, default=80.0)
    a = ap.parse_args()
    fw = fwtool.Firmware(open(a.fw, 'rb').read())
    sec = bytes(fw.sections[2])
    resolver = C.Resolver([('base', fw)])
    resolver.cabs = B.factory_cabs(sec)
    x = B.signal(a.seconds)
    knobs = schedule(len(x) // 64)
    first = int(0.3 * len(knobs)) * 64
    cabs = list(resolver.cabs.items())
    cases = [(C.CAB_MENU, cid, name, sec[off:off + 4096]) for cid, (name, off) in cabs]
    cid, (name, off) = cabs[len(cabs) // 2]
    resolver.set_ir(0, sec[off:off + 4096])
    cases.append((C.IR_MENU, Q.USER_IR_BASE, 'User IR 1 = ' + name, sec[off:off + 4096]))
    if a.only is not None:
        cases = [cases[a.only]]
    worst, bad = math.inf, 0
    for menu, eid, name, ir in cases:
        ref = stock(resolver, menu, eid, x, knobs)
        got = native(ir, x, knobs)
        audible, whole = db(ref, got, 30.0), db(ref, got)
        lr = bool(np.array_equal(got[:, 0], got[:, 1]))
        ok = audible >= a.min_db and lr
        bad += not ok
        worst = min(worst, audible)
        print('%-28s acima de 30 Hz %6.1f dB  (tudo %6.1f dB)%s%s' % (name, audible, whole, '' if lr else '  L != R',
                                                                     '' if ok else '  <<<'), flush=True)
    print('\n%d casos, pior %.1f dB acima de 30 Hz, %d abaixo de %.0f dB' % (len(cases), worst, bad, a.min_db))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
