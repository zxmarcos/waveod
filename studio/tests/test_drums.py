"""MIDI files to MEUS RITMOS patterns (waveod_studio/drums.py), no pedal needed.

    python tests/test_drums.py
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))

from waveod_studio import drums                     # noqa: E402

failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, 0x80 | v & 0x7F)
        v >>= 7
    return bytes(out)


def midi(tracks, division=480, fmt=1):
    """a Standard MIDI File: tracks = [[(tick, bytes of the event)]]"""
    out = struct.pack('>4sIHHH', b'MThd', 6, fmt, len(tracks), division)
    for tr in tracks:
        body, now = b'', 0
        for t, ev in sorted(tr, key=lambda e: e[0]):
            body += varlen(t - now) + ev
            now = t
        body += b'\x00\xFF\x2F\x00'
        out += b'MTrk' + struct.pack('>I', len(body)) + body
    return out


def note(t, n, v=100, ch=9):
    return [(t, bytes([0x90 | ch, n, v])), (t + 60, bytes([0x80 | ch, n, 0]))]


def rock_bar(division=480, bars=1, start=0):
    """kick on 1 and 3, snare on 2 and 4, closed hat on the eighths, a tom on 4 and"""
    ev = []
    for b in range(bars):
        o = start + b * 4 * division
        for beat in range(4):
            ev += note(o + beat * division, 36 if beat % 2 == 0 else 38, 110)
            ev += note(o + beat * division, 42, 80)
            ev += note(o + beat * division + division // 2, 42, 60)
        ev += note(o + 3 * division + division // 2, 45, 90)
    return ev


def main():
    tempo = [(0, b'\xFF\x51\x03' + (500000 * 120 // 96).to_bytes(3, 'big')),      # 96 BPM
             (0, b'\xFF\x58\x04\x04\x02\x18\x08'), (0, b'\xFF\x03\x08Groove 1')]
    data = midi([tempo, rock_bar(bars=2)])
    p = drums.convert(data)
    check(p['bpm'] == 96 and p['bars'] == 2 and p['beats'] == 8, 'tempo %d, %d bars, %g beats' % (p['bpm'], p['bars'],
                                                                                                    p['beats']))
    check(p['dropped'] == {45: 2} and p['hits'] == 2 * 12, 'tom left out (%s), %d hits' % (
        drums.describe_dropped(p['dropped']), p['hits']))
    ev = p['events']
    first = [e for e in ev[:3]]
    check(first == [drums.HIT | 1 << 8 | 110, drums.HIT | 6 << 8 | 80, 60], 'first beat: kick, hat, wait 60 (%s)' %
          ['%04X' % e for e in first])
    waits = sum(e for e in ev if e < 0x8000)
    check(waits == 8 * drums.TICKS and ev[-1] == drums.LOOP, 'waits add up to the 2 bars, ends with LOOP')
    check(p['name'] == 'GROOVE 1', 'name from the track: %s' % p['name'])
    snare = [e for e in ev if e & 0x8000 and e < 0xF000 and (e >> 8) & 0x7F == 5]
    check(len(snare) == 4, 'snare (sound 5) on 2 and 4: %d' % len(snare))

    e = drums.convert(data, kit='eletronico', once=True, name='Teste Ação')
    kinds = {(x >> 8) & 0x7F for x in e['events'] if x & 0x8000 and x < 0xF000}
    check(kinds == {0, 3, 6} and e['events'][-1] == drums.END and e['name'] == 'TESTE ACAO',
          'electronic kit, once, name cleaned: %s %s' % (sorted(kinds), e['name']))

    rec = drums.record(p)
    r = drums.parse_record(rec)
    check(len(rec) == 22 + 2 * len(ev) and r['events'] == ev and r['bpm'] == 96 and not r['once'] and
          r['name'] == 'GROOVE 1', 'record: %d bytes, read back' % len(rec))
    check(drums.parse_record(drums.renamed(rec, 'outro nome'))['name'] == 'OUTRO NOME', 'renamed')

    # every channel when there is no channel 10; division other than 480; running status
    d2 = midi([[(0, bytes([0x90, 36, 100])), (240, bytes([38, 100])), (480, bytes([36, 0]))]], division=240, fmt=0)
    q = drums.convert(d2)
    check(q['channel'] is None and q['hits'] == 2 and q['bars'] == 1 and q['bpm'] == 0,
          'format 0, channel 1, running status: %d hits, 1 bar' % q['hits'])

    # too long: cut at whole bars
    long = midi([tempo, [x for b in range(200) for x in rock_bar(bars=1, start=b * 1920)]])
    lp = drums.convert(long)
    check(len(lp['events']) <= drums.MAX_EVENTS and lp['cut'] > 0 and lp['bars'] + lp['cut'] == 200,
          'long file: %d bars kept, %d cut, %d events' % (lp['bars'], lp['cut'], len(lp['events'])))

    for bad, why in ((b'RIFF', 'não é'), (midi([[(0, b'\xFF\x51\x03\x07\xA1\x20')]]), 'não tem notas'),
                     (midi([note(0, 45) + note(480, 47)]), 'nenhuma nota')):
        try:
            drums.convert(bad)
            check(False, 'refused: %s' % why)
        except drums.MidiError as x:
            check(why in str(x), 'refused: %s' % x)

    # the roll the Studio draws: every hit back at its tick, the loop's whole length
    pat = drums.convert(midi([rock_bar(bars=2)]))
    r = drums.roll(pat['events'])
    kicks = [h for h in r['hits'] if h[1] == 1]
    check(len(r['hits']) == pat['hits'] and r['length'] == 2 * 4 * drums.TICKS and not r['once']
          and [h[0] for h in kicks] == [0, 240, 480, 720] and all(h[2] == 110 for h in kicks),
          'roll: %d hits over %d ticks, kicks at %s' % (len(r['hits']), r['length'], [h[0] for h in kicks]))
    s = drums.summary(drums.convert(midi([rock_bar(bars=3)]), once=True))
    check(s['bars'] == 3 and s['bar'] == 480 and s['length'] == 1440 and s['once'] and 'tom grave' in s['dropped'],
          'summary: %d bars of %d ticks, once, left out: %s' % (s['bars'], s['bar'], s['dropped']))

    print('%d failed' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
