"""The controls editor's tables as the new interface sends them (webapp.table_from / mods_from): what is left out,
what is refused, what is clamped. No pedal needed: python tests/test_controls_web.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from waveod_studio import controls as C, i18n, webapp, wvpreset as WP     # noqa: E402

i18n.set_language('pt-BR')                          # the messages checked below are the Portuguese ones

failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


def link(key, modes, when, action, slot=-1, arg=0, curve=0, flags=0, off=0.0, on=0.0):
    return dict(key=key, modes=modes, when=when, action=action, slot=slot, arg=arg, curve=curve, flags=flags, off=off, on=on)


def main():
    page = dict(links=[
        link(C.A, C.STOMP, C.PRESS, C.BLOCK, 2, 0),
        link(C.B, C.STOMP, C.PRESS, C.FN, -1, 0),                      # no destination yet: left out
        link(C.C, 0, C.PRESS, C.FN, -1, 8),                            # in no mode: left out
        link(C.EXP1A, C.ALL, C.PRESS, C.PARAM, 1, 3, C.FAST, 0, 0.0, 10.0),
        link(C.MIDI_KEY + 20, C.ALL, C.PRESS, C.MIDI, 3, 74, 0, 0, 0.0, 127.0),
        link(C.D, C.SCENE, C.HOLD, C.SCENE_ACT, -1, C.SCENE_NEXT),
    ], colour=[1, 9, 0], label=['lead', 'muito comprido', ''])
    t = webapp.table_from(page)
    check([l.key for l in t.links] == [C.A, C.EXP1A, C.MIDI_KEY + 20, C.D], 'unset links and links in no mode left out')
    check(t.label[:2] == ['LEAD', 'MUITO CO'] and t.colour[:2] == [1, 1] and len(t.colour) == C.FOOTSWITCH_KEYS,
          'card names upper case, 8 characters; colours 0-7; six of each: %s %s' % (t.label[:2], t.colour[:2]))
    back = C.Table.decode(t.encode())
    check(back.links == t.links, 'the table the pedal reads is the one sent')
    view = webapp.table_view(t)
    check(webapp.table_from(view).links == t.links, 'table_view / table_from round trip')

    g = webapp.table_from(page, is_global=True)
    check([l.action for l in g.links] == [C.MIDI, C.SCENE_ACT], 'the global table keeps functions, scenes and MIDI')

    for bad, why in ((link(C.A, C.STOMP, C.PRESS, C.PARAM, 11, 40), 'parameter 40 of slot 12'),
                     (link(C.A, C.STOMP, C.PRESS, C.MIDI, 20, 1, 0, 0, 0, 127), 'MIDI channel 21')):
        try:
            webapp.table_from(dict(links=[bad]))
            check(False, 'refused: %s' % why)
        except RuntimeError as e:
            check('não aceitaria' in str(e), 'refused: %s (%s)' % (why, e))
    try:
        webapp.table_from(dict(links=[link(C.A, C.STOMP, C.PRESS, C.FN, -1, 8)] * (C.CONTROL_MAX + 1)))
        check(False, 'more than 48 links refused')
    except RuntimeError as e:
        check(str(C.CONTROL_MAX) in str(e), 'more than 48 links refused: %s' % e)
    try:
        webapp.table_from(dict(links=[dict(key='x')]))
        check(False, 'a malformed link refused')
    except RuntimeError as e:
        check('malformada' in str(e), 'a malformed link refused: %s' % e)

    mods = webapp.mods_from([dict(WP.mod_default('lfo'), rate=99, phase=200, shape='?'),
                             dict(WP.mod_default('env'), attack=0, release=99999, sens=-80),
                             dict(WP.mod_default('seq'), steps=40, step=[150, -3] + [50] * 30)])
    check(len(mods) == WP.MOD_COUNT and mods[3]['type'] == 'off', 'four modulators, the missing one off')
    check(mods[0]['rate'] == 20.0 and mods[0]['phase'] == 95 and mods[0]['shape'] == 'sine', 'LFO clamped: %s' % (
        {k: mods[0][k] for k in ('rate', 'phase', 'shape')}))
    check((mods[1]['attack'], mods[1]['release'], mods[1]['sens']) == (1, 5000, -20), 'envelope clamped')
    check(mods[2]['steps'] == WP.MOD_STEPS and mods[2]['step'][:3] == [100, 0, 50] and len(mods[2]['step']) == WP.MOD_STEPS,
          'sequence clamped')
    pr = WP.Preset()
    pr.set_mods(mods)
    check(pr.mods()[:3] == mods[:3], 'the modulators pack and read back')
    print('%d failed' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
