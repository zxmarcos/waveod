"""Compare waveod's global EQ and fixed input / output filters with the original firmware's code.

    python tools/check_dsp.py STOCK_FW.bin [--blocks N] [--seed S]

STOCK_FW.bin must carry the original application V1.2.2 (its functions are called at their V1.2.2 addresses):
  FUN_00029634 global EQ (state struct through the pointer at 0x2023651C), FUN_00023C9C input filter (state
  0x20236EB4), FUN_00023BB4 output filter (state 0x20236EE4).
The same blocks (noise plus sines; EQ settings that change every 8 blocks, the EQ switched off now and then, from
the factory settings to extreme ones) go through waveod's code with `waveod-tests --dsp-apply OPS OUT`:
  - fixed filters: must be identical bit for bit;
  - global EQ, run a second time with the original's coefficients put into waveod's EQ: identical bit for bit
    (stage order, switches, level ramp, arithmetic);
  - global EQ coefficients: waveod's own design against the original's, relative difference. The math
    libraries round sin / cos / pow differently, so these are close but not equal, and resonant low bands turn
    that into small output differences (printed, not judged).
Needs the 'unicorn' package (PYTHONPATH) and the host build (tools/wvbuild.py).
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, \
    UC_ARM_REG_R3, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_MAX

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfw
import wvbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLOBAL_EQ, IN_FILTER, OUT_FILTER = 0x29634, 0x23C9C, 0x23BB4
EQ_STATE_PTR, EQ_STATE = 0x2023651C, 0x20230000
EQ_COEFS = (0xF8, 0x140, 0x188, 0x1D0, 0x218, 0x260, 0x2A8, 0x2F0)   # b0 b1 b2 a0 a1 a2 of the 8 stages
IN_STATE, OUT_STATE = 0x20236EB4, 0x20236EE4
STOP = 0x7FFF0
IO, PARAMS = 0x2001E000, 0x2001E400
FRAMES, NPARAMS, NCOEFS = 64, 31, 48
KIND_EQ, KIND_IN, KIND_OUT = 0, 1, 2
INJECT = 2                              # flag in `on`: use the coefficients that follow the block
FACTORY = [1, 19, 0.71, 0, 20, 0.71, 0, 1, 100, 0.71, 0, 1, 500, 0.71, 0, 1, 1000, 0.71, 0, 1, 5000, 0.71, 0,
           0, 20000, 0.71, 0, 1, 20001, 0.71, 50]
STAGES = [(0, 'cut'), (3, 'gain'), (7, 'gain'), (11, 'gain'), (15, 'gain'), (19, 'gain'), (23, 'gain'), (27, 'cut')]


class Stock:
    def __init__(self, fw_path):
        _, secs = mkfw.read_frmw(open(fw_path, 'rb').read())
        mcu = secs[0]
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)          # double-precision FPU (the shelves use it)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        for base, size in ((0, 0x80000), (0x20000000, 0x20000), (0x20200000, 0x80000), (0x80000000, 0x2000000)):
            uc.mem_map(base, size)
        for at in (0x10, 0x1c):
            fa, size, dst = struct.unpack_from('<3I', mcu, at)
            uc.mem_write(dst, mcu[fa - 0x20000:fa - 0x20000 + size])
        uc.mem_write(STOP, b'\xfe\xe7')
        uc.mem_write(EQ_STATE, b'\0' * 0x400)
        uc.mem_write(EQ_STATE_PTR, struct.pack('<I', EQ_STATE))
        uc.mem_write(IN_STATE, b'\0' * 0x30)
        uc.mem_write(OUT_STATE, b'\0' * 0x30)

    def call(self, fn, *args):
        uc = self.uc
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(reg, v & 0xffffffff)
        uc.reg_write(UC_ARM_REG_SP, 0x20008000)
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        uc.emu_start(fn | 1, STOP, count=20_000_000)

    def block(self, kind, on, params, io):
        """processed io, and the EQ coefficients the block used"""
        uc = self.uc
        uc.mem_write(IO, struct.pack('<%df' % (2 * FRAMES), *io))
        coefs = [0.0] * NCOEFS
        if kind == KIND_EQ:
            uc.mem_write(PARAMS, struct.pack('<%df' % NPARAMS, *params))
            self.call(GLOBAL_EQ, IO, FRAMES, PARAMS, on)
            state = uc.mem_read(EQ_STATE, 0x340)
            coefs = [v for off in EQ_COEFS for v in struct.unpack_from('<6f', state, off)]
        else:
            self.call(IN_FILTER if kind == KIND_IN else OUT_FILTER, IN_STATE if kind == KIND_IN else OUT_STATE,
                      IO, FRAMES)
        return struct.unpack('<%df' % (2 * FRAMES), uc.mem_read(IO, 8 * FRAMES)), coefs


def random_params(rng):
    p = list(FACTORY)
    for first, kind in STAGES:
        p[first] = float(rng.random() < 0.7)
        p[first + 1] = float(round(20 * 1000 ** rng.random()))
        p[first + 2] = round(rng.uniform(0.1, 10.0), 1)
        if kind == 'gain':
            p[first + 3] = float(rng.randint(-20, 20))
    p[30] = float(rng.randint(0, 100))
    return p


def scenario(rng, blocks):
    """(kind, on, params, io) per block"""
    ops = []
    params = list(FACTORY)
    for b in range(blocks):
        if b % 8 == 0:
            params = random_params(rng) if b else list(FACTORY)
        io = []
        for f in range(FRAMES):
            t = (b * FRAMES + f) / 44100.0
            s = 0.3 * math.sin(2 * math.pi * 110 * t) + 0.2 * math.sin(2 * math.pi * 3150 * t)
            io += [s + rng.uniform(-0.05, 0.05), 0.5 * s + rng.uniform(-0.05, 0.05)]
        ops.append((KIND_EQ, 0 if b % 13 == 12 else 1, params, io))
        ops.append((KIND_IN, 1, FACTORY, io))
        ops.append((KIND_OUT, 1, FACTORY, io))
    return ops


def waveod_apply(ops, coefs, tmp):
    paths = [os.path.join(tmp, n) for n in ('ops.bin', 'out.bin')]
    with open(paths[0], 'wb') as f:
        for (kind, on, params, io), c in zip(ops, coefs):
            f.write(struct.pack('<II', kind, on) + struct.pack('<%df' % NPARAMS, *params) +
                    struct.pack('<%df' % (2 * FRAMES), *io) + struct.pack('<%df' % NCOEFS, *c))
    res = wvbuild.tests(['--dsp-apply'] + paths, capture_output=True,
                         text=True)
    if res.returncode:
        sys.exit('waveod --dsp-apply failed:\n' + res.stdout + res.stderr)
    data = open(paths[1], 'rb').read()
    rec = 2 * FRAMES + NCOEFS
    return [(struct.unpack_from('<%df' % (2 * FRAMES), data, 4 * rec * i),
             struct.unpack_from('<%df' % NCOEFS, data, 4 * rec * i + 8 * FRAMES)) for i in range(len(ops))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--blocks', type=int, default=400)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    ops = scenario(random.Random(a.seed), a.blocks)
    stock = Stock(a.fw)
    theirs = [stock.block(*op) for op in ops]
    injected = [(k, on | INJECT if k == KIND_EQ else on, p, io) for k, on, p, io in ops]
    with tempfile.TemporaryDirectory() as tmp:
        own = waveod_apply(ops, [c for _, c in theirs], tmp)
        exact = waveod_apply(injected, [c for _, c in theirs], tmp)

    ok = True
    def report(name, kind, ours, judge):
        nonlocal ok
        worst = peak = 0.0
        for op, (x, _), (y, _) in zip(ops, ours, theirs):
            if op[0] == kind:
                peak = max(peak, max(abs(v) for v in y))
                worst = max(worst, max(abs(p - q) for p, q in zip(x, y)))
        verdict = ''
        if judge:
            verdict = 'identical' if worst == 0 else 'DIFFERS'
            ok &= worst == 0
        print('%-40s largest difference %.3g (peak %.3g) %s' % (name, worst, peak, verdict))

    report('input filter', KIND_IN, own, True)
    report('output filter', KIND_OUT, own, True)
    report('global EQ, the original\'s coefficients', KIND_EQ, exact, True)
    rel = 0.0
    for op, (_, c1), (_, c2) in zip(ops, own, theirs):
        if op[0] == KIND_EQ:
            for s in range(8):
                scale = max(abs(v) for v in c2[6 * s:6 * s + 6]) or 1.0
                rel = max(rel, max(abs(p - q) for p, q in zip(c1[6 * s:6 * s + 6], c2[6 * s:6 * s + 6])) / scale)
    good = rel < 1e-5
    ok &= good
    print('%-40s largest relative difference %.3g %s' % ('global EQ coefficients', rel, 'OK' if good else 'DIFFERS'))
    report('global EQ, own coefficients', KIND_EQ, own, False)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
