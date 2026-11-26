"""waveod Studio against the real firmware running in the emulator (tools/emu.py through check_editor's
USB host): info, list, get / put, move, copy, load, describe and the control table, flash reads, backup and
restore, the latency test, the chain screen's state and live edits (protocol 8), the GLOBAL screen's settings (9), the scene menu's edits (10), the FX Loop / SND / RTN blocks.

    python tests/test_emulator.py WAVEOD.bin [--elf waveod.elf]
"""
import argparse
import struct
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WAVEOD = os.path.join(HERE, '..', '..')
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(WAVEOD, 'tools'))
sys.path.insert(0, HERE)

import check_editor as ce                           # noqa: E402
from waveod_studio.usb import htmidi                # noqa: E402
from waveod_studio import backup, controls, drums, wvpreset  # noqa: E402
import test_drums                                   # noqa: E402  (its MIDI file writer)
from waveod_studio import protocol as PR            # noqa: E402
from waveod_studio.protocol import DESCRIBE, LAT_ROUTES, Pedal, PedalError  # noqa: E402

ce.htmidi = htmidi
failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


class EmuTransport:
    """send() / receive() of htmidi.Port over the emulated pedal's USB MIDI endpoints"""
    def __init__(self, w):
        self.w, self.stream = w, bytearray()

    def send(self, sysex):
        self.w.send(sysex)

    def receive(self):
        self.w.b.run_ms(5)
        while True:
            d = self.w.u.transfer_in(0x83)
            if d is None:
                break
            for i in range(0, len(d) - 3, 4):
                self.stream += d[i + 1:i + 1 + ce.CIN_LEN[d[i] & 0x0f]]
        out = []
        while 0xF7 in self.stream:
            e = self.stream.index(0xF7)
            s = self.stream.rfind(0xF0, 0, e)
            if s >= 0:
                out.append(bytes(self.stream[s:e + 1]))
            del self.stream[:e + 1]
        return out

    def wait(self, seconds):
        self.w.play(seconds)

    def latency_cable(self, route):
        self.w.latency_cable(route)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fw')
    ap.add_argument('--elf', help='the firmware''s ELF (default: FW.elf beside it, else build/fw''s)')
    a = ap.parse_args()
    from waveod_studio.emulated import firmware_elf
    w = ce.Waveod(a.fw, a.elf or firmware_elf(a.fw))
    p = Pedal(transport=EmuTransport(w), timeout=60)

    info = p.info()
    check(info['presets'] == 240 and info['store']['sectors'] == 103, 'info: %s' % info)
    lst = p.presets()
    check(len(lst) == 240 and lst[0]['active'] and lst[0]['place'] == 1, 'list: 01A "%s" original' % lst[0]['name'])

    origin, data = p.get(0)
    pr = wvpreset.Preset.decode(data)
    check(pr.name == lst[0]['name'] and len(pr.chain()) > 0,
          'get 0: "%s", %d blocks, %s' % (pr.name, len(pr.chain()), pr.route_mode()))
    check(pr.encode() == data, 'the codec gives back the same bytes')

    pr.name = 'STUDIO 5'
    p.put(5, pr.encode())
    _, back = p.get(5)
    lst = p.presets()
    check(wvpreset.Preset.decode(back).name == 'STUDIO 5' and lst[5]['place'] == 0, 'put 5: now waveod\'s record')

    p.move(5, 9)
    lst = p.presets()
    check(lst[9]['name'] == 'STUDIO 5', 'move 5 -> 9')
    p.copy(9, 30)
    check(p.presets()[30]['name'] == 'STUDIO 5', 'copy 9 -> 30')
    p.load(9)
    check(p.info()['active'] == 9, 'load 9')
    _, edited = p.get(0xFFFF)
    check(wvpreset.Preset.decode(edited).name == 'STUDIO 5', 'the edited preset')

    # scenes: stored by the pedal as given, the active one plays
    pr = wvpreset.Preset.decode(data)
    pr.name = 'CENAS'
    chain = pr.chain()
    sc = pr.scenes()
    first = chain[0].slot
    sc['scenes'][2]['on'][first] = not sc['scenes'][2]['on'][first]
    sc['scenes'][2]['name'] = 'SOLO'
    sc['params'] = [dict(slot=first, knob=0, values=[chain[0].knobs[0], 1.0, 2.0, chain[0].knobs[0]])]
    pr.set_scenes(sc)
    p.put(12, pr.encode())
    p.load(12)
    _, edited = p.get(0xFFFF)
    got = wvpreset.Preset.decode(edited).scenes()
    check(got['scenes'][2]['name'] == 'SOLO' and got['scenes'][2]['on'][first] != got['scenes'][0]['on'][first]
          and got['params'] == sc['params'], 'scenes kept by the pedal: %s' % got['params'])
    sc['active'] = 2
    want_on = sc['scenes'][2]['on'][first]
    sc['scenes'][0]['name'] = ''
    pr.set_scenes(sc)
    for t, b in pr.chunks:                          # the slots play scene 3: its blocks and values
        if t == wvpreset.SLOT and b[0] == first:
            s = bytearray(b)
            s[1] = int(want_on)
            s[10:14] = wvpreset.struct.pack('<f', 2.0)
            pr.chunks[pr.chunks.index((t, b))] = (t, bytes(s))
    p.put(13, pr.encode())
    p.load(13)
    _, edited = p.get(0xFFFF)
    e = wvpreset.Preset.decode(edited)
    slot = next(s for s in e.slots() if s.slot == first)
    check(e.scenes()['active'] == 2 and bool(slot.on) == want_on and slot.knobs[0] == 2.0,
          'loaded in scene 3: block %s, knob %g' % ('on' if slot.on else 'off', slot.knobs[0]))

    # the control table: described (converted from the original's fields), edited here, stored, loaded
    raw = p.request(DESCRIBE, struct.pack('<I', 0))
    table, blocks = p.describe(0)
    body = raw[11:11 + struct.unpack_from('<H', raw, 9)[0]]
    check(len(table.links) > 0 and table.encode() == body, 'describe 0: %d links, the codec gives back the bytes'
          % len(table.links))
    check(len(blocks) == len([s for s in pr.chain()]) and all(b.name and b.params for b in blocks),
          'describe 0: %s' % ', '.join('%s (%d)' % (b.name, len(b.params)) for b in blocks))
    texts = [controls.link_text(l, blocks) for l in table.links]
    check(all('?' not in t for t in texts), 'every link named: %s' % '; '.join(texts[:4]))
    t = table.copy()
    blk = blocks[0]
    cc = controls.Link(controls.MIDI_KEY + 20, controls.ALL)
    controls.set_param(cc, blk, blk.params[0])
    t.add(cc)
    out = controls.new_link(controls.A, controls.STOMP)
    controls.set_midi(out, False)
    out.arg, out.slot = 85, 2
    t.add(out)
    t.label[0], t.colour[0] = 'SOLO', 3
    pr = wvpreset.Preset.decode(data)
    pr.name = 'CONTROLES'
    pr.set_controls(t)
    p.put(14, pr.encode())
    back, _ = p.describe(14)
    check(back.encode() == t.encode(), 'controls stored as edited: %d links, "%s"' % (len(back.links), back.label[0]))
    p.load(14)
    back, _ = p.describe(0xFFFF)
    check(back.encode() == t.encode() and controls.link_text(back.links[-1], blocks) == 'MIDI CC 85 0 » 127 · CANAL 3',
          'the edited preset plays them: %s' % controls.link_text(back.links[-1], blocks))

    # the global control table: the factory one, the factory functions no longer in the preset; edited, stored
    # (a block link left out) and played by a preset without links on the key
    check(info['protocol'] >= 3, 'protocol %d: global controls' % info['protocol'])
    glob = p.global_controls()
    check(len(glob.links) > 20 and all(controls.global_ok(l) for l in glob.links),
          'global: %d links, functions and scenes' % len(glob.links))
    t0, blocks0 = p.describe(0)
    a_runs = controls.runs(t0, glob, controls.A, controls.PRESET)
    check(not t0.group(controls.A, controls.PRESET, 0) and a_runs and a_runs[0][1] and
          controls.link_text(a_runs[0][0], blocks0) == 'PRESET A', 'preset 0: A in preset mode = PRESET A (global)')
    g = glob.copy()
    for i in reversed(g.group(controls.BANK_UP, controls.PRESET, 0)):
        g.remove(i, controls.PRESET)
    tap = controls.Link(controls.BANK_UP, controls.PRESET, controls.PRESS, controls.FN, -1, 8)
    g.add(tap)
    want = g.copy()
    g.add(controls.Link(controls.D, controls.STOMP, controls.PRESS, controls.BLOCK, 0, 0))
    g.label[2] = want.label[2] = 'LOOP'
    p.set_global_controls(g)
    back = p.global_controls()
    check(back.encode() == want.encode(), 'global stored: %d links, the block link left out' % len(back.links))
    up = [x for x in controls.runs(t0, back, controls.BANK_UP, controls.PRESET) if x[0].when != controls.HOLD]
    check(len(up) == 1 and up[0] == (tap, True), 'preset 0: BANK+ in preset mode = %s (global)'
          % (controls.link_text(up[0][0], blocks0) if up else '-'))
    p.set_global_controls(glob)
    check(p.global_controls().encode() == glob.encode(), 'factory global table back')

    # protocol 13: the flash chip's id and status registers, read at start-up and never written
    if info['protocol'] >= 13:
        fs = p.flash_status()
        check(fs['jedec'] == 0xEF4017 and fs['sr'] == [0x00, 0x02, 0x60] and 'SR 00 02 60' in fs['diag'],
              'flash status: %s %s' % (fs['jedec'] and '%06X' % fs['jedec'], fs['sr']))
    else:
        check(False, 'protocol 13: the flash status')

    # MEUS RITMOS (protocol 7): a MIDI file converted, stored, listed, read back, played, emptied
    check(info['protocol'] >= 7, 'protocol %d: MEUS RITMOS' % info['protocol'])
    pat = drums.convert(test_drums.midi([[(0, b'\xFF\x51\x03\x07\xA1\x20')], test_drums.rock_bar(bars=2)]))
    rec = drums.record(pat, 'ROCK MIDI')
    p.drum_put(4, rec)
    d = p.drums()
    s = d['slots'][4]
    check(d['places'] == 20 and d['sounds'] == 13 and s and s['name'] == 'ROCK MIDI' and s['events'] == len(pat['events'])
          and s['bpm'] == 120 and not s['once'] and d['slots'][3] is None,
          'drums: place 5 "%s", %d events, %d BPM, %d sounds' % (s and s['name'], s and s['events'], s and s['bpm'],
                                                                  d['sounds']))
    check(p.drum_get(4) == rec and p.drum_get(3) == b'', 'drum get: the record back, an empty place empty')
    p.drum_play(4, True)
    d = p.drums()
    check(d['playing_place'] == 4 and d['playing'], 'drum play: place 5 playing')
    p.drum_put(4, drums.renamed(rec, 'NOVO NOME'))
    d = p.drums()
    check(d['slots'][4]['name'] == 'NOVO NOME' and d['playing_place'] == 4, 'rewritten while playing: still chosen')
    try:
        p.drum_put(5, rec[:-2])
        check(False, 'a cut record refused')
    except PedalError as x:
        check('inválido' in str(x), 'a cut record refused: %s' % x)
    try:
        p.drum_play(6, True)
        check(False, 'an empty place does not play')
    except PedalError as x:
        check('vazio' in str(x), 'an empty place does not play: %s' % x)
    p.drum_put(4, b'')
    d = p.drums()
    check(d['slots'][4] is None and d['playing_place'] is None, 'emptied: the bank\'s pattern again')
    p.drum_play(None, False)
    check(not p.drums()['playing'], 'stopped')

    try:
        p.put(400, data)
        check(False, 'put 400 refused')
    except PedalError as e:
        check('inválida' in str(e), 'put 400 refused: %s' % e)

    flash = p.read_flash(0x480000, 0x2000)
    check(flash == bytes(w.b.uc.mem_read(ce.emu.FLASH + 0x480000, 0x2000)), 'flash read')

    path = os.path.join(tempfile.mkdtemp(), 'teste.wvbak')
    m = backup.save(p, path)
    check(len(m['presets']) >= 120, 'backup: %d presets' % len(m['presets']))
    pr.name = 'MUDOU'
    p.put(30, pr.encode())
    g = p.global_controls()
    check(m.get('global_controls', {}).get('links') == len(g.links) and backup.read_global(path).encode() == g.encode(),
          'backup: the global control table (%d links)' % len(g.links))
    changed = g.copy()
    changed.add(controls.Link(controls.B, controls.PRESET, controls.HOLD, controls.FN, -1, 8))
    p.set_global_controls(changed)
    n = backup.restore(p, path, only={30})
    check(n == 1 and p.presets()[30]['name'] == 'STUDIO 5', 'restore brings back 30')
    check(p.global_controls().encode() == changed.encode(), 'restoring some presets leaves the global table')
    backup.restore(p, path, only=set(), global_controls=True)
    check(p.global_controls().encode() == g.encode(), 'the global table restored')

    # the latency test: through the emulated cable (128 + 7 samples) and through the active preset's chain
    check(info['protocol'] >= 4 and p.latency_result()['status'] == 0, 'protocol %d: latency, none measured yet'
          % info['protocol'])
    for name in ('L', 'SEND'):
        r = p.measure_latency(LAT_ROUTES[name])
        check(r['status'] == 2 and r['runs'] == 8 and abs(r['mean'] - 135) < 0.1 and r['max'] - r['min'] < 0.1,
              'latency %s: %s, %.2f samples (%.2f..%.2f) = %.3f ms, %d dBFS, match %.2f' % (
                  name, r['status_text'], r['mean'], r['min'], r['max'], r['ms'], r['level_db'], r['match']))
    r = p.measure_latency(LAT_ROUTES['PRESET'])
    check(r['status'] in (2, 3) and r['runs'] == 8 and 0 <= r['mean'] < 1000,
          'latency through preset %d: %s, %.2f samples (%.2f..%.2f), match %.2f' % (
              p.info()['active'], r['status_text'], r['mean'], r['min'], r['max'], r['match']))

    chain_screen(p)
    global_screen(p)
    scene_menu(p)
    fx_loop(p)
    print('%d failed' % failed)
    return 1 if failed else 0


