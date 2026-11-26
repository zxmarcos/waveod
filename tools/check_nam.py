"""NAM captures in the Clone slots (docs/nam.md) in the ARM emulator, against the same code built for the PC.

    python tools/check_nam.py FW.bin MODEL.nam [--elf build/fw/waveod.elf] [--core]

The whole firmware runs in tools/emu.py, reached through its USB MIDI port like waveod Studio:
  1. the .nam goes into Clone slot 1 as a 48 kHz capture and into slot 2 at 44.1 kHz (native rate), both
     written with the original's Clone command as the Studio does; the slots read back with protocol 6 (what each
     holds, as the Studio's NAM tab shows it); slot 3 keeps what it had;
  2. a preset with just the Clone block plays each slot: a tone that changes pitch and level, with Gain and
     Treble moved halfway through. Every call of the NAM block's process is recorded (its input, knobs and
     output) through hooks on the block's init and process;
  3. tools/nam_host.c (the same sources for the PC) replays the calls: the outputs must be the same bits;
  4. the cost: instructions per audio block with the block against the same preset without it;
  5. slot 3 (not a NAM capture) still runs on the original's Clone module (the NAM block is not loaded).
--core: the network alone against NAM's own code (matribox-studio's nam/a2lite.c, which matches
NeuralAmpModelerCore bit for bit) on a DI-like signal, in dB.
Needs unicorn and the host clang (tools/wvbuild.py).
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'studio'))
from unicorn import UC_HOOK_CODE                            # noqa: E402
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_LR   # noqa: E402
import check_blocks as cb                                   # noqa: E402
import check_editor as ce                                   # noqa: E402
import emu                                                  # noqa: E402
import mkfw                                                 # noqa: E402
import wvbuild                                              # noqa: E402
from waveod_studio import nam, protocol, wvpreset           # noqa: E402
from waveod_studio.protocol import Pedal                    # noqa: E402
from waveod_studio.usb import htmidi                        # noqa: E402

MENU_CLONE = 11
K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE = range(5)
BLOCKS = 240                        # audio blocks played per slot (0.35 s)
PRESET = 22                         # where the test preset is stored
failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def clone_ids(fw):
    """the item ids of the 10 Clone slots: the last 10 entries of the IR directory (section 2: header {u16 0x5AA5,
    .., u8 count @0xA}, 0x48-byte entries from +0x10 {u16 index, u16 block, u32 id, name[16]})"""
    _, secs = mkfw.read_frmw(open(fw, 'rb').read())
    d = secs[2]
    n = d[0x0A]
    return [struct.unpack_from('<I', d, 0x10 + 0x48 * i + 4)[0] for i in range(n - 10, n)]


def with_clone(data, item_id, knobs):
    """preset `data` (without blocks) with one Clone block"""
    pr = wvpreset.Preset.decode(data)
    k = [0.0] * wvpreset.KNOBS
    for i, v in knobs.items():
        k[i] = float(v)
    body = struct.pack('<BBbhIB', 0, 1, MENU_CLONE, 0, item_id, wvpreset.KNOBS) + struct.pack('<15f', *k)
    pr.chunks = [(t, b) for t, b in pr.chunks if not (t == wvpreset.SLOT and b[0] == 0)]
    pr.chunks.append((wvpreset.SLOT, body))
    main = bytearray(pr.chunk(wvpreset.MAIN))
    main[6] = 1
    main[7] = 0
    pr.set_chunk(wvpreset.MAIN, main)
    pr.name = 'CLONE'
    return pr.encode()


class Recorder:
    """every call of the NAM block's init and process, through code hooks"""

    def __init__(self, b):
        self.b, uc = b, b.uc
        desc = b.sym('nam_block')
        self.init_at, self.process_at = (struct.unpack('<I', uc.mem_read(desc + o, 4))[0] & ~1 for o in (36, 40))
        self.inits, self.calls, self.pending, self.ret_hook = 0, [], None, None
        uc.hook_add(UC_HOOK_CODE, self.on_init, begin=self.init_at, end=self.init_at)
        uc.hook_add(UC_HOOK_CODE, self.on_process, begin=self.process_at, end=self.process_at)

    def floats(self, addr, n):
        return list(struct.unpack('<%df' % n, self.b.uc.mem_read(addr, 4 * n)))

    def on_init(self, uc, addr, size, ud):
        self.inits += 1
        self.calls = []

    def on_process(self, uc, addr, size, ud):
        io = uc.reg_read(UC_ARM_REG_R1)
        p_io, _aux, _s, _r, p_knobs, _beat, frames, enabled = struct.unpack('<6IHB', uc.mem_read(io, 27))
        self.pending = dict(frames=frames, enabled=enabled, knobs=self.floats(p_knobs, 15),
                            input=self.floats(p_io, 2 * frames), at=p_io)
        if self.ret_hook is None:
            ret = uc.reg_read(UC_ARM_REG_LR) & ~1
            self.ret_hook = uc.hook_add(UC_HOOK_CODE, self.on_return, begin=ret, end=ret)

    def on_return(self, uc, addr, size, ud):
        if self.pending:
            c = self.pending
            c['output'] = self.floats(c['at'], 2 * c['frames'])
            self.calls.append(c)
            self.pending = None


