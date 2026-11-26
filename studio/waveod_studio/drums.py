"""MIDI files to waveod's drum patterns (MEUS RITMOS; waveod/src/model/drum_user.h).

A pattern is the drum machine's events (waveod/src/engine/drums.h): waits in ticks of 1/120 beat and hits
(0x8000 | sound << 8 | velocity), ending with LOOP (start over) or END (play once and stop). The pedal has the 13
sounds of the original's bank and no toms; the General MIDI drum notes are mapped onto them (KITS), the others
are left out and counted.

A record for the pedal: u8 version 1, u8 flags (ONCE), u16 BPM (the file's first tempo, 0 = none), char name[16],
u16 count, u16 events[count].
"""
import os
import re
import struct

TICKS = 120                                 # per beat
HIT, LOOP, END = 0x8000, 0xFFFF, 0xFFFE
MAX_EVENTS = 2000
MAX_WAIT = 0x7FFF
PLACES = 20
NAME_LEN = 16
ONCE = 0x01
VERSION = 1

# the bank's sounds (what they are: their spectra and where the factory patterns use them)
SOUNDS = ['bumbo eletrônico', 'bumbo', 'aro / clique', 'caixa eletrônica', 'palmas', 'caixa', 'chimbal fechado',
          'chimbal aberto', 'prato de ataque', 'ride', 'splash', 'cowbell', 'pandeirola']

# General MIDI drum note -> (sound, velocity factor)
_COMMON = {
    37: (2, 1.0),                           # side stick
    39: (4, 1.0),                           # hand clap
    42: (6, 1.0), 44: (6, 0.6), 46: (7, 1.0),               # hi-hat closed, pedal, open
    49: (8, 1.0), 57: (8, 1.0),             # crash 1 / 2
    51: (9, 1.0), 59: (9, 1.0), 53: (9, 1.0),               # ride, ride bell
    52: (10, 1.0), 55: (10, 1.0),           # china, splash
    56: (11, 1.0),                          # cowbell
    54: (12, 1.0), 69: (12, 0.8), 70: (12, 0.8), 82: (12, 0.8),     # tambourine, cabasa, maracas, shaker
    33: (2, 0.8), 34: (2, 0.8),             # metronome click / bell
}
KITS = {
    'acustico': {**_COMMON, 35: (1, 1.0), 36: (1, 1.0), 38: (5, 1.0), 40: (5, 1.0)},
    'eletronico': {**_COMMON, 35: (0, 1.0), 36: (0, 1.0), 38: (3, 1.0), 40: (3, 1.0)},
}
KIT_NAMES = {'acustico': 'Acústicos', 'eletronico': 'Eletrônicos'}
GM_NAMES = {35: 'bumbo acústico', 36: 'bumbo', 41: 'tom chão grave', 43: 'tom chão', 45: 'tom grave',
            47: 'tom médio', 48: 'tom médio-agudo', 50: 'tom agudo', 58: 'vibraslap', 60: 'bongô agudo',
            61: 'bongô grave', 62: 'conga abafada', 63: 'conga aberta', 64: 'conga grave', 65: 'timbal agudo',
            66: 'timbal grave', 67: 'agogô agudo', 68: 'agogô grave', 71: 'apito curto', 72: 'apito longo',
            73: 'guiro curto', 74: 'guiro longo', 75: 'claves', 76: 'wood block agudo', 77: 'wood block grave',
            78: 'cuíca abafada', 79: 'cuíca aberta', 80: 'triângulo abafado', 81: 'triângulo aberto'}


class MidiError(ValueError):
    pass


# ---------------------------------------------------------------------- reading a Standard MIDI File

def _varlen(b, at):
    v = 0
    for _ in range(4):
        if at >= len(b):
            raise MidiError('arquivo MIDI cortado')
        c = b[at]
        at += 1
        v = v << 7 | c & 0x7F
        if not c & 0x80:
            return v, at
    raise MidiError('número de tamanho variável inválido')