def chain_screen(p):
    """protocol 8 (src/editor/wv_rig_msgs.c): the GRID of the new interface"""
    if p.info()['protocol'] < 8:
        check(False, 'protocol 8: the chain screen')
        return
    p.load(0)
    r = p.rig()
    cells = [b['cell'] for b in r['blocks']]
    check(len(r['order']) == len(r['blocks']) and len(set(cells)) == len(cells) and r['name'],
          'rig: "%s", %d blocks, route %s' % (r['name'], len(r['blocks']), r['route']))
    cat = p.catalog(3)
    check(len(cat) > 10 and all(i['name'] for i in cat) and [i['index'] for i in cat] == list(range(len(cat))),
          'catalog DRV: %d items (%s .. %s)' % (len(cat), cat[0]['name'], cat[-1]['name']))
    check(len(p.catalog(5)) > 20 and p.catalog(13) is not None, 'catalog CAB: %d items' % len(p.catalog(5)))

    b = next(x for x in r['blocks'] if x['kind'] != 'routing')
    ps = p.block(b['slot'])
    check(len(ps) == b['params'] and all(q['name'] and q['text'] for q in ps),
          'block %d %s: %s' % (b['slot'], b['name'], ', '.join('%s=%s' % (q['name'], q['text']) for q in ps)))
    e = p.edit(PR.EDIT_PARAM, b['slot'], ps[0]['k'], 0, ps[0]['max'] + 1000)
    check(e['result'] == 0 and e['param']['value'] == ps[0]['max'] and p.rig()['dirty'],
          'param to the top, clamped: %s, the preset modified' % e['param']['text'])
    p.edit(PR.EDIT_ON, b['slot'], 0)
    check(not next(x for x in p.rig()['blocks'] if x['slot'] == b['slot'])['on'], 'block off')
    p.edit(PR.EDIT_ON, b['slot'], 1)

    free = [c for c in range(12) if c not in cells]
    e = p.edit(PR.EDIT_PLACE, free[0], 3, 0)
    slot = e['result']
    placed = next((x for x in p.rig()['blocks'] if x['slot'] == slot), None)
    check(slot >= 0 and placed and placed['cell'] == free[0] and placed['name'] == cat[0]['name'],
          'place %s in cell %d: slot %d' % (cat[0]['name'], free[0], slot))
    p.edit(PR.EDIT_MOVE, slot, b['cell'])                  # onto a block: the two swap places
    at = {x['slot']: x['cell'] for x in p.rig()['blocks']}
    check(at[slot] == b['cell'] and at[b['slot']] == free[0], 'move onto %s: swapped' % b['name'])
    p.edit(PR.EDIT_REPLACE, slot, 3, 1)
    check(next(x for x in p.rig()['blocks'] if x['slot'] == slot)['name'] == cat[1]['name'],
          'replace with %s' % cat[1]['name'])
    p.edit(PR.EDIT_REMOVE, slot)
    check(all(x['slot'] != slot for x in p.rig()['blocks']), 'remove')

    e = p.edit(PR.EDIT_ROUTE, 1, 2, 4)
    rr = p.rig()['route']
    check(e['result'] == 1 and (rr['mode'], rr['split'], rr['merge']) == (1, 2, 4), 'route: parallel 2..4')
    p.edit(PR.EDIT_JOINT, 3, 70)
    check(p.rig()['route']['joints'][3] == 70, 'joint: merge mix 70')
    p.edit(PR.EDIT_SCENE, 1)
    check(p.rig()['scene']['active'] == 1, 'scene 2')
    p.edit(PR.EDIT_SCENE, 0)
    check(p.edit(99)['result'] == -21, 'unknown edit refused')
    name = p.rig()['name']
    e = p.edit(PR.EDIT_SAVE, 0)
    after = p.rig()
    _, data = p.get(0)
    check(e['result'] == 0 and not after['dirty'] and wvpreset.Preset.decode(data).route_mode() == 'paralelo'
          and after['name'] == name, 'save into 01A: stored, routing kept, no longer modified')

    # protocol 12: the edited preset's name, written by EDIT_SAVE (the save screen's NOME)
    if p.info().get('protocol', 0) >= 12:
        e = p.edit(PR.EDIT_NAME, text='Novo Nome 123456789')
        mid = p.rig()
        check(e['result'] == 0 and mid['name'] == 'Novo Nome 1234567' and mid['dirty'], 'name: 17 characters, modified')
        e = p.edit(PR.EDIT_SAVE, 0)
        _, data = p.get(0)
        check(e['result'] == 0 and wvpreset.Preset.decode(data).name == 'Novo Nome 1234567'
              and p.presets()[0]['name'] == 'Novo Nome 1234567', 'name saved into 01A')
        p.edit(PR.EDIT_NAME, text=name)
        p.edit(PR.EDIT_SAVE, 0)
        check(p.rig()['name'] == name and wvpreset.Preset.decode(p.get(0)[1]).name == name, 'name back')
    else:
        check(False, 'protocol 12: the preset name')

    # the presets screen draws a stored preset where the pedal puts its blocks on loading (webapp.route_fit)
    from waveod_studio import webapp
    for i in (0, 1, 9, 54, 78, 101):                       # 54, 78: a slot out of the chain
        d = webapp.preset_view(i, *p.get(i))
        p.load(i)
        rg = p.rig()
        want = sorted((x['slot'], x['cell']) for x in rg['blocks']          # cell -1: a slot out of the chain
                      if x['kind'] != 'routing' and x['cell'] >= 0)
        got = sorted((x['slot'], x['cell']) for x in d['blocks'])
        same_route = all(d['route'][k] == rg['route'][k] for k in ('mode', 'split', 'merge'))
        check(got == want and same_route and d['name'] == rg['name'],
              'presets screen %s "%s": %d blocks where the pedal lays them, %s' % (
                  wvpreset.preset_label(i), d['name'], len(got), ('série', 'paralelo', 'dual')[d['route']['mode']]))


