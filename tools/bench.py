"""An effect's cost on the pedal's CPU, in cycles (protocol 11, src/engine/bench.h, docs/bench.md).

    python tools/bench.py TARGET... [--emu FW.bin [--elf ELF]] [--port NAME] [--blocks 1000] [--skip 50]
                          [--cold] [--bypass] [--knob K=V ...] [--level 0.25] [--seed 1] [--bpm 120]
                          [--json OUT.json] [--against EARLIER.json] [--cycles] [--watch]
    python tools/bench.py --list MENU [--emu FW.bin]
    python tools/bench.py --profile SECONDS [--emu FW.bin]     the preset playing, slot by slot

TARGET:
  src/blocks/x.c        a block, built as a module (tools/wvfx.py build) and sent into the pedal's RAM
  out/fx/x.elf          a module ELF (a block of waveod's built as one), sent the same way
  MENU:NAME             an item of the pedal's catalog: AMP:Brit 45, AMP:0x0700002A (id), AMP:#3 (index); the
                        original's modules are there (library), and the built-ins and waveod's library
  MENU:*                every item of the menu
  id:0x0700002A         the item with that id, whatever its menu
  TARGET@K=V,K=V        any of these with its own knob words (over --knob and the defaults)
MENU is a tag (DYN FX WAH DRV AMP CAB IR EQ MOD DLY RVB CLN LOOP SND RTN VOL) or its number.

Each target is loaded on its own in the pedal (the side of the pools the preset playing does not use; nothing is
written to the flash, the preset comes back as it was, the audio is silent meanwhile), runs `--skip` blocks of
the test signal unmeasured and `--blocks` measured, interrupts off around each process. Reported per target:
cycles per 64-frame block (median, 99th percentile, max), the share of a block's time at the core clock, load +
init cycles, the pools' bytes and the CRC of the output. --cold cleans and invalidates the data cache before every
block (the worst case: delay lines and tables come from the SDRAM). --knob K=V sets knob word K (0..14); the
others get the item's defaults (the descriptor's for an ELF).

--profile: the preset playing as it plays (the guitar in, the whole chain), each slot's process timed in the audio
interrupt for SECONDS: mean, max and last cycles per block per slot, and the engine's whole block time. This is
the cost with the chain's real cache state, the number that tells whether a preset fits.

The same command in the emulator (--emu FW.bin) computes the same signal and the same output: --against a JSON
of one run checks the other's CRCs (the pedal computes what the emulator does). In the emulator the cycle counter
steps by a fixed amount per read, so its cycles mean nothing.

--watch: after the first measurement the file targets (.c, .elf) are watched; whenever one is saved (for a .c,
also any .c / .h beside it) it is built again and measured again, each run against the first one: the median's
change and whether the CRC (the output) is the same. A source that does not build shows the compiler's words and
the watch goes on. Ctrl+C ends it. The same loop is the Studio's Desempenho tab with Vigiar.
"""
import argparse
import json
import os
import time
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'studio'))
from waveod_studio import wvlib                                     # noqa: E402
from waveod_studio.protocol import Pedal                            # noqa: E402

from waveod_studio import bench as B                                # noqa: E402

SOURCE_CATALOG, SOURCE_ELF, KNOBS, FS, FRAMES, NAN = B.SOURCE_CATALOG, B.SOURCE_ELF, B.KNOBS, B.FS, B.FRAMES, B.NAN
SECTIONS, CHAIN_SECTION = B.SECTIONS, B.CHAIN_SECTION


def menu_number(tag):
    t = tag.upper()
    if t in wvlib.MENU_TAGS:
        return wvlib.MENU_TAGS.index(t)
    try:
        return int(t, 0)
    except ValueError:
        sys.exit('menu desconhecido: %s (%s)' % (tag, ' '.join(wvlib.MENU_TAGS)))


