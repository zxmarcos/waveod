"""A waveod block module (tools/wvfx.py build) on its own in the ARM emulator: its regions placed and relocated as
engine/module_loader.c does, then init, process and poll called with the buffers of tools/circuits/wvfx_host.c, so
the two outputs compare sample for sample.

    python tools/wvfx_emu.py out/fx/dark_muff.elf in.f32 out.f32 [knobs...] [--count N]

--count N: instructions per block (process, and poll after it) measured over the first N blocks (slow: a hook per
instruction); printed as the median and the largest.
"""
import argparse
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, \
    UC_ARM_REG_R3, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_MAX

FRAMES = 64
KNOBS = 15
BASE = {'ER_IROM': 0x00058000, 'ER_SROM': 0x81301000, 'ER_IRAM': 0x20204000, 'ER_SRAM': 0x81321000}
STATE, IOBUF, STACK_TOP, RETURN_STUB = 0x20210000, 0x20260000, 0x20007000, 0x0007fff0
REGIONS = ['ER_IROM', 'ER_SROM', 'ER_IRAM', 'ER_SRAM']


class Module:
    def __init__(self, elf):
        b = elf
        shoff, = struct.unpack_from('<I', b, 32)
        shnum, shstrndx = struct.unpack_from('<HH', b, 48)
        secs = [struct.unpack_from('<10I', b, shoff + 40 * i) for i in range(shnum)]
        so = secs[shstrndx][4]
        names = [b[so + s[0]:b.index(b'\0', so + s[0])].decode() for s in secs]
        self.regions = {}
        for n, s in zip(names, secs):
            if n in REGIONS:
                self.regions[n] = (s[3], s[5], None if s[1] == 8 else bytearray(b[s[4]:s[4] + s[5]]))
        symtab = secs[names.index('.symtab')]
        syms = [struct.unpack_from('<IIIBBH', b, symtab[4] + 16 * j) for j in range(symtab[5] // 16)]
        self.rels = []
        rel = secs[names.index('.rel.dyn')]
        for j in range(rel[5] // 8):
            P, info = struct.unpack_from('<II', b, rel[4] + 8 * j)
            shndx = syms[info >> 8][5]
            self.rels.append((P, names[shndx]))

    def place(self, uc):
        for n, (addr, size, data) in self.regions.items():
            if data is None:
                uc.mem_write(BASE[n], b'\0' * size)
        for P, target in self.rels:                    # the word holds the offset in its region
            src = REGIONS[P >> 28]
            addr, size, data = self.regions[src]
            o = P - addr
            v, = struct.unpack_from('<I', data, o)
            struct.pack_into('<I', data, o, (v + BASE[target]) & 0xffffffff)
        for n, (addr, size, data) in self.regions.items():
            if data is not None:
                uc.mem_write(BASE[n], bytes(data))


class Runner:
    def __init__(self, path):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        for base, size in ((0, 0x80000), (0x20000000, 0x20000), (0x20200000, 0x80000), (0x80000000, 0x2000000)):
            uc.mem_map(base, size)
        uc.mem_write(RETURN_STUB, b'\xfe\xe7')
        m = Module(open(path, 'rb').read())
        m.place(uc)
        desc, = struct.unpack('<I', uc.mem_read(BASE['ER_IROM'] + 12, 4))
        d = uc.mem_read(desc, 48)
        self.nparams = d[13]
        params, self.state_size, self.buffer_size, self.init, self.process, self.poll = struct.unpack_from('<6I', d, 24)
        self.defaults = []
        for i in range(self.nparams):                 # struct wvfx_param: name, min, max, step, def, knob, ...
            p = uc.mem_read(params + 32 * i, 32)
            self.defaults.append((p[20], struct.unpack_from('<f', p, 16)[0]))
        self.buffer = 0x81400000
        self.knobs = IOBUF
        self.io = IOBUF + 0x100
        self.aux = self.io + 0x400
        self.send = self.aux + 0x400
        self.ret = self.send + 0x400
        self.iostruct = self.ret + 0x400
        self.env = self.iostruct + 0x40

    profile = None                                      # {pc: executions}, when profiling

    def call(self, fn, *args, count=False):
        uc = self.uc
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(reg, v)
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, RETURN_STUB | 1)
        n = [0]
        prof = self.profile

        def hook(uc_, addr, size, data):
            n[0] += 1
            if prof is not None:
                prof[addr] = prof.get(addr, 0) + 1
        h = uc.hook_add(UC_HOOK_CODE, hook) if count else None
        try:
            uc.emu_start(fn | 1, RETURN_STUB, count=500_000_000)
        finally:
            if h is not None:
                uc.hook_del(h)
        return n[0], uc.reg_read(UC_ARM_REG_R0)

    def start(self, knobs):
        uc = self.uc
        uc.mem_write(STATE, b'\0' * max(self.state_size, 4))
        uc.mem_write(self.buffer, b'\0' * max(self.buffer_size, 4))
        uc.mem_write(self.knobs, struct.pack('<15f', *knobs))
        data, at = b'', 0
        if os.environ.get('WVFX_DATA'):                     # the item's data (a Clone capture, an IR): env.data
            data, at = open(os.environ['WVFX_DATA'], 'rb').read(), 0x81E00000     # past ER_SROM and the buffer
            uc.mem_write(at, data)
        uc.mem_write(self.env, struct.pack('<IfHBBII', 1, 44100.0, FRAMES, 0, 0, at, len(data)))
        uc.mem_write(self.ret, b'\0' * 0x200)
        _, r = self.call(self.init, STATE, self.buffer, self.env, self.knobs)
        return r

    def block(self, samples, knobs, enabled=True, count=False):
        uc = self.uc
        uc.mem_write(self.knobs, struct.pack('<15f', *knobs))
        uc.mem_write(self.io, struct.pack('<128f', *samples))
        uc.mem_write(self.aux, struct.pack('<128f', *samples))
        uc.mem_write(self.iostruct, struct.pack('<6IHBB', self.io, self.aux, self.send, self.ret, self.knobs, 500,
                                                FRAMES, int(enabled), 0))
        n, _ = self.call(self.process, STATE, self.iostruct, count=count)
        m = 0
        if self.poll:
            m, _ = self.call(self.poll, STATE, self.knobs, count=count)
        return list(struct.unpack('<128f', uc.mem_read(self.io, 512))), n, m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('module')
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('knobs', nargs='*', type=float)
    ap.add_argument('--count', type=int, default=0)
    ap.add_argument('--profile', action='store_true', help='with --count: the instruction mix, an M7 cycle estimate '
                                                          'and the hottest code')
    a = ap.parse_args()
    r = Runner(a.module)
    if a.profile:
        r.profile = {}
    k = [0.0] * KNOBS
    for idx, v in r.defaults:
        k[idx] = v
    for (idx, _), v in zip(r.defaults, a.knobs):
        k[idx] = v
    if r.start(k):
        sys.exit('init failed')
    raw = open(a.input, 'rb').read()
    n = len(raw) // 4
    x = struct.unpack('<%df' % n, raw)
    out, costs = [], []
    for blk in range(0, n, 2 * FRAMES):
        s = list(x[blk:blk + 2 * FRAMES])
        got = len(s)
        s += [0.0] * (2 * FRAMES - got)
        y, c, p = r.block(s, k, count=blk // (2 * FRAMES) < a.count)
        if blk // (2 * FRAMES) < a.count:
            costs.append((c, p))
        out += y[:got]
    open(a.output, 'wb').write(struct.pack('<%df' % len(out), *out))
    if costs:
        cs = sorted(c for c, _ in costs)
        print('process: median %d, largest %d instructions per block over %d blocks; poll %d' % (
            cs[len(cs) // 2], cs[-1], len(cs), max(p for _, p in costs)))
    if r.profile:
        report(r, len(costs))


# Cortex-M7 issue costs (cycles) by instruction class, single precision: the ARM Cortex-M7 TRM's FPU timings
# (VDIV / VSQRT 14, not pipelined; VLDR / VSTR one a cycle, dual-issued with arithmetic in the best case; a taken
# branch ~2 with the predictor). An estimate for comparing code, not a cycle count.
CLASSES = [
    ('fp divide / sqrt', ('vdiv', 'vsqrt'), 14.0),
    ('fp multiply-add', ('vmla', 'vmls', 'vfma', 'vfms', 'vnmla', 'vnmls', 'vfnma', 'vfnms'), 1.0),
    ('fp add / mul', ('vadd', 'vsub', 'vmul', 'vnmul', 'vneg', 'vabs'), 1.0),
    ('fp compare / select / convert', ('vcmp', 'vcmpe', 'vmrs', 'vseleq', 'vselge', 'vselgt', 'vselvs', 'vmaxnm', 'vminnm', 'vcvt', 'vcvtr', 'vrintm', 'vrintz', 'vrinta'), 1.0),
    ('fp load / store', ('vldr', 'vstr', 'vldm', 'vstm', 'vldmia', 'vstmia', 'vldmdb', 'vstmdb', 'vpush', 'vpop'), 1.0),
    ('fp move', ('vmov',), 1.0),
    ('branch', ('b', 'bl', 'blx', 'bx'), 1.5),
    ('it', ('it',), 0.5),
    ('integer load / store', ('ldr', 'str', 'ldrd', 'strd', 'ldrb', 'strb', 'ldrh', 'strh', 'ldm', 'stm', 'ldmia', 'stmia', 'push', 'pop'), 1.0),
]


CONDS = ('eq', 'ne', 'cs', 'hs', 'cc', 'lo', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al')


def classify_name(mnemonic):
    """'vmul.f32' -> 'vmul', 'bne.w' -> 'b', 'ldr.w' -> 'ldr', 'itte' -> 'it', 'adds' -> 'add'"""
    m = mnemonic.split('.')[0]
    if m.startswith('it') and set(m[2:]) <= {'t', 'e'}:
        return 'it'
    for c in CONDS:
        if m.endswith(c) and len(m) > len(c) and m[:-len(c)] in ('b', 'bx', 'bl', 'vmov', 'mov', 'add', 'sub'):
            return m[:-len(c)]
    if m in ('cbz', 'cbnz', 'tbb', 'tbh'):
        return 'b'
    return m


def report(r, blocks):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    total = sum(r.profile.values())
    per_class, cyc, mnem = {}, 0.0, {}
    hot = sorted(r.profile.items(), key=lambda t: -t[1])
    for addr, n in r.profile.items():
        code = bytes(r.uc.mem_read(addr, 4))
        ins = next(md.disasm(code, addr, 1), None)
        m = classify_name(ins.mnemonic if ins else '?')
        mnem[m] = mnem.get(m, 0) + n
        name, cost = 'integer / other', 1.0
        for cname, names, c in CLASSES:
            if m in names:
                name, cost = cname, c
                break
        per_class[name] = per_class.get(name, 0) + n
        cyc += n * cost
    print('per block: %d instructions, about %d M7 cycles (estimate)' % (total // blocks, cyc / blocks))
    for name, n in sorted(per_class.items(), key=lambda t: -t[1]):
        print('  %-32s %5.1f %%' % (name, 100.0 * n / total))
    print('  top mnemonics: ' + ', '.join('%s %.1f%%' % (m, 100.0 * n / total)
                                         for m, n in sorted(mnem.items(), key=lambda t: -t[1])[:12]))


if __name__ == '__main__':
    main()