def read_midi(data):
    """dict(division, notes=[(tick, channel, note, velocity)], tempo (µs per beat or None), meter (num, den),
    end (last tick), name)"""
    if data[:4] != b'MThd':
        raise MidiError('não é um arquivo MIDI (.mid)')
    hl, fmt, ntrk, div = struct.unpack_from('>IHHH', data, 4)
    if div & 0x8000:
        raise MidiError('MIDI com tempo SMPTE não é suportado')
    if fmt > 1:
        raise MidiError('MIDI formato %d não é suportado (só 0 e 1)' % fmt)
    at = 8 + hl
    notes, tempo, meter, end, name = [], None, None, 0, ''
    for _ in range(ntrk):
        if data[at:at + 4] != b'MTrk':
            raise MidiError('trilha MIDI inválida')
        n, = struct.unpack_from('>I', data, at + 4)
        tr, at = data[at + 8:at + 8 + n], at + 8 + n
        i, t, status = 0, 0, 0
        while i < len(tr):
            d, i = _varlen(tr, i)
            t += d
            if i >= len(tr):
                break
            c = tr[i]
            if c == 0xFF:                       # meta
                kind = tr[i + 1]
                ln, i = _varlen(tr, i + 2)
                body = tr[i:i + ln]
                i += ln
                if kind == 0x51 and ln == 3 and tempo is None:
                    tempo = body[0] << 16 | body[1] << 8 | body[2]
                elif kind == 0x58 and ln >= 2 and meter is None:
                    meter = (body[0], 1 << body[1])
                elif kind == 0x03 and not name:
                    name = body.decode('latin-1', 'replace').strip()
                elif kind == 0x2F:
                    break
                continue
            if c in (0xF0, 0xF7):               # SysEx
                ln, i = _varlen(tr, i + 1)
                i += ln
                continue
            if c & 0x80:
                status = c
                i += 1
            elif not status:
                raise MidiError('evento MIDI sem status')
            kind, ch = status & 0xF0, status & 0x0F
            size = 1 if kind in (0xC0, 0xD0) else 2
            args = tr[i:i + size]
            i += size
            if kind == 0x90 and len(args) == 2 and args[1] > 0:
                notes.append((t, ch, args[0], args[1]))
        end = max(end, t)
    return dict(division=div, notes=notes, tempo=tempo, meter=meter or (4, 4), end=end, name=name)


# ---------------------------------------------------------------------- converting