def global_screen(p):
    """protocol 9 (src/editor/wv_settings_msgs.c): the GLOBAL screen's settings and the global EQ"""
    if p.info()['protocol'] < 9:
        check(False, 'protocol 9: the GLOBAL screen')
        return
    s = p.settings()
    names = [k for k in s if k != 'eq']
    check(len(names) == len(PR.SETTING_IDS) and len(s['eq']) == 31 and all(s['eq']),
          'settings: %d, global EQ: 31 parameters' % len(names))
    r = p.set_settings(dict(input_level=4, a4=441, aux=60, eq={14: 5.0}))
    check((r['input_level']['value'], r['a4']['value'], r['aux']['value'], r['eq'][14]['value']) == (4, 441, 60, 5.0)
          and p.settings() == r, 'changed and read back: input +4 dB, A4 441 Hz, AUX 60, EQ B2 +5 dB')
    for bad in ({'exp1_max': 100}, {'tempo': 20}, {'a4': 470}):
        try:
            p.set_settings(bad)
            check(False, 'refused: %s' % bad)
        except PedalError as e:
            check(True, 'refused: %s (%s)' % (bad, e))
    p.set_settings(dict(input_level=s['input_level']['value'], a4=s['a4']['value'], aux=s['aux']['value'],
                        eq={14: s['eq'][14]['value']}))

