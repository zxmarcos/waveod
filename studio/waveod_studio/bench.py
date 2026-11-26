"""An effect's cost on the pedal's CPU (protocol 11, src/editor/wv_bench_msgs.c, docs/bench.md): the bench (one
effect on its own, the audio silent meanwhile) and the profile (the preset playing, each slot timed in the audio
interrupt). Used by the Studio's Desempenho screen and by waveod/tools/bench.py."""
import os
import struct
import subprocess
import sys
import time

BENCH_ELF, BENCH, PROFILE = 0x1F00001B, 0x1F00001C, 0x1F00001D
PROTOCOL = 11
SOURCE_CATALOG, SOURCE_ELF = 0, 1
FLAG_COLD, FLAG_BYPASS = 1, 2
ELF_CHUNK = 4096
ELF_MAX = 0x10000                                   # src/engine/bench.h BENCH_ELF_MAX
MAX_BLOCKS = 2000                                   # BENCH_MAX_BLOCKS
KNOBS = 15
HEAD = 68 + 4 * KNOBS
FS, FRAMES = 44100, 64
NAN = float('nan')
STATUS = {0: 'ok', -1: 'ELF inválido', -2: 'sem memória nos pools', -3: 'relocação inválida', -4: 'cabeçalho inválido',
          -5: 'init recusou', -10: 'pedido inválido', -11: 'ELF não enviado'}
POOLS = ('code', 'const', 'state', 'buffer')
# enum engine_section (src/engine/audio_engine.h), in order
SECTIONS = ['conversores -> floats', 'filtro de entrada', 'USB playback', 'buffers / afinador', 'antes da cadeia',
            'cadeia', 'entre cadeia e nível', 'nível / gravação USB', 'EQ global', 'bateria', 'AUX / USB',
            'filtro de saída', 'filtro do send', 'DAC', 'latência / estatísticas']
CHAIN_SECTION = 5                                   # ESEC_CHAIN: the slots run inside it


class BenchError(RuntimeError):
    pass


class BuildError(BenchError):
    """a block's source that did not build: the compiler's (or the SDK's) words in `log`"""
    def __init__(self, message, log):
        super().__init__(message)
        self.log = log


def waveod_root():
    """the waveod tree (its tools/wvfx.py and src/sdk): WAVEOD_ROOT, else the one this app lives in"""
    return os.environ.get('WAVEOD_ROOT') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


