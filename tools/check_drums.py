"""Compare waveod's drum voices with the original firmware's, bit for bit.

    python tools/check_drums.py STOCK_FW.bin [--blocks N] [--seed S]

STOCK_FW.bin must carry the original application V1.2.2. Its drum code runs in Unicorn: FUN_00040CE0 sets the
banks up from the application's own SDRAM image, FUN_00042534 queues a hit, FUN_0004104C mixes one block of the
16 voices into 64 int32. The same hits (random samples and velocities, now and then several in a block, more
than 16 at once to fill the voices) go through waveod (`waveod-tests --drums-apply FW OPS OUT`, which takes
the sounds from the same file the way tools/mkfw.py does). The blocks must be identical.
Needs the 'unicorn' package (PYTHONPATH) and the host build (tools/wvbuild.py).
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, \
    UC_CPU_ARM_MAX

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfw
import wvbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INIT, QUEUE, MIX = 0x40CE0, 0x42534, 0x4104C
STOP, OUT = 0x7FFF0, 0x2001E000


class Stock:
    def __init__(self, fw_path):
        _, secs = mkfw.read_frmw(open(fw_path, 'rb').read())
        mcu = secs[0]
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        for base, size in ((0, 0x80000), (0x20000000, 0x20000), (0x20200000, 0x80000), (0x80000000, 0x2000000)):
            uc.mem_map(base, size)
        for at in (0x10, 0x1c):
            fa, size, dst = struct.unpack_from('<3I', mcu, at)
            uc.mem_write(dst, mcu[fa - 0x20000:fa - 0x20000 + size])
        uc.mem_write(0x20234000, b'\0' * 0x10000)          # the drum module's state starts zeroed
        uc.mem_write(STOP, b'\xfe\xe7')
        self.call(INIT)

    def call(self, fn, *args):
        uc = self.uc
        for k, v in enumerate(args):
            uc.reg_write(UC_ARM_REG_R0 + k, v)
        uc.reg_write(UC_ARM_REG_SP, 0x20008000)
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        uc.emu_start(fn | 1, STOP, count=5_000_000)

    def block(self, notes):
        for n in notes:
            self.call(QUEUE, n)
        self.call(MIX, OUT)
        return struct.unpack('<64i', self.uc.mem_read(OUT, 256))


def scenario(rng, blocks):
    out = []
    for b in range(blocks):
        k = rng.choice([0] * 6 + [1, 1, 2, 3]) if b % 97 else 18      # a burst now and then: more hits than voices
        out.append([rng.randrange(13) << 8 | rng.randrange(128) for _ in range(k)])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--blocks', type=int, default=3000)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    ops = scenario(random.Random(a.seed), a.blocks)
    stock = Stock(a.fw)
    theirs = [stock.block(n) for n in ops]
    with tempfile.TemporaryDirectory() as tmp:
        paths = [os.path.join(tmp, n) for n in ('ops.bin', 'out.bin')]
        with open(paths[0], 'wb') as f:
            for notes in ops:
                f.write(struct.pack('<I%dH' % len(notes), len(notes), *notes))
        res = wvbuild.tests(['--drums-apply', os.path.abspath(a.fw)] + paths, capture_output=True, text=True)
        if res.returncode:
            sys.exit('waveod --drums-apply failed:\n' + res.stdout + res.stderr)
        data = open(paths[1], 'rb').read()
    ours = [struct.unpack_from('<64i', data, 256 * i) for i in range(len(ops))]
    bad = [i for i, (x, y) in enumerate(zip(ours, theirs)) if x != y]
    peak = max(max(abs(v) for v in y) for y in theirs)
    hits = sum(len(n) for n in ops)
    print('%d blocks, %d hits, peak %d: %s' % (len(ops), hits, peak,
                                              'identical' if not bad else 'DIFFERS from block %d' % bad[0]))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
