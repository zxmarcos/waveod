"""Spillover in the ARM emulator: a preset rings out after a preset change, as if it had not changed.

    python tools/check_spill.py out/QME-200_waveod.bin [--from 0] [--keys B] [--elf build/fw/waveod.elf]

The whole firmware runs in tools/emu.py with the stock effect modules. A tone plays into preset --from for
TONE_MS, then the guitar goes silent and the footswitches in --keys are pressed (audio blocks run all the
time, so the interrupt takes the tail while the main loop loads the next preset). Three runs:

  reference   the same, without the change: the old preset ringing out on its own
  spillover   the change with the spillover (the factory setting, 10 s)
  cut         the change with the spillover off (engine spill time 0): the original's behaviour

Printed per window after the change: the output level (dB of the level while the tone played) and, for the
spillover, how far it is from the reference (the level of the difference). The tail must follow the
reference (the same modules, the same silence in) until the slots before the delays and reverbs stop, and
the cut run must be silent.
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu

TONE_MS = 600
AFTER_MS = 1800
BLOCK_MS = 64 / 44.1
WINDOWS = ((0, 30), (30, 100), (100, 300), (300, 800), (800, 1800))


def find_u32(b, base, size, value, start=0):
    data = bytes(b.uc.mem_read(base, size))
    hits = [i for i in range(start, size - 3, 4) if struct.unpack_from('<I', data, i)[0] == value]
    return hits


class Run:
    def __init__(self, fw, elf, boot_ms):
        self.b = emu.Board(fw, elf)
        if not self.b.run_ms(boot_ms):
            sys.exit('boot: FAULT ' + '; '.join(self.b.faults))
        self.phase = 0
        self.left = []
        self.right = []
        self.eng = self.b.sym('eng')
        self.eng_size = self.b.syms['eng'][1]
        self.spills_off = None
        self.handover = None                    # sample index of the block where the tail took over

    def block(self, level, count=False):
        n, out = self.b.audio_block(self.phase, level=level, count=count)
        self.count = n
        self.phase += 1
        if self.spills_off is not None and self.handover is None and self.word(self.spills_off):
            self.handover = len(self.left)
        self.left += [v / 2 ** 31 for v in out[0::4]]
        self.right += [v / 2 ** 31 for v in out[2::4]]
        self.b.run_ms(BLOCK_MS)
        if self.b.faults:
            sys.exit('FAULT: ' + '; '.join(self.b.faults))

    def play(self, ms, level):
        for _ in range(int(ms / BLOCK_MS)):
            self.block(level)

    def press(self, key):
        """a footswitch pressed for 60 ms and released (it acts on release) while the audio runs"""
        mask = 1 << emu.BUTTONS[key]
        self.b.buttons &= ~mask
        self.play(60, 0.0)
        self.b.buttons |= mask

    def stats_offset(self):
        """engine_stats.blocks: the u32 in `eng` that holds the number of blocks run, at two different counts"""
        self.play(BLOCK_MS * 7.5, 0.0)
        a = set(find_u32(self.b, self.eng, self.eng_size, self.phase))
        self.play(BLOCK_MS * 3.5, 0.0)
        cands = sorted(a & set(find_u32(self.b, self.eng, self.eng_size, self.phase)))
        if len(cands) != 1:
            sys.exit('engine_stats.blocks not found (%s)' % cands)
        return cands[0]

    def word(self, off):
        return struct.unpack('<I', bytes(self.b.uc.mem_read(self.eng + off, 4)))[0]


def write_wav(path, left, right):
    data = b''.join(struct.pack('<ff', l, r) for l, r in zip(left, right))
    fmt = struct.pack('<HHIIHH', 3, 2, 44100, 44100 * 8, 8, 32)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data)


def level_db(x):
    return 20 * math.log10(x) if x > 1e-12 else -240.0


def rms(xs):
    return math.sqrt(sum(v * v for v in xs) / len(xs)) if xs else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(emu.ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--boot-ms', type=int, default=400)
    ap.add_argument('--keys', default='B', help='footswitches pressed after the tone, e.g. B or B,C (rapid)')
    ap.add_argument('--wav', help='directory for the three runs as WAV files')
    ap.add_argument('--runs', default='reference,spillover,cut')
    ap.add_argument('--after', type=float, default=AFTER_MS)
    ap.add_argument('--cost', action='store_true', help='instructions per block during the tail')
    ap.add_argument('--trace', action='store_true', help='block levels around the handover')
    ap.add_argument('--cpu', type=float, default=0, help='every block measures US microseconds (cut by CPU)')
    ap.add_argument('--cpu-at', type=float, help='only from this many ms after the press')
    a = ap.parse_args()
    keys = a.keys.split(',')

    results = {}
    for name in a.runs.split(','):
        r = Run(a.fw, a.elf, a.boot_ms)
        blocks_off = r.stats_offset()
        r.spills_off = blocks_off + 12
        spill_max = (10000 * 44100) // (1000 * 64) + 1
        hits = find_u32(r.b, r.eng, r.eng_size, spill_max)
        if len(hits) != 1:
            sys.exit('engine spill time not found (%d hits)' % len(hits))
        if name == 'cut':
            r.b.uc.mem_write(r.eng + hits[0], struct.pack('<I', 0))
        if a.cpu and a.cpu_at is None:
            r.b.cyc_step = int(a.cpu * 600)
        r.play(TONE_MS, 0.25)
        tone = rms(r.left[-int(0.2 * 44100):])
        start = len(r.left)
        if name == 'reference':
            r.play(60 * len(keys), 0.0)
        else:
            for k in keys:
                r.press(k)
        costs = []
        for k in range(int(a.after / BLOCK_MS)):
            t_ms = (len(r.left) - start) / 44.1
            want = r.handover is not None and any(abs(t_ms - (r.handover - start) / 44.1 - m) < BLOCK_MS / 2
                                                   for m in (3, 50, 200, 400, 1000))
            if a.cpu and a.cpu_at is not None and t_ms >= a.cpu_at:
                r.b.cyc_step = int(a.cpu * 600)
            r.block(0.0, count=want and a.cost)
            if want and a.cost:
                costs.append('%.0f ms: %d' % (t_ms - (r.handover - start) / 44.1, r.count))
        if a.cost:
            r.block(0.0, count=True) if name == 'reference' else None
            print('%-10s instructions per audio block after the handover: %s%s' % (name, ', '.join(costs),
                  ('(%d without a change)' % r.count) if name == 'reference' else ''))
        spills, cuts, overloads = r.word(blocks_off + 12), r.word(blocks_off + 16), r.word(blocks_off + 8)
        results[name] = (r.left[start:], r.right[start:], tone)
        if r.handover is not None:
            print('%-10s handed over %.1f ms after the press' % (name, (r.handover - start) / 44.1))
            results['handover'] = r.handover - start
        if a.wav:
            write_wav(os.path.join(a.wav, 'spill_%s.wav' % name), r.left, r.right)
        print('%-10s tone %.1f dB FS; spills %d, cut by CPU %d, overloads %d' %
              (name, level_db(tone), spills, cuts, overloads))

    ref_l, _, tone = results['reference']
    print('\nwindow after the change   reference  spillover  cut       spillover - reference')
    for lo, hi in WINDOWS:
        i, j = int(lo * 44.1), int(hi * 44.1)
        row = []
        for name in ('reference', 'spillover', 'cut'):
            row.append(level_db(rms(results[name][0][i:j]) / tone) if name in results else float('nan'))
        sp = results['spillover'][0]
        diff = level_db(rms([x - y for x, y in zip(sp[i:j], ref_l[i:j])]) / tone)
        print('  %4d-%4d ms            %7.1f    %7.1f  %7.1f       %7.1f dB' % (lo, hi, row[0], row[1], row[2], diff))
    if a.trace and 'handover' in results:
        h = results['handover']
        sp = results['spillover'][0]
        print('\nblock  from handover  reference  spillover  difference (dB of the tone)')
        for k in range(-4, 60):
            i = h + 64 * k
            if i < 0:
                continue
            d = level_db(rms([x - y for x, y in zip(sp[i:i + 64], ref_l[i:i + 64])]) / tone)
            print('  %3d  %6.1f ms  %7.1f  %7.1f  %7.1f' % (k, k * BLOCK_MS, level_db(rms(ref_l[i:i + 64]) / tone),
                  level_db(rms(sp[i:i + 64]) / tone), d))
    # clicks: the largest step from one sample to the next in the first 100 ms, against the reference's
    for name in [n for n in ('reference', 'spillover', 'cut') if n in results]:
        xs = results[name][0][:int(100 * 44.1)]
        step = max(abs(xs[k] - xs[k - 1]) for k in range(1, len(xs)))
        print('%-10s largest sample step in the first 100 ms: %.2e' % (name, step))


if __name__ == '__main__':
    main()