def build_source(src, out_dir=None):
    """a block's C source built into a module ELF by the SDK (tools/wvfx.py build: the Arm Toolchain for
    Embedded's clang, src/sdk); dict(elf=bytes, path, log, seconds). Raises BuildError with the compiler's words."""
    root = os.path.abspath(waveod_root())
    tool = os.path.join(root, 'tools', 'wvfx.py')
    if not os.path.exists(tool):
        raise BuildError('o SDK do waveod não foi encontrado (%s): defina WAVEOD_ROOT' % tool, '')
    stem = os.path.splitext(os.path.basename(src))[0]
    out = os.path.join(out_dir or os.path.join(root, 'out', 'bench'), stem + '.elf')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    t0 = time.monotonic()
    r = subprocess.run([sys.executable, '-I', tool, 'build', os.path.abspath(src), '-o', out], capture_output=True,
                       text=True, encoding='utf-8', errors='replace', cwd=root,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    log = (r.stdout + r.stderr).strip()
    if r.returncode:
        lines = log.splitlines()
        if lines and lines[0].startswith('failed: '):      # wvfx.py's own line: the whole command
            lines = lines[1:]
        raise BuildError('%s não compilou' % os.path.basename(src), '\n'.join(lines).strip() or log)
    with open(out, 'rb') as f:
        return dict(elf=f.read(), path=out, log=log, seconds=time.monotonic() - t0)


def stamp(path):
    """when a module last changed: a .elf's time; a .c's, or of the .c / .h files beside it (a block's own headers)"""
    if not path.lower().endswith('.c'):
        return os.path.getmtime(path)
    d = os.path.dirname(os.path.abspath(path))
    times = [os.path.getmtime(path)]
    for n in os.listdir(d):
        if n.lower().endswith(('.h', '.c')):
            try:
                times.append(os.path.getmtime(os.path.join(d, n)))
            except OSError:
                pass
    return max(times)


def module_source(path):
    """the module of a .elf as it is, or of a .c built now: (elf bytes, build dict or None)"""
    if path.lower().endswith('.c'):
        b = build_source(path)
        return b['elf'], b
    with open(path, 'rb') as f:
        return f.read(), None


def block_cycles(core_hz):
    """the cycles of one 64-frame block at the core clock (0 when the pedal did not tell it)"""
    return core_hz * FRAMES / FS if core_hz else 0


def send_elf(pedal, data):
    """a module ELF into the pedal's RAM for the next bench (never the flash)"""
    if not data or len(data) > ELF_MAX:
        raise BenchError('o módulo tem %d bytes: o pedal mede até %d KB' % (len(data), ELF_MAX // 1024))
    for off in range(0, len(data), ELF_CHUNK):
        part = data[off:off + ELF_CHUNK]
        r = pedal.request(BENCH_ELF, struct.pack('<III', off, len(data), len(part)) + part, timeout=10.0)
        got, err = struct.unpack_from('<Ii', r)
        if got != off or err:
            raise BenchError('o pedal não aceitou o ELF em %#x: %s' % (off, STATUS.get(err, err)))


def measure(pedal, source, menu, index=0, elf=None, knobs=None, blocks=1000, skip=50, cold=False, bypass=False,
            level=0.25, seed=1, bpm=120, timeout=None):
    """one bench: the item `index` of `menu` (SOURCE_CATALOG) or the ELF given (SOURCE_ELF), `skip` blocks of the
    test signal unmeasured, then `blocks` measured. knobs: KNOBS words, NaN = the item's default."""
    if elf is not None:
        send_elf(pedal, elf)
    k = list(knobs) if knobs is not None else [NAN] * KNOBS
    k = (k + [NAN] * KNOBS)[:KNOBS]
    blocks = max(1, min(MAX_BLOCKS, int(blocks)))
    flags = (FLAG_COLD if cold else 0) | (FLAG_BYPASS if bypass else 0)
    req = struct.pack('<IiiIIIfII', source, menu, index, blocks, int(skip), flags, level, seed, bpm) + \
        struct.pack('<%df' % KNOBS, *k)
    r = pedal.request(BENCH, req, timeout=timeout or (blocks + skip) * 0.004 + 30.0)   # answered when it is done
    (status, load, overhead, n, mn, med, p99, mx, mean, crc, peak, c0, c1, c2, c3, hz, _) = \
        struct.unpack_from('<iIIIIIIIfIfIIIIII', r)
    got_knobs = list(struct.unpack_from('<%df' % KNOBS, r, 68))
    cycles = list(struct.unpack_from('<%dI' % n, r, HEAD)) if n and len(r) >= HEAD + 4 * n else []
    return dict(status=status, status_text=STATUS.get(status, str(status)), load=load, overhead=overhead, blocks=n,
                min=mn, median=med, p99=p99, max=mx, mean=mean, crc='%08x' % crc, peak=peak,
                pools=dict(zip(POOLS, (c0, c1, c2, c3))), core_hz=hz, budget=block_cycles(hz), knobs=got_knobs,
                cold=bool(cold), bypass=bool(bypass), cycles=cycles)


def read_profile(pedal, reset):
    """the slots' counts since the last reset (and the engine's sections); reset: counted afresh after this answer"""
    r = pedal.request(PROFILE, struct.pack('<I', 1 if reset else 0))
    hz, block_ms, worst_ms, blocks, n = struct.unpack_from('<IffII', r)
    slots = []
    for k in range(n):
        slot, menu, _, id_, nb, mean, mx, last = struct.unpack_from('<BbHIIIII', r, 20 + 24 * k)
        slots.append(dict(slot=slot, menu=menu, id=id_, blocks=nb, mean=mean, max=mx, last=last))
    sections = []
    at = 20 + 24 * n
    if len(r) >= at + 8:                            # firmware with the interrupt's sections
        _, m = struct.unpack_from('<II', r, at)
        sections = list(struct.unpack_from('<%dI' % m, r, at + 8))
    return dict(core_hz=hz, budget=block_cycles(hz), block_ms=block_ms, worst_ms=worst_ms, blocks=blocks,
                slots=slots, sections=sections)