def convert(data, kit='acustico', once=False, name=None):
    """a pattern from a MIDI file's bytes: dict(events, bpm, bars, beats, hits, dropped {note: count}, channel,
    cut (bars left out to fit), name)"""
    m = read_midi(data)
    if not m['notes']:
        raise MidiError('o arquivo não tem notas')
    chans = {c for _, c, _, _ in m['notes']}
    channel = 9 if 9 in chans else None     # the GM drum channel when there is one, else every channel
    mapping = KITS[kit]
    div = m['division']
    hits, dropped = {}, {}
    for t, c, note, vel in m['notes']:
        if channel is not None and c != channel:
            continue
        if note not in mapping:
            dropped[note] = dropped.get(note, 0) + 1
            continue
        sound, f = mapping[note]
        tick = (t * TICKS + div // 2) // div
        v = max(1, min(127, round(vel * f)))
        key = (tick, sound)
        hits[key] = max(hits.get(key, 0), v)            # the same sound twice at once: the louder
    if not hits:
        raise MidiError('nenhuma nota de bateria que o pedal toque (%s)' % describe_dropped(dropped))
    num, den = m['meter']
    bar = max(1, num * TICKS * 4 // den)
    last = max(t for t, _ in hits)
    length = max(last + 1, (m['end'] * TICKS + div // 2) // div)
    length = -(-length // bar) * bar                    # whole bars: a loop stays in time
    events, cut = _events(hits, length, once)
    while len(events) > MAX_EVENTS and length > bar:    # too long: whole bars from the start that fit
        length -= bar
        cut += 1
        events, _ = _events({k: v for k, v in hits.items() if k[0] < length}, length, once)
    if len(events) > MAX_EVENTS:
        raise MidiError('um compasso já passa de %d eventos' % MAX_EVENTS)
    bpm = round(60e6 / m['tempo']) if m['tempo'] else 0
    return dict(events=events, bpm=bpm if 40 <= bpm <= 300 else 0, bars=length // bar, beats=length / TICKS,
                hits=sum(1 for k in hits if k[0] < length), dropped=dropped, channel=channel, cut=cut,
                name=clean_name(name or m['name'] or 'MIDI'), once=once, kit=kit)


def _events(hits, length, once):
    ev, now = [], 0
    for tick in sorted({t for t, _ in hits}):
        ev += _wait(tick - now)
        now = tick
        ev += [HIT | s << 8 | v for (t, s), v in sorted(hits.items()) if t == tick]
    ev += _wait(length - now)
    ev.append(END if once else LOOP)
    return ev, 0


def _wait(n):
    out = []
    while n > 0:
        out.append(min(n, MAX_WAIT))
        n -= out[-1]
    return out


def clean_name(s):
    """what the pedal's font shows: upper-case ASCII, at most NAME_LEN characters"""
    import unicodedata
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().upper()
    s = re.sub(r'[^A-Z0-9 .,+\-#/()&]', ' ', s)
    return re.sub(r'\s+', ' ', s).strip()[:NAME_LEN].strip() or 'MIDI'


def name_from_path(path):
    return clean_name(os.path.splitext(os.path.basename(path))[0].replace('_', ' '))


def describe_dropped(dropped):
    if not dropped:
        return 'nada'
    return ', '.join('%s ×%d' % (GM_NAMES.get(n, 'nota %d' % n), c) for n, c in sorted(dropped.items()))


def record(pattern, name=None):
    """the bytes the pedal stores for a pattern from convert()"""
    nm = clean_name(name or pattern['name']).encode('ascii')[:NAME_LEN].ljust(NAME_LEN, b'\0')
    ev = pattern['events']
    return struct.pack('<BBH16sH', VERSION, ONCE if pattern['once'] else 0, pattern['bpm'], nm, len(ev)) + \
        struct.pack('<%dH' % len(ev), *ev)


def parse_record(rec):
    """dict(flags, once, bpm, name, events) of a stored record, or None"""
    if len(rec) < 24 or rec[0] != VERSION:
        return None
    ver, flags, bpm, nm, n = struct.unpack_from('<BBH16sH', rec)
    if len(rec) != 22 + 2 * n:
        return None
    return dict(flags=flags, once=bool(flags & ONCE), bpm=bpm, name=nm.split(b'\0')[0].decode('latin-1'),
                events=list(struct.unpack_from('<%dH' % n, rec, 22)))


def renamed(rec, name):
    """a stored record under another name"""
    r = parse_record(rec)
    if r is None:
        raise MidiError('registro inválido')
    return rec[:4] + clean_name(name).encode('ascii')[:NAME_LEN].ljust(NAME_LEN, b'\0') + rec[20:]


def roll(events):
    """what a pattern plays, for drawing it: dict(hits=[[tick, sound, velocity]], length (ticks), once)"""
    hits, now, once = [], 0, False
    for ev in events:
        if ev == END:
            once = True
            break
        if ev == LOOP:
            break
        if ev & HIT:
            hits.append([now, ev >> 8 & 0x7F, ev & 0xFF])
        else:
            now += ev
    return dict(hits=hits, length=now, once=once)


def summary(pattern):
    """a converted pattern as the import screen shows it, with its roll and the length of its bars in ticks"""
    r = roll(pattern['events'])
    return dict(name=pattern['name'], bpm=pattern['bpm'], bars=pattern['bars'], beats=pattern['beats'],
                seconds=length_seconds(pattern), notes=pattern['hits'], events=len(pattern['events']),
                dropped=describe_dropped(pattern['dropped']) if pattern['dropped'] else '', cut=pattern['cut'],
                all_channels=pattern['channel'] is None, once=pattern['once'], kit=pattern['kit'],
                hits=r['hits'], length=r['length'], bar=r['length'] // max(1, pattern['bars']))


def length_seconds(pattern, bpm=None):
    b = bpm or pattern['bpm'] or 120
    return pattern['beats'] * 60.0 / b
