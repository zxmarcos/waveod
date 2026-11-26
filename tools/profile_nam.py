"""Where the NAM Clone block's time goes in the ARM emulator (docs/nam.md "Cost").

    python tools/profile_nam.py FW.bin MODEL.nam [--elf build/fw/waveod.elf] [--blocks 8] [--lru]

The whole firmware runs in tools/emu.py as in tools/check_nam.py: the .nam written into Clone slot 1 (48 kHz)
and slot 2 (44.1 kHz), a preset with just the Clone block playing a tone, neutral knobs. Per slot:
  1. the audio interrupt's instructions per block, by function (a code hook);
  2. the lines the D-cache fetches per block: every access of the interrupt to the network's histories and to
     the application's .bss goes through a model of the M7's data cache (32 KB, 4 ways, 32-byte lines, a random
     way replaced; --lru: the least recently used one). ITCM and DTCM are not cached. No prefetch (PLD) in the
     model: a fetched line is one the code needs, in time or not.
Unicorn loses the IT-block state when a memory hook fires inside one, so pass 2 hooks only the instance's
struct a2 (assembly and straight loops) and the .bss, not the block's small buffers, and runs the same blocks
again without the hook: the state pool must end the same.
Needs unicorn and capstone.
"""
import argparse
import bisect
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'studio'))
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE     # noqa: E402
import check_blocks as cb                                   # noqa: E402
import check_editor as ce                                   # noqa: E402
import check_nam as cn                                      # noqa: E402
from waveod_studio import nam                               # noqa: E402
from waveod_studio.protocol import Pedal                    # noqa: E402

STATE_POOL, STATE_END = 0x20204000, 0x20234000              # engine/pools.c; the block is its first allocation
BSS, BSS_END = 0x20242000, 0x20278000                       # board/qme200/qme200.ld
CODE_POOL, CODE_POOL_SIZE = 0x00058000, 0x20000
# struct a2 (nam/a2.h): w0, head, L[23] {h, kern, ring, cur, lb, k, d, direct, move, struct a2k_args}, x, hh, hist
A2_L, A2_STRIDE, A2_LAYERS = 8, 124, 23
KERNEL = [6] * 14 + [15, 15] + [6] * 7
DILATION = [1, 3, 7, 17, 41, 101, 239] * 2 + [1, 13] + [1, 3, 7, 17, 41, 101, 239]
COLS_MORE = 72 + 6                                          # A2_B4 + A2_SPARE


