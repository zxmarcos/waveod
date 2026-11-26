"""Compare waveod's user IR / Clone file system writer with the original firmware's, byte for byte.

    python tools/check_irfs.py STOCK_FW.bin [--ops N] [--seed S]

STOCK_FW.bin must carry the original application V1.2.2. Its code runs in Unicorn with the flash primitives
(FUN_00026868 read, FUN_000267A4 program, FUN_00026688 erase) done on a Python copy of the flash:
  FUN_000255D4 / FUN_00024AE0 mount (they format a blank area), FUN_00025D30 write (user IR slots go on to
  FUN_00025A84), FUN_00024D80 Clone rename, FUN_00024974 Clone delete.
waveod applies the same operations with `waveod-tests --irfs-apply IN OPS OUT`. Runs:
  1. blank areas formatted by the original, N random writes / Clone renames / Clone deletes;
  2. the original mounts waveod's result and keeps going with N/2 more (the mount is compared too);
  3. user IR renames (FUN_000252D8, whose decompiled form seems to clear the data block: the code keeps it).
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
FLASH = 0x800000
USER, USER_SIZE, CLONE, CLONE_SIZE = 0x753000, 0x2D000, 0x40A000, 0x36000
FLASH_READ, FLASH_PROGRAM, FLASH_ERASE = 0x26868, 0x267A4, 0x26688
MOUNT_USER, MOUNT_CLONE = 0x255D4, 0x24AE0
WRITE, RENAME_USER, RENAME_CLONE, DELETE_CLONE = 0x25D30, 0x252D8, 0x24D80, 0x24974
STOP = 0x7FFF0
ARGS, NAME, DATA = 0x2001E000, 0x2001E100, 0x80F00000


class Stock:
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
        self.flash = bytearray(b'\xff' * FLASH)

    def flash_op(self, uc, addr, size, ud):
        r = [uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        off = r[1] & 0xffffff
        if addr == FLASH_ERASE:
            off &= ~0xfff
            self.flash[off:off + 0x1000] = b'\xff' * 0x1000
        elif addr == FLASH_READ:
            uc.mem_write(r[2], bytes(self.flash[off:off + r[3]]))
        else:
            for i, b in enumerate(uc.mem_read(r[2], r[3])):
                self.flash[off + i] &= b
        uc.reg_write(UC_ARM_REG_R0, 0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)

    def call(self, fn, *args):
        uc = self.uc
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(reg, v & 0xffffffff)
        uc.reg_write(UC_ARM_REG_SP, 0x20008000)
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        uc.emu_start(fn | 1, STOP, count=100_000_000)
        return uc.reg_read(UC_ARM_REG_R0)

    def mount(self, image):
        self.flash[:] = image
        self.uc.mem_write(0x20242000, b'\0' * 0x1000)          # the file systems' state starts zeroed
        self.uc.mem_write(ARGS, struct.pack('<4I', 0x20241000, USER, USER_SIZE, 0x1000))
        self.call(MOUNT_USER, ARGS)
        self.uc.mem_write(ARGS, struct.pack('<5I', 0x20241000, CLONE, CLONE_SIZE, 0x2000, 0x1000))
        self.call(MOUNT_CLONE, ARGS)

    def apply(self, ops):
        for op, kind, slot, name, data in ops:
            self.uc.mem_write(NAME, name)
            if op == 0:
                self.uc.mem_write(DATA, data)
                self.call(WRITE, slot + (0x51 if kind else 0), NAME, DATA, len(data))
            elif op == 1:
                self.call(RENAME_CLONE if kind else RENAME_USER, slot, NAME)
            else:
                self.call(DELETE_CLONE, slot)


def random_ops(rng, n, renames_user=False):
    ops = []
    for _ in range(n):
        r = rng.random()
        kind = rng.random() < 0.4
        slot = rng.randrange(10 if kind else 20)
        name = ('%s %d' % (rng.choice(['Mesa', 'Fender', 'Vox', 'Clone', 'Tweed']), rng.randrange(100))).encode()
        name = name[:16].ljust(17, b'\0')
        if r < 0.7:
            size = (0x2000 if kind else 0x1000) if rng.random() < 0.8 else rng.randrange(1, 0x1000)
            ops.append((0, int(kind), slot, name, bytes(rng.getrandbits(8) for _ in range(size))))
        elif r < 0.85 and (kind or renames_user):
            ops.append((1, int(kind), slot, name, b''))
        elif kind:
            ops.append((2, 1, slot, name, b''))
    return ops


def waveod_apply(image, ops, tmp):
    paths = [os.path.join(tmp, n) for n in ('in.bin', 'ops.bin', 'out.bin')]
    open(paths[0], 'wb').write(image)
    with open(paths[1], 'wb') as f:
        for op, kind, slot, name, data in ops:
            f.write(struct.pack('<BBB', op, kind, slot) + name + struct.pack('<I', len(data)) + data)
    res = wvbuild.tests(['--irfs-apply'] + paths, capture_output=True,
                         text=True)
    if res.returncode:
        sys.exit('waveod --irfs-apply failed:\n' + res.stdout + res.stderr)
    return open(paths[2], 'rb').read()


def compare(name, ours, theirs, skip=()):
    diff = [i for i in list(range(USER, USER + USER_SIZE)) + list(range(CLONE, CLONE + CLONE_SIZE))
            if ours[i] != theirs[i] and i not in skip]
    if not diff:
        print('%s: identical' % name)
        return True
    i = diff[0]
    print('%s: %d bytes differ, first at %#x (ours %s, theirs %s)' % (
        name, len(diff), i, ours[i:i + 8].hex(), theirs[i:i + 8].hex()))
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--ops', type=int, default=120)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    stock = Stock(a.fw)
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        stock.mount(b'\xff' * FLASH)                        # the original formats both areas
        start = bytes(stock.flash)
        ops = random_ops(rng, a.ops)
        stock.apply(ops)
        ours = waveod_apply(start, ops, tmp)
        ok &= compare('1. %d operations from blank areas' % len(ops), ours, bytes(stock.flash))

        ops = random_ops(rng, a.ops // 2)
        stock.mount(ours)
        stock.apply(ops)
        ours = waveod_apply(ours, ops, tmp)
        ok &= compare('2. %d more after a fresh mount' % len(ops), ours, bytes(stock.flash))

        # user IR renames
        ops = [(1, 0, s, ('Renamed %d' % s).encode().ljust(17, b'\0'), b'') for s in rng.sample(range(20), 5)]
        stock.mount(ours)
        stock.apply(ops)
        ours = waveod_apply(ours, ops, tmp)
        ok &= compare('3. %d user IR renames' % len(ops), ours, bytes(stock.flash))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
