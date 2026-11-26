"""Do the effect modules work in blocks of 32 frames? A low-latency mode would run the audio in blocks of 32
(the buffers' 128 samples, 2.90 ms, become 64, 1.45 ms); the modules get the block size in Process's second
argument, but nothing promises they use it.

Every module of a firmware's library runs twice in Unicorn on the same signal, from the same Init: in blocks
of 64 frames (the reference) and in blocks of 32 (two calls per 64 frames). Then:
  - whether a 32-frame call touches frames 32..63 (sentinel values there): the module ignores the size;
  - the 32-frame output against the reference (bit-exact, or the error in dB under the signal);
  - the cost of 64 frames both ways (bytes of code executed, through Unicorn's basic block hook, over a few
    blocks after a warm-up): the per-call overhead that halving the block adds.

    python tools/check_block.py BASE.bin [--seconds 0.75] [--menu M] [--json OUT.json]

Cab blocks run their engine (library entry 5.0) with a few factory cabs; User IR and Clone need slot data
and are left out. Needs matribox-studio (pip install -e) and unicorn.

Result on the V1.2.2 library (docs/latency.md): every module honours the size except the Cab and User IR
engines, which always process 64 frames.
"""
import argparse
import json
import math
import struct
import sys
import time

import numpy as np
from unicorn import UC_HOOK_BLOCK

from matribox.emulator import chain as C
from matribox.firmware import fwtool
from matribox.module import effemu as E

FS = 44100
SENTINEL = 12345.0
COST_WARM, COST_BLOCKS = 12, 8              # 64-frame blocks before / while measuring the cost
CABS = 6                                    # factory cabs tried with the Cab engine


def factory_cabs(section):
    """the IR section's factory cabinets {id: (name, data offset)}, read like waveod's fw/ir_store.c: header
    {u16 0x5AA5, .., u16 block size @4, .., u8 count @0xA}, 0x48-byte entries from +0x10 {u16 index, u16 block,
    u32 id, char name[16]}; the last 30 entries are the user IRs and Clone slots"""
    out = {}
    if len(section) < 0x10 or struct.unpack_from('<H', section, 0)[0] != 0x5AA5:
        return out
    bsize = struct.unpack_from('<H', section, 4)[0] or 0x1000
    for i in range(max(0, section[0x0A] - 30)):
        _idx, blk, eid = struct.unpack_from('<HHI', section, 0x10 + 0x48 * i)
        name = section[0x18 + 0x48 * i:0x28 + 0x48 * i].split(b'\0')[0].decode('latin1')
        if blk:
            out[eid] = (name, blk * bsize)
    return out


def signal(seconds):
    """a guitar-like test signal: plucked notes (decaying harmonics) every 150 ms with a little noise, stereo"""
    n = int(seconds * FS) // 64 * 64
    t = np.arange(n) / FS
    x = np.zeros(n)
    notes = (82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 220.0, 164.8)
    for k, start in enumerate(np.arange(0.0, seconds, 0.15)):
        f, s = notes[k % len(notes)], int(start * FS)
        tt = t[s:] - start
        env = np.exp(-tt * 6.0)
        x[s:] += 0.3 * env * sum(np.sin(2 * np.pi * f * h * tt) / h for h in range(1, 6))
    x += 0.001 * np.random.default_rng(1).standard_normal(n)
    return np.stack([x, x], 1).astype(np.float32)


class Runner:
    """one module loaded and initialised like the app does (matribox's UnicornBackend), Process on any size"""
    def __init__(self, resolver, menu, eid, knobs):
        spec = C.ChainSpec([C.SlotSpec(0, menu, eid, True, knobs)])
        self.b = b = C.UnicornBackend(spec, resolver)
        if not b.slots:
            raise RuntimeError(b.notes.get(0, 'não carregou'))
        _, _, self.mod, self.K, self.EN = b.slots[0]
        self.uc = b.emu.uc
        self.bytes = 0
        self.hook = None

    def count(self, on):
        if on and self.hook is None:
            def blk(uc, addr, size, user):
                self.bytes += size
            self.hook = self.uc.hook_add(UC_HOOK_BLOCK, blk)
        elif not on and self.hook is not None:
            self.uc.hook_del(self.hook)
            self.hook = None

    def process(self, io, n):
        """io: (n, 2) float32 -> output, and whether frames n..63 of the buffer were touched"""
        b, uc = self.b, self.uc
        b.toggle ^= 1
        uc.mem_write(E.HOST_TOGGLE, bytes([b.toggle]))
        buf = np.full((64, 2), SENTINEL, np.float32)
        buf[:n] = io
        uc.mem_write(b.BUF, buf.tobytes())
        uc.mem_write(b.AUX, buf.tobytes())
        uc.mem_write(b.X1, bytes(8 * 64))
        b.emu.call(self.mod.process, b.BUF, n, self.K, b.AUX, self.EN, b.BEAT, b.EXTRA, budget=40_000_000)
        out = np.frombuffer(bytes(uc.mem_read(b.BUF, 8 * 64)), np.float32).reshape(64, 2)
        return out[:n].copy(), n < 64 and bool(np.any(out[n:] != SENTINEL))


