"""Compare waveod's record store writer with the original firmware's, byte for byte.

    python tools/check_store.py STOCK_FW.bin [--writes N] [--seed S]

STOCK_FW.bin must carry the original application V1.2.2 (its store functions are called at their V1.2.2
addresses). The original code runs in Unicorn with its flash primitives done on a Python copy of the store:
  FUN_00026000 open (formats a blank area, then FUN_00026160 mount), FUN_000262F4 write a record.
waveod applies the same writes with `waveod-tests --store-apply IN OPS OUT`. Three runs:
  1. a blank area formatted by the original, then N random writes;
  2. the result of 1 aged by hand (superseded copies marked in use again, as after power cuts), then N/4
     more writes, so the start-up clean-up is compared too;
  3. the original reads every record of waveod's result (FUN_000260DC) and gets what was written last.
Needs the 'unicorn' package (PYTHONPATH) and the host build (tools/wvbuild.py).
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, \
    UC_ARM_REG_R2, UC_ARM_REG_R3, UC_CPU_ARM_MAX

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfw
import wvbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORE, SIZE = 0x780000, 0x80000
FLASH_READ, FLASH_PROGRAM, FLASH_ERASE = 0x26868, 0x267A4, 0x26688
STORE_OPEN, STORE_WRITE, STORE_READ = 0x26000, 0x262F4, 0x260DC
STOP = 0x7FFF0
ARGS = 0x2001E000                   # argument struct, then data buffers
BUF = 0x2001E100


class Stock:
    """the original's store functions over a Python copy of the 512 KB store"""

    def __init__(self, fw_path):
        _, secs = mkfw.read_frmw(open(fw_path, 'rb').read())
        mcu = secs[0]
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        for base, size in ((0, 0x80000), (0x20000000, 0x20000), (0x20200000, 0x80000), (0x80000000, 0x2000000)):
            uc.mem_map(base, size)
        for at in (0x10, 0x1c):
            fa, size, dst = struct.unpack_from('<3I', mcu, at)
            uc.mem_write(dst, mcu[fa - 0x20000:fa - 0x20000 + size])
        uc.mem_write(STOP, b'\xfe\xe7')
        for a in (FLASH_READ, FLASH_PROGRAM, FLASH_ERASE):
            uc.hook_add(UC_HOOK_CODE, self.flash_op, begin=a, end=a)
        self.store = bytearray(b'\xff' * SIZE)

    def flash_op(self, uc, addr, size, ud):
        r = [uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        off = r[1] - STORE
        if addr == FLASH_ERASE:
            off &= ~0xfff
            assert 0 <= off < SIZE, hex(r[1])
            self.store[off:off + 0x1000] = b'\xff' * 0x1000
        else:
            assert 0 <= off and off + r[3] <= SIZE, hex(r[1])
            if addr == FLASH_READ:
                uc.mem_write(r[2], bytes(self.store[off:off + r[3]]))
            else:
                data = uc.mem_read(r[2], r[3])
                for i, b in enumerate(data):
                    self.store[off + i] &= b
        uc.reg_write(UC_ARM_REG_R0, 0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)

    def call(self, fn, *args):
        uc = self.uc
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(reg, v & 0xffffffff)
        uc.reg_write(UC_ARM_REG_SP, 0x20008000)
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        uc.emu_start(fn | 1, STOP, count=50_000_000)
        return uc.reg_read(UC_ARM_REG_R0)

    def open(self, image):
        self.store[:] = image
        self.uc.mem_write(0x20240000, b'\0' * 0x8000)          # the store state (0x20242004) starts zeroed
        self.uc.mem_write(ARGS, struct.pack('<III', 0x20241000, STORE, SIZE))
        self.call(STORE_OPEN, ARGS)

    def write(self, rec, data):
        self.uc.mem_write(BUF, data)
        return self.call(STORE_WRITE, rec, BUF, len(data))

    def read(self, rec, length):
        self.uc.mem_write(BUF, b'\0' * length)
        n = self.call(STORE_READ, rec, BUF, length)
        return bytes(self.uc.mem_read(BUF, length)) if n else None


def random_writes(rng, n):
    ops = []
    for _ in range(n):
        rec = rng.choice([0, 1] + list(range(2, 300)) + [2, 3, 4, 5] * 20)   # some records far more often
        length = rng.choice([0x238, 0x1e0, 0x48c, rng.randint(1, 0x54c)])
        ops.append((rec, bytes(rng.getrandbits(8) for _ in range(length))))
    return ops


def waveod_apply(image, ops, tmp):
    paths = [os.path.join(tmp, n) for n in ('in.bin', 'ops.bin', 'out.bin')]
    open(paths[0], 'wb').write(image)
    open(paths[1], 'wb').write(b''.join(struct.pack('<HH', r, len(d)) + d for r, d in ops))
    res = wvbuild.tests(['--store-apply'] + paths, capture_output=True,
                         text=True)
    if res.returncode:
        sys.exit('waveod --store-apply failed:\n' + res.stdout + res.stderr)
    return open(paths[2], 'rb').read()


def compare(name, ours, theirs):
    if ours == theirs:
        print('%s: identical' % name)
        return True
    diff = [i for i in range(SIZE) if ours[i] != theirs[i]]
    print('%s: %d bytes differ, first at +%#x (sector %d)' % (name, len(diff), diff[0], diff[0] // 0x1000))
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--writes', type=int, default=600)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    stock = Stock(a.fw)
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        stock.open(b'\xff' * SIZE)                          # the original formats a blank area
        start = bytes(stock.store)
        ops = random_writes(rng, a.writes)
        for r, d in ops:
            stock.write(r, d)
        ours = waveod_apply(start, ops, tmp)
        ok &= compare('1. %d writes from a blank store' % len(ops), ours, bytes(stock.store))

        aged = bytearray(ours)
        superseded = [i for i in range(1, SIZE // 0x1000) if aged[i * 0x1000:i * 0x1000 + 2] == b'\x80\x8e']
        for i in rng.sample(superseded, min(12, len(superseded))):
            aged[i * 0x1000:i * 0x1000 + 2] = b'\x8a\x8e'    # as if the power went before the 0x8E80 mark
        ops = random_writes(rng, a.writes // 4)
        stock.open(bytes(aged))
        for r, d in ops:
            stock.write(r, d)
        ours = waveod_apply(bytes(aged), ops, tmp)
        ok &= compare('2. %d writes after power cuts' % len(ops), ours, bytes(stock.store))

        last = {}
        for r, d in ops:
            last[r] = d
        stock.open(ours)
        bad = [r for r, d in last.items() if stock.read(r, len(d)) != d]
        print('3. the original reads %d of %d records back from waveod\'s store' % (len(last) - len(bad), len(last)))
        ok &= not bad
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