def set_knob(pedal, w, knob, value):
    """the edited preset's slot 0 knob (the editor's knob write, 0x01050001 {slot, knob, value})"""
    payload = struct.pack('<IIIf', 0x01050001, 0, knob, value)
    for pk in htmidi.packets(htmidi.message(0x10000001, payload), htmidi.FLAG_DATA):
        pedal.port.send(pk)
    w.b.run_ms(20)


def play(w, pedal, rec, index, knobs_mid=None, count=0):
    """load preset `index`, play the tone; returns the median instructions of `count` blocks"""
    pedal.load(cb.EMPTY)
    pedal.load(index)
    b = w.b
    cost = []
    for i in range(BLOCKS):
        f = 110.0 * 2 ** ((i // 30) % 5 / 2.0)
        level = (0.05, 0.2, 0.5)[(i // 40) % 3]
        n, _ = b.audio_block(w.phase, freq=f, level=level, count=i >= BLOCKS - count)
        w.phase += 1
        if i >= BLOCKS - count:
            cost.append(n)
        b.run_ms(1.4512)
        if knobs_mid and i == BLOCKS // 2:
            for k, v in knobs_mid.items():
                set_knob(pedal, w, k, v)
    if b.faults:
        sys.exit('FAULT: ' + '; '.join(b.faults))
    return sorted(cost)[len(cost) // 2] if cost else 0


def build_host(tmp):
    exe = os.path.join(tmp, 'nam_host.exe')
    src = lambda p: os.path.join(ROOT, 'src', p)
    return wvbuild.host_compile(exe, [os.path.join(HERE, 'nam_host.c'), src('nam/a2.c'), src('nam/rs48.c'),
                                      src('engine/nam_block.c'), src('engine/pools.c'), src('fw/crc32.c')],
                                ['-I', os.path.join(ROOT, 'src')], cwd=tmp)


def replay(exe, cap, calls, init_knobs, tmp, tag):
    cp, cl, out = (os.path.join(tmp, tag + s) for s in ('.cap', '.calls', '.out'))
    open(cp, 'wb').write(cap)
    with open(cl, 'wb') as f:
        f.write(struct.pack('<I15f', len(calls), *init_knobs))
        for c in calls:
            f.write(struct.pack('<HBB15f', c['frames'], c['enabled'], 0, *c['knobs']))
            f.write(struct.pack('<%df' % len(c['input']), *c['input']))
    r = subprocess.run([exe, 'block', cp, cl, out], capture_output=True, text=True)
    if r.returncode:
        sys.exit('nam_host: ' + r.stderr)
    d = open(out, 'rb').read()
    return list(struct.unpack('<%df' % (len(d) // 4), d))


def core_check(model, cap, exe, tmp):
    """the network alone against NAM's own code (matribox-studio/nam/a2lite.c built for the PC)"""
    sys.path.insert(0, os.path.join(os.path.dirname(ROOT), 'matribox-studio', 'nam'))
    try:
        import numpy as np
        import test_a2
    except ImportError as ex:
        print('skip  --core: %s' % ex)
        return
    gen = os.path.join(tmp, 'model_w')
    subprocess.run([sys.executable, '-m', 'matribox.nam.nam_a2', model, gen], check=True, capture_output=True)
    old = test_a2.build_host(gen + '.c', 'a2lite.c')
    x = test_a2.test_signal().astype(np.float32)
    ref = test_a2.render(old, x.astype(np.float64), 'core').astype(np.float64)
    xi, yo = os.path.join(tmp, 'x.f32'), os.path.join(tmp, 'y.f32')
    x.tofile(xi)
    cp = os.path.join(tmp, 'core.cap')
    open(cp, 'wb').write(cap)
    subprocess.run([exe, 'net', cp, xi, yo], check=True)
    y = np.fromfile(yo, np.float32).astype(np.float64)
    e = 20 * math.log10(np.sqrt(np.mean((y - ref) ** 2)) / np.sqrt(np.mean(ref ** 2)))
    check(e < -95, 'the network against NAM\'s own code on %s: %.1f dB' % (os.path.basename(model), e))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('model')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--core', action='store_true')
    a = ap.parse_args()
    cap48, info = nam.capture(a.model)
    cap44, _ = nam.capture(a.model, native_rate=True)
    print('%s: %d weights, %s, loudness %s, %d bytes' % (os.path.basename(a.model), info['weights'],
                                                       info['runs_at'], info['loudness'], info['bytes']))
    ids = clone_ids(a.fw)
    with tempfile.TemporaryDirectory() as tmp:
        exe = build_host(tmp)
        if a.core:
            core_check(a.model, cap48, exe, tmp)
        w = ce.Waveod(a.fw, a.elf)
        pedal = Pedal(transport=cb.Transport(w), timeout=60)
        rec = Recorder(w.b)
        pedal.write_clone(0, 'NAM 48k', cap48)
        pedal.write_clone(1, 'NAM 44k', cap44)
        names = pedal.clone_names()
        check(names[:2] == ['NAM 48k', 'NAM 44k'], 'Clone slots written over USB: %s' % names[:3])
        slots = pedal.clones()
        kinds = [(s['kind'], s['name'], round(s.get('sample_rate', 0))) for s in slots[:3]]
        check(kinds[:2] == [(protocol.CLONE_NAM_48K, 'NAM 48k', 48000), (protocol.CLONE_NAM, 'NAM 44k', 48000)] and
              kinds[2][0] in (protocol.CLONE_EMPTY, protocol.CLONE_ORIGINAL),
              'the slots as the Studio reads them (protocol 6): %s' % [protocol.CLONE_KINDS[k] for k, _, _ in kinds])
        _, data = pedal.get(0)
        base = cb.empty(data)
        pedal.put(cb.EMPTY, base)
        knobs = {K_GAIN: 50, K_VOLUME: 50, K_BASS: 50, K_MIDDLE: 50, K_TREBLE: 50}
        plain = play(w, pedal, rec, cb.EMPTY, count=6)
        for slot, cap, what in ((0, cap48, '48 kHz'), (1, cap44, '44.1 kHz')):
            pedal.put(PRESET, with_clone(base, ids[slot], knobs))
            inits = rec.inits
            cost = play(w, pedal, rec, PRESET, {K_GAIN: 72, K_TREBLE: 35}, count=6)
            calls = rec.calls
            check(rec.inits == inits + 1 and len(calls) >= BLOCKS - 10,
                  '%s: the NAM block loaded once, %d calls recorded' % (what, len(calls)))
            out = replay(exe, cap, calls, [float(knobs.get(i, 0)) for i in range(15)], tmp, 'slot%d' % slot)
            arm = [v for c in calls for v in c['output']]
            peak = max(abs(v) for v in arm)
            same = out == arm
            moved = any(c['knobs'][K_GAIN] == 72 for c in calls)
            diff = '' if same else ', first difference at sample %d' % next(i for i, (p, q) in enumerate(zip(out, arm))
                                                                           if p != q)
            check(same and peak > 0.01 and moved, '%s: the PC build gives the emulator\'s bits (%d samples, peak %.3f, '
                  'knobs moved: %s)%s' % (what, len(arm), peak, moved, diff))
            print('      %s: %d instructions per audio block, %+d against the preset without it' %
                  (what, cost, cost - plain))
        # slot 3, not a NAM capture: the original's module, not the NAM block
        pedal.put(PRESET + 1, with_clone(base, ids[2], knobs))
        inits = rec.inits
        play(w, pedal, rec, PRESET + 1)
        _, blocks = pedal.describe(PRESET + 1)
        check(rec.inits == inits, 'slot 3 (%s): the original\'s Clone module, %s' %
              (protocol.CLONE_KINDS[kinds[2][0]], [b.name for b in blocks]))
    print('%d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