def file_target(spec):
    """a .c (built by the SDK, waveod_studio.bench.build_source) or a .elf: the target tuple, with the file's path
    last (--watch measures it again when it changes)"""
    try:
        return file_target_live(spec)
    except B.BuildError as e:
        sys.exit('%s\n%s' % (e, e.log))

def targets(pedal, specs):
    """[(label, source, menu, index, elf bytes or None, knob defaults)]"""
    out, cats = [], {}
    for spec in specs:
        sets = {}
        if '@' in spec:                                             # TARGET@K=V,K=V: this target's knobs
            spec, kv = spec.rsplit('@', 1)
            sets = {int(k): float(v) for k, v in (p.split('=') for p in kv.split(',') if p)}
        n = len(out)
        add_target(pedal, spec, out, cats)
        for t in out[n:]:
            for k, v in sets.items():
                t[5][k] = v
    return out


def add_target(pedal, spec, out, cats):
    if spec.endswith('.c') or spec.endswith('.elf'):
        out.append(file_target(spec))
        return
    if ':' not in spec:
        sys.exit('alvo não entendido: %s' % spec)
    tag, what = spec.split(':', 1)
    if tag.lower() == 'id':                                     # id:0x0700002A, in whichever menu has it
        want = int(what, 16)
        for menu in range(len(wvlib.MENU_TAGS)):
            items = cats.get(menu) or cats.setdefault(menu, pedal.catalog(menu))
            if any(it['id'] == want for it in items):
                break
        else:
            sys.exit('id %#010x em nenhum menu' % want)
        what = '0x%x' % want
    else:
        menu = menu_number(tag)
    items = cats.get(menu) or cats.setdefault(menu, pedal.catalog(menu))
    if what == '*':
        pick = [it for it in items if it['kind'] not in ('missing', 'routing')]
    elif what.startswith('#'):
        pick = [it for it in items if it['index'] == int(what[1:])]
    elif what.lower().startswith('0x'):
        pick = [it for it in items if it['id'] == int(what, 16)]
    else:
        pick = [it for it in items if it['name'].lower() == what.lower()] or \
               [it for it in items if what.lower() in it['name'].lower()]
    if not pick:
        sys.exit('nada em %s com %r' % (wvlib.MENU_TAGS[menu] if menu < len(wvlib.MENU_TAGS) else menu, what))
    for it in pick:
        out.append(('%s %s [%s %#010x]' % (wvlib.MENU_TAGS[menu], it['name'], it['kind'], it['id']),
                    SOURCE_CATALOG, menu, it['index'], None, [NAN] * KNOBS))
    return out


def measure(pedal, t, a):
    label, source, menu, index, data, knobs = t[:6]
    knobs = list(knobs)
    for kv in a.knob or []:
        k, v = kv.split('=', 1)
        knobs[int(k)] = float(v)
    try:
        r = B.measure(pedal, source, menu, index, data, knobs, a.blocks, a.skip, a.cold, a.bypass, a.level, a.seed,
                      a.bpm)
    except B.BenchError as e:
        sys.exit(str(e))
    r.pop('budget')
    return dict(label=label, **r)


def report(res, emu):
    block = None
    print('%-46s %9s %9s %9s %7s %10s  %s' % ('', 'mediana', 'p99', 'máx', 'CPU', 'load', 'CRC'))
    for r in res:
        if r['status'] != 0:
            print('%-46s %s' % (r['label'][:46], r['status_text']))
            continue
        block = r['core_hz'] * FRAMES / FS if r['core_hz'] else None
        share = '%6.1f%%' % (100.0 * r['median'] / block) if block and not emu else '    -  '
        print('%-46s %9d %9d %9d %s %10d  %s' % (r['label'][:46], r['median'], r['p99'], r['max'], share, r['load'],
                                                 r['crc']))
    if block and not emu:
        print('(um bloco de %d frames a %.0f MHz = %d ciclos)' % (FRAMES, res[0]['core_hz'] / 1e6, block))
    if emu:
        print('(emulador: os ciclos não valem nada, só o CRC)')