def scene_menu(p):
    """protocol 10 (src/editor/wv_rig_msgs.c): the scene menu's edits on the edited preset"""
    if p.info()['protocol'] < 10:
        check(False, 'protocol 10: the scene menu')
        return
    p.load(1)
    r = p.rig()
    b = next(x for x in r['blocks'] if x['kind'] != 'routing')
    check(p.edit(PR.EDIT_SCENE_PICK, 2)['result'] == 0 and p.rig()['scene']['active'] == 2 and p.rig()['scene']['editing'],
          'pick scene 3: it plays and edits go to it')
    p.edit(PR.EDIT_ON, b['slot'], 0)                         # off in scene 3 only
    p.edit(PR.EDIT_SCENE_COPY, 2, 0)
    p.edit(PR.EDIT_SCENE_PICK, 0)
    check(not next(x for x in p.rig()['blocks'] if x['slot'] == b['slot'])['on'], 'scene 3 copied over scene 1: %s off there'
          % b['name'])
    p.edit(PR.EDIT_SCENE_NAME, 0, text='LIMPO')
    check(p.rig()['scene']['names'][0] == 'LIMPO', 'scene 1 named: %s' % p.rig()['scene']['names'])
    k = p.edit(PR.EDIT_SCENE_PARAM, b['slot'], 0)
    check(k['result'] == 1 and p.block(b['slot'])[0]['scene'], 'parameter 1 of %s: a scene parameter' % b['name'])
    check(p.edit(PR.EDIT_SCENE_PARAM, b['slot'], 0)['result'] == 0 and not p.block(b['slot'])[0]['scene'],
          'and the preset\'s again')
    e = p.edit(PR.EDIT_SCENE_REVERT, 0)
    rr = p.rig()
    check(e['result'] == 0 and rr['scene']['names'][0] == 'CENA 1' and
          next(x for x in rr['blocks'] if x['slot'] == b['slot'])['on'], 'scene 1 reverted: its name and blocks as stored')
    check(p.edit(PR.EDIT_SCENE_COPY, 1, 1)['result'] == -21, 'a scene over itself refused')
    p.load(1)