class Cache:
    SETS, WAYS = 256, 4

    def __init__(self, lru):
        self.rnd, self.lru = random.Random(1), lru
        self.sets = [[] for _ in range(self.SETS)]          # per set: [line, dirty], most recent last
        self.reset()

    def reset(self):
        self.accesses = self.evicted = 0
        self.fetched = {}                                   # what -> [for a read, for a write]

    def access(self, addr, size, write, what):
        for line in range(addr >> 5, ((addr + size - 1) >> 5) + 1):
            self.accesses += 1
            s = self.sets[line & (self.SETS - 1)]
            for i, e in enumerate(s):
                if e[0] == line:
                    e[1] = e[1] or write
                    s.append(s.pop(i))
                    break
            else:
                self.fetched.setdefault(what, [0, 0])[1 if write else 0] += 1
                if len(s) == self.WAYS and s.pop(0 if self.lru else self.rnd.randrange(self.WAYS))[1]:
                    self.evicted += 1
                s.append([line, write])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('model')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--blocks', type=int, default=8)
    ap.add_argument('--lru', action='store_true')
    a = ap.parse_args()
    cap48, _ = nam.capture(a.model)
    cap44, _ = nam.capture(a.model, native_rate=True)
    ids = cn.clone_ids(a.fw)
    w = ce.Waveod(a.fw, a.elf)
    b, uc = w.b, w.b.uc
    funcs = sorted((v[0] & ~1, v[1], k) for k, v in b.syms.items() if v[1] and (v[0] < 0x80000 or v[0] >= 0x80000000))
    starts = [f[0] for f in funcs]
    tables = [b.syms[n] for n in ('t_up', 't_dn') if n in b.syms]
    pedal = Pedal(transport=cb.Transport(w), timeout=60)
    pedal.write_clone(0, 'NAM 48k', cap48)
    pedal.write_clone(1, 'NAM 44k', cap44)
    _, data = pedal.get(0)
    base = cb.empty(data)
    pedal.put(cb.EMPTY, base)
    counts, cache, layers = {}, Cache(a.lru), []

    def on_code(uc_, addr, size, ud):
        i = bisect.bisect_right(starts, addr) - 1
        name = funcs[i][2] if i >= 0 and addr < funcs[i][0] + funcs[i][1] else '?'
        counts[name] = counts.get(name, 0) + 1

    def what(addr):
        if addr < STATE_END:
            for lo, hi, name in layers:
                if lo <= addr < hi:
                    return name
            return 'the input and the head sums'
        for at, size in tables:
            if at <= addr < at + size:
                return 'the resampler\'s tables'
        return 'the rest of the .bss'

    def on_mem(uc_, access, addr, size, value, ud):
        cache.access(addr, size, access == UC_MEM_WRITE, what(addr))

    def blocks(n, hook=None, count=False):
        hs = []
        if hook == 'code':
            hs.append(uc.hook_add(UC_HOOK_CODE, on_code))
        elif hook == 'cache':
            end = max(hi for lo, hi, _ in layers if lo >= STATE_POOL)
            hs.append(uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_mem, begin=STATE_POOL, end=end - 1))
            hs.append(uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_mem, begin=BSS, end=BSS_END - 1))
            uc.ctl_flush_tb()                   # what was translated before the hook does not call it
        total = 0
        for _ in range(n):
            f = 110.0 * 2 ** ((w.phase // 30) % 5 / 2.0)
            total += b.audio_block(w.phase, freq=f, level=0.2, count=count)[0]
            w.phase += 1
            if count:
                b.run_ms(1.4512)
        for h in hs:
            uc.hook_del(h)
        if hook == 'cache':
            uc.ctl_flush_tb()
        return total

    def measure(title, index, block):
        pedal.load(cb.EMPTY)
        pedal.load(index)
        blocks(40, count=True)
        plain = blocks(8, count=True) / 8
        counts.clear()
        blocks(a.blocks, 'code')
        print('\n%s: %.0f instructions per audio block' % (title, plain))
        for name, n in sorted(counts.items(), key=lambda kv: -kv[1])[:12]:
            print('   %9.0f  %s' % (n / a.blocks, name))
        if block:
            del layers[:]
            for l in range(1, A2_LAYERS):
                h, = struct.unpack('<I', uc.mem_read(STATE_POOL + A2_L + l * A2_STRIDE, 4))
                lb = (KERNEL[l] - 1) * DILATION[l]
                if not (STATE_POOL <= h < STATE_END or CODE_POOL <= h < CODE_POOL + CODE_POOL_SIZE):
                    sys.exit('layer %d\'s history at %#x: is struct a2 still as this script has it?' % (l, h))
                layers.append((h, h + (lb + COLS_MORE) * 12, 'the long histories (rings)' if lb > 205 else
                               'the short histories'))
            where = 'the code pool (ITCM, not cached)' if any(lo < STATE_POOL for lo, _, _ in layers) else \
                'the state pool'
            regions = [(CODE_POOL, CODE_POOL_SIZE), (0x20000000, 0x8000), (0x20200000, 0x80000)]
            snap = [bytes(uc.mem_read(x, n)) for x, n in regions]
            ctx, st = uc.context_save(), (b.cyc, b.gpt, dict(b.regs), w.phase)
            blocks(4, 'cache')
            cache.reset()
            blocks(a.blocks, 'cache')
            hooked = bytes(uc.mem_read(STATE_POOL, STATE_END - STATE_POOL))
            for (x, _), s in zip(regions, snap):
                uc.mem_write(x, s)
            uc.context_restore(ctx)
            b.cyc, b.gpt, w.phase = st[0], st[1], st[3]
            b.regs.clear()
            b.regs.update(st[2])
            blocks(4 + a.blocks)
            same = hooked == bytes(uc.mem_read(STATE_POOL, STATE_END - STATE_POOL))
            total = sum(r + wr for r, wr in cache.fetched.values())
            print('   D-cache: %.0f lines fetched per block (%.0f written back), the short histories in %s%s' %
                  (total / a.blocks, cache.evicted / a.blocks, where,
                   '' if same else '; NOT VALID: the state pool ends differently without the hook'))
            for name, (r, wr) in sorted(cache.fetched.items(), key=lambda kv: -sum(kv[1])):
                print('      %6.0f for a read %6.0f for a write  %s' % (r / a.blocks, wr / a.blocks, name))
        if b.faults:
            sys.exit('FAULT: ' + '; '.join(b.faults))
        return plain

    knobs = {i: 50 for i in range(5)}
    without = measure('no block', cb.EMPTY, False)
    for slot, title in ((0, '48 kHz'), (1, '44.1 kHz')):
        pedal.put(cn.PRESET, cn.with_clone(base, ids[slot], knobs))
        n = measure(title, cn.PRESET, True)
        print('   %s: %+.0f instructions against the preset without the block' % (title, n - without))


if __name__ == '__main__':
    main()