read_profile = B.read_profile


def profile(pedal, seconds, emu_board):
    read_profile(pedal, True)
    if emu_board is not None:                       # the emulator: audio blocks with the main loop between them
        b = emu_board
        for i in range(int(seconds * FS / FRAMES)):
            b.audio_block(i, freq=110.0 * 2 ** ((i // 100) % 4 / 2.0))
            b.run_ms(1.4512)
    else:
        time.sleep(seconds)
    p = read_profile(pedal, False)
    names = {}
    budget = p['core_hz'] * FRAMES / FS if p['core_hz'] else 0
    print('%-4s %-34s %8s %9s %9s %9s %7s' % ('slot', '', 'blocos', 'média', 'máx', 'último', 'CPU'))
    total = 0
    for s in p['slots']:
        if s['menu'] not in names:
            try:
                names[s['menu']] = {it['id']: it['name'] for it in pedal.catalog(s['menu'])}
            except Exception:
                names[s['menu']] = {}
        tag = wvlib.MENU_TAGS[s['menu']] if 0 <= s['menu'] < len(wvlib.MENU_TAGS) else str(s['menu'])
        label = '%s %s' % (tag, names[s['menu']].get(s['id'], '%#010x' % s['id']))
        share = '%6.1f%%' % (100.0 * s['mean'] / budget) if budget else '   -'
        total += s['mean']
        print('%4d %-34s %8d %9d %9d %9d %s' % (s['slot'], label[:34], s['blocks'], s['mean'], s['max'], s['last'],
                                               share))
    if budget:
        print('soma dos slots: %d ciclos = %.1f%% de um bloco (%d ciclos a %.0f MHz); bloco inteiro %.3f ms, pior '
              '%.3f ms' % (total, 100.0 * total / budget, budget, p['core_hz'] / 1e6, p['block_ms'], p['worst_ms']))
    if p['sections']:
        print('o resto da interrupção, por seção (ciclos médios por bloco):')
        whole = sum(p['sections'])
        for k, c in enumerate(p['sections']):
            print('  %-24s %9d' % (SECTIONS[k] if k < len(SECTIONS) else str(k), c))
        if len(p['sections']) > CHAIN_SECTION:
            print('  %-24s %9d   (a cadeia menos a soma dos slots)'
                  % ('entre os slots', p['sections'][CHAIN_SECTION] - total))
        print('  %-24s %9d   (sem a soma dos slots: %d)' % ('total', whole, whole - total))
    return p


def watch(pedal, a, tl, first):
    """--watch: the file targets measured again whenever they change (a .c built again; its own .c / .h files
    beside it count), each run against the first: the median's change and whether the CRC is the same"""
    files = [(k, t) for k, t in enumerate(tl) if len(t) > 6]
    if not files:
        sys.exit('--watch: nenhum alvo é um arquivo (.c / .elf)')
    seen = {k: B.stamp(t[6]) for k, t in files}
    print('vigiando %s; Ctrl+C sai' % ', '.join(os.path.basename(t[6]) for _, t in files))
    try:
        while True:
            time.sleep(0.5)
            for k, t in files:
                s = B.stamp(t[6])
                if s == seen[k]:
                    continue
                seen[k] = s
                time.sleep(0.25)                    # let the editor finish writing
                when = time.strftime('%H:%M:%S')
                try:
                    nt = file_target_live(t[6])
                    nt = nt[:5] + (t[5],) + nt[6:]  # the knobs it was first measured with (TARGET@K=V)
                except B.BuildError as e:
                    print('%s  %s\n%s' % (when, e, e.log))
                    continue
                r = measure(pedal, nt, a)
                f = first[k]
                if r['status']:
                    print('%s  %s: %s' % (when, r['label'], r['status_text']))
                    continue
                d = 100.0 * (r['median'] / f['median'] - 1) if f['median'] else 0.0
                same = 'CRC igual' if r['crc'] == f['crc'] else 'CRC DIFERENTE (%s, antes %s)' % (r['crc'], f['crc'])
                print('%s  %-40s mediana %9d  p99 %9d  %+6.1f%% contra a primeira  %s' % (
                    when, r['label'][:40], r['median'], r['p99'], d, same))
    except KeyboardInterrupt:
        pass


def file_target_live(spec):
    """file_target, raising BuildError instead of leaving (--watch goes on after a build that fails)"""
    data, b = B.module_source(spec)
    m = wvlib.describe(data)
    k = [NAN] * KNOBS
    for p in m.params:
        if p['knob'] < KNOBS:
            k[p['knob']] = p['default']
    return ('%s (%s)' % (m.name, os.path.basename(b['path'] if b else spec)), SOURCE_ELF, m.menu, 0, data, k, spec)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('targets', nargs='*')
    ap.add_argument('--list', metavar='MENU')
    ap.add_argument('--profile', type=float, metavar='SECONDS')
    ap.add_argument('--emu', metavar='FW.bin')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--port')
    ap.add_argument('--blocks', type=int, default=1000)
    ap.add_argument('--skip', type=int, default=50)
    ap.add_argument('--cold', action='store_true')
    ap.add_argument('--bypass', action='store_true')
    ap.add_argument('--knob', action='append')
    ap.add_argument('--level', type=float, default=0.25)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--bpm', type=int, default=120)
    ap.add_argument('--json')
    ap.add_argument('--against')
    ap.add_argument('--cycles', action='store_true', help='print every block\'s cycles too')
    ap.add_argument('--watch', action='store_true', help='measure the file targets again whenever they change')
    a = ap.parse_args()
    if not a.targets and not a.list and not a.profile:
        ap.error('nada a medir')

    board = None
    if a.emu:
        import check_blocks as cb
        import check_editor as ce
        w = ce.Waveod(a.emu, a.elf)
        board = w.b
        pedal = Pedal(transport=cb.Transport(w), timeout=600)
    else:
        pedal = Pedal(port=a.port, timeout=10).__enter__()
    info = pedal.info()
    if info.get('protocol', 0) < 11:
        sys.exit('o firmware do pedal não tem o protocolo 11 (tem %s): grave um firmware waveod novo'
                 % info.get('protocol'))

    if a.list:
        menu = menu_number(a.list)
        for it in pedal.catalog(menu):
            print('  #%-3d %#010x  %-8s %s' % (it['index'], it['id'], it['kind'], it['name']))
        return
    if a.profile:
        p = profile(pedal, a.profile, board)
        if a.json:
            json.dump(dict(emu=bool(a.emu), profile=p), open(a.json, 'w'), indent=1)
        return

    res = []
    tl = targets(pedal, a.targets)
    for t in tl:
        r = measure(pedal, t, a)
        res.append(r)
        if a.cycles and r['cycles']:
            print(r['label'], ' '.join(map(str, r['cycles'])))
    report(res, bool(a.emu))

    if a.against:
        earlier = {r['label']: r for r in json.load(open(a.against))['results']}
        bad = 0
        for r in res:
            e = earlier.get(r['label'])
            if not e or r['status'] or e['status']:
                continue
            same = e['crc'] == r['crc']
            bad += not same
            print('%-5s %s: CRC %s, antes %s' % ('ok' if same else 'DIFF', r['label'], r['crc'], e['crc']))
        print('%d diferença(s) de CRC' % bad)
    if a.json:
        json.dump(dict(emu=bool(a.emu), blocks=a.blocks, skip=a.skip, cold=a.cold, bypass=a.bypass, level=a.level,
                       seed=a.seed, info={k: v for k, v in info.items() if isinstance(v, (int, float, str))},
                       results=res), open(a.json, 'w'), indent=1)
    if a.watch:
        watch(pedal, a, tl, res)


if __name__ == '__main__':
    main()