def fx_loop(p):
    """FX Loop, SND and RTN (blocks/fx_loop.c, in place of the original's modules): the GRID's browser offers
    them; their knobs are the entry's, then Return Phase"""
    want = {12: ('FX Loop', ['Send Level', 'Return Level', 'Mix', 'Return Phase']),
            13: ('SND', ['Send Level']),
            14: ('RTN', ['Return Level', 'Mix', 'Return Phase'])}
    p.load(0)
    for menu, (name, knobs) in want.items():
        cat = p.catalog(menu)
        check(len(cat) == 1 and cat[0]['name'] == name and cat[0]['kind'] == 'builtin',
              'catalog %d: %s' % (menu, [(i['name'], i['kind']) for i in cat]))
        free = [c for c in range(12) if c not in [b['cell'] for b in p.rig()['blocks']]]
        slot = p.edit(PR.EDIT_PLACE, free[0], menu, 0)['result']
        ps = p.block(slot)
        check(slot >= 0 and [q['name'] for q in ps] == knobs, '%s placed: %s' % (
            name, ', '.join('%s=%s' % (q['name'], q['text']) for q in ps)))
        if 'Return Phase' in knobs:
            e = p.edit(PR.EDIT_PARAM, slot, ps[knobs.index('Return Phase')]['k'], 0, 1)
            check(e['param']['text'] == 'Invert', '%s Return Phase: %s' % (name, e['param']['text']))
        p.edit(PR.EDIT_REMOVE, slot)
    p.load(0)

if __name__ == '__main__':
    sys.exit(main())
