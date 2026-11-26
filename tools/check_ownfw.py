"""waveod's own effects in the update file (tools/portfw.py puts them in section 1), in the ARM emulator.

    python tools/check_ownfw.py out/waveod.bin [--elf build/fw/waveod.elf]

FW.bin is a firmware made by tools/portfw.py (its module ELFs in out/portfw/). With an empty waveod library:
  - every own effect is in its menu's catalog once, with its name;
  - a preset with it plays (finite, not silent), the pedal names the block, and its knobs come with the options
    and units of the module's descriptor (fw/library.c reads them in section 1 by the id);
then with a library holding one of them again (the Efeitos tab installed it before), the catalog still lists it
once (fw/catalog.c: the firmware's copy).
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_blocks as cb                                   # noqa: E402
import check_modfx                                          # noqa: E402
import portfw                                               # noqa: E402
from waveod_studio import wvlib                             # noqa: E402

failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(cb.emu.ROOT, 'build', 'fw', 'waveod.elf'))
    a = ap.parse_args()
    own = portfw.own_blocks()
    mods = {}
    for name in own:
        path = os.path.join(cb.emu.ROOT, 'out', 'portfw', name + '.elf')
        if not os.path.exists(path):
            sys.exit('%s: falta (rode tools/portfw.py antes)' % path)
        mods[name] = wvlib.describe(open(path, 'rb').read())

    cb.TONE_BLOCKS = 60
    w = cb.ce.Waveod(a.fw, a.elf)
    p = cb.Pedal(transport=cb.Transport(w), timeout=120)
    play = cb.Player(w)
    _, data = p.get(0)
    base = cb.empty(data)
    p.put(cb.EMPTY, base)
    p.write_library(wvlib.Library().pack())                 # the emulated flash: no library of the user's
    cats = {}

    def listed(menu, id_):
        if menu not in cats:
            cats[menu] = p.catalog(menu)
        return [x['name'] for x in cats[menu] if x['id'] == id_]

    for i, (name, m) in enumerate(sorted(mods.items())):
        names = listed(m.menu, m.id)
        check(names == [m.name], '%-15s in %s\'s catalog once: %s' % (name, wvlib.MENU_TAGS[m.menu], names))
        p.put(30, check_modfx.with_block(base, m, m.menu))
        _, blocks = p.describe(30)
        l, r, _cost = play.run(p, 30, count=True)
        rms = math.sqrt(sum(v * v for v in l) / len(l))
        finite = all(math.isfinite(v) for v in l + r)
        slot = next(b['slot'] for b in p.rig()['blocks'] if b['id'] == m.id)
        got = p.block(slot)
        want_opts = [q['options'] or [] for q in m.params]
        have_opts = [q['options'] for q in got]
        # a unit is the value's printf format ("%.2f Hz"): the pedal's text is the value through it
        units = [(q['unit'], g) for q, g in zip(m.params, got) if q['unit'] and not g['synced']]
        bad = [(g['name'], g['text'], u % g['value']) for u, g in units if g['text'] != u % g['value']]
        check([b.name for b in blocks] == [m.name] and finite and rms > 1e-4 and have_opts == want_opts and not bad,
              '%-15s plays (%.1f dBFS), %d knobs, options %s, units %d%s' % (
                  name, 20 * math.log10(rms + 1e-30), len(got), sum(1 for o in have_opts if o) or '-', len(units),
                  ' wrong: %s' % bad if bad else ''))

    chorus = mods.get('chorus')
    if chorus:
        p.write_library(_one(chorus))
        cats.clear()
        check(listed(chorus.menu, chorus.id) == [chorus.name],
              'Chorus installed in the library too: listed once (%d in MOD)' % len(cats[chorus.menu]))
    if w.b.faults:
        check(False, 'faults: ' + '; '.join(w.b.faults))
    print('%d failed' % failures)
    return 1 if failures else 0


def _one(m):
    lib = wvlib.Library()
    lib.add(m.elf)
    return lib.pack()


if __name__ == '__main__':
    sys.exit(main())