def run(resolver, menu, eid, knobs, x, n):
    r = Runner(resolver, menu, eid, knobs)
    out, wild = [], False
    for b in range(0, len(x), 64):
        for o in range(0, 64, n):
            y, w = r.process(x[b + o:b + o + n], n)
            out.append(y)
            wild |= w
    return np.concatenate(out), wild


def cost(resolver, menu, eid, knobs, x, n):
    """bytes of code executed per 64 frames, over COST_BLOCKS blocks after COST_WARM (a fresh module: the hook
    has to be there before Unicorn translates the code)"""
    r = Runner(resolver, menu, eid, knobs)
    r.count(True)
    for b in range(0, 64 * (COST_WARM + COST_BLOCKS), 64):
        if b // 64 == COST_WARM:
            r.bytes = 0
        for o in range(0, 64, n):
            r.process(x[b + o:b + o + n], n)
    return r.bytes / COST_BLOCKS


def compare(ref, got):
    if np.array_equal(ref, got):
        return 'idêntico', math.inf
    if not np.all(np.isfinite(got)):
        return 'NaN', -math.inf
    err = float(np.sqrt(np.mean((got.astype(np.float64) - ref) ** 2)))
    lvl = float(np.sqrt(np.mean(ref.astype(np.float64) ** 2)))
    db = 20 * math.log10(lvl / err) if lvl > 0 and err > 0 else (math.inf if err == 0 else -math.inf)
    return ('equivalente' if db >= 80 else 'próximo' if db >= 40 else 'diferente'), db


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--seconds', type=float, default=0.75)
    ap.add_argument('--menu', type=int, action='append')
    ap.add_argument('--json')
    a = ap.parse_args()
    fw = fwtool.Firmware(open(a.fw, 'rb').read())
    resolver = C.Resolver([('base', fw)])
    resolver.cabs = factory_cabs(bytes(fw.sections[2])) if len(fw.sections) > 2 else {}
    lib = fw.library
    x = signal(a.seconds)
    rows = []
    t0 = time.time()
    for m, lst in enumerate(lib.cat_entries):
        if a.menu and m not in a.menu:
            continue
        if m in (C.IR_MENU, C.CLONE_MENU):
            continue
        items = [(lib.entries[ei], lib.entries[ei].id, lib.entries[ei].name) for ei in lst]
        if m == C.CAB_MENU and items:              # the engine with a few factory cabs
            cabs = list(resolver.cabs.items())
            items = [(items[0][0], cid, name) for cid, (name, _) in cabs[:: max(1, len(cabs) // CABS)][:CABS]]
        for k, (e, eid, name) in enumerate(items):
            knobs = [0.0] * C.NKNOBS
            for p in e.params:
                if 0 <= p['idx'] < C.NKNOBS:
                    knobs[p['idx']] = p['default']
            row = dict(menu=m, index=k, id='%#010x' % eid, name=name, category=e.category)
            try:
                ref, _ = run(resolver, m, eid, knobs, x, 64)
                got, wild = run(resolver, m, eid, knobs, x, 32)
                c64, c32 = cost(resolver, m, eid, knobs, x, 64), cost(resolver, m, eid, knobs, x, 32)
                verdict, db = compare(ref, got)
                row.update(verdict='ignora o tamanho' if wild else verdict, db=db, cost64=c64, cost32=c32,
                           overhead=(c32 / c64 - 1) if c64 else None)
            except Exception as ex:                     # noqa: BLE001 - every failure is a result
                row.update(verdict='falha', error=str(ex)[:120])
            rows.append(row)
            print('%2d.%-3d %-22s %-18s %s%s' % (
                m, k, name[:22], row['verdict'],
                '' if 'db' not in row or row['db'] in (math.inf,) else '%6.1f dB ' % row['db'],
                ' custo +%.0f%%' % (100 * row['overhead']) if row.get('overhead') is not None else
                (' ' + row.get('error', '') if 'error' in row else '')), flush=True)
    counts = {}
    for r in rows:
        counts[r['verdict']] = counts.get(r['verdict'], 0) + 1
    over = [r['overhead'] for r in rows if r.get('overhead') is not None]
    print('\n%d módulos em %.0f s: %s' % (len(rows), time.time() - t0,
                                          ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    if over:
        print('custo de 64 amostras em 2 x 32: mediana +%.1f%%, máximo +%.1f%%' % (
            100 * float(np.median(over)), 100 * max(over)))
    if a.json:
        json.dump(rows, open(a.json, 'w', encoding='utf-8'), indent=1, ensure_ascii=False,
                  default=lambda v: None if isinstance(v, float) and not math.isfinite(v) else v)
    return 0


if __name__ == '__main__':
    sys.exit(main())
