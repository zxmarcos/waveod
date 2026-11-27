"""waveod's preset format (waveod/src/fw/wv_preset.h):

    "WP" u8 version u8 flags, then chunks {u8 tag, u16 length, bytes}

A Preset keeps every chunk as it came (unknown tags too), so editing a field and encoding again never loses
what a newer firmware stored; the known chunks are also decoded into fields.
"""
import struct

from waveod_studio.i18n import tr

NAME, STYLE, MAIN, SLOT, QK, EXP, CTRL, FS, ROUTE, SCENES, CONTROLS, MODS = range(1, 13)
SLOTS, KNOBS, NAME_LEN, STYLE_LEN = 12, 15, 17, 10
SCENE_COUNT = 4
ROUTE_MODES = ('série', 'paralelo', 'dual L/R')
# modulators (chunk 12; the control table's keys MOD 1-4 follow them)
MOD_COUNT, MOD_STEPS, MOD_BYTES = 4, 16, 32
MOD_TYPES = ('off', 'lfo', 'env', 'seq')
MOD_SHAPES = ('sine', 'triangle', 'square', 'saw up', 'saw down', 'random', 'smooth')


def mod_default(type_):
    """a modulator as the pedal starts one of that type (preset_mod_default): 1 Hz sine, 10 / 200 ms, 8 steps rising"""
    return dict(type=type_, shape='sine', sync=0, rate=1.0, steps=8, phase=0, glide=0, sens=0, attack=10, release=200,
                step=[i % 8 * 100 // 7 for i in range(MOD_STEPS)])


class FormatError(ValueError):
    pass


class Slot:
    def __init__(self, slot, on, menu, index, id_, knobs):
        self.slot, self.on, self.menu, self.index, self.id, self.knobs = slot, on, menu, index, id_, knobs

    def __repr__(self):
        return 'Slot(%d, menu %d, id %#x, %s)' % (self.slot, self.menu, self.id, 'on' if self.on else 'off')


class Preset:
    def __init__(self, chunks=None, version=1, flags=0):
        self.version, self.flags = version, flags
        self.chunks = list(chunks or [])            # [(tag, bytes)] in order

    # ------------------------------------------------------------------ chunks
    def chunk(self, tag):
        return next((b for t, b in self.chunks if t == tag), None)

    def set_chunk(self, tag, body):
        for i, (t, _) in enumerate(self.chunks):
            if t == tag:
                self.chunks[i] = (tag, bytes(body))
                return
        self.chunks.append((tag, bytes(body)))

    # ------------------------------------------------------------------ fields
    @property
    def name(self):
        return (self.chunk(NAME) or b'').decode('latin-1')

    @name.setter
    def name(self, value):
        self.set_chunk(NAME, value.encode('latin-1', 'replace')[:NAME_LEN])

    @property
    def style(self):
        return (self.chunk(STYLE) or b'').decode('latin-1')

    @style.setter
    def style(self, value):
        self.set_chunk(STYLE, value.encode('latin-1', 'replace')[:STYLE_LEN])

    def main(self):
        """volume, pan, BPM, FX loop, EXP 1 group, chain length, order (or None)"""
        b = self.chunk(MAIN)
        if not b or len(b) < 7 + SLOTS:
            return None
        vol, pan, bpm, loop, grp, count = struct.unpack_from('<BbHBBB', b)
        order = [x - 256 if x > 127 else x for x in b[7:7 + SLOTS]]
        return dict(volume=vol, pan=pan, bpm=bpm, fx_loop=loop, exp_group=grp, count=count, order=order[:count])

    @property
    def volume(self):
        m = self.main()
        return m['volume'] if m else None

    @property
    def bpm(self):
        m = self.main()
        return m['bpm'] if m else None

    def slots(self):
        out = []
        for t, b in self.chunks:
            if t != SLOT or len(b) < 10:
                continue
            slot, on, menu, index, id_, n = struct.unpack_from('<BBbhIB', b)
            knobs = list(struct.unpack_from('<%df' % min(n, (len(b) - 10) // 4), b, 10))
            out.append(Slot(slot, on, menu, index, id_, knobs))
        return out

    def chain(self):
        """the used slots in signal order"""
        m, by = self.main(), {s.slot: s for s in self.slots()}
        return [by[i] for i in (m['order'] if m else []) if i in by]

    def route_mode(self):
        b = self.chunk(ROUTE)
        return ROUTE_MODES[b[1]] if b and len(b) > 1 and b[1] < len(ROUTE_MODES) else ROUTE_MODES[0]

    def scenes(self):
        """The scenes: dict(active, scenes=[dict(name, on={slot: bool})], params=[dict(slot, knob, values)]).
        A preset without the chunk has SCENE_COUNT scenes equal to its blocks (as the firmware reads it)."""
        used = {s.slot: bool(s.on) for s in self.slots() if s.menu >= 0}
        plain = dict(active=0, params=[],
                     scenes=[dict(name='', on=dict(used)) for _ in range(SCENE_COUNT)])
        b = self.chunk(SCENES)
        if not b or len(b) < 2:
            return plain
        active, count, at, scenes = b[0], b[1], 2, []
        for _ in range(count):
            if at + 3 > len(b) or at + 3 + b[at + 2] > len(b):
                return plain
            mask, n = struct.unpack_from('<HB', b, at)
            scenes.append(dict(name=b[at + 3:at + 3 + n].decode('latin-1'),
                               on={s: bool(mask >> s & 1) for s in used}))
            at += 3 + n
        if not scenes:
            return plain
        while len(scenes) < SCENE_COUNT:
            scenes.append(dict(name='', on=dict(scenes[0]['on'])))
        if active < len(scenes):                    # the slots are what plays
            scenes[active]['on'] = dict(used)
        params = []
        if at < len(b):
            for _ in range(b[at]):
                at_v = at + 1 + 2
                if at_v + 4 * count > len(b):
                    break
                params.append(dict(slot=b[at + 1], knob=b[at + 2],
                                   values=list(struct.unpack_from('<%df' % count, b, at_v))))
                at += 2 + 4 * count
        return dict(active=active if active < SCENE_COUNT else 0, scenes=scenes[:SCENE_COUNT], params=params)

    def set_scenes(self, sc):
        """Store scenes() as given. The firmware plays the slots: the active scene's blocks must match them."""
        body = bytearray([sc['active'], len(sc['scenes'])])
        for scene in sc['scenes']:
            name = scene['name'].encode('latin-1', 'replace')[:10]
            mask = sum(1 << s for s, on in scene['on'].items() if on)
            body += struct.pack('<HB', mask, len(name)) + name
        body.append(len(sc['params']))
        for prm in sc['params']:
            body += struct.pack('<BB%df' % len(prm['values']), prm['slot'], prm['knob'], *prm['values'])
        self.set_chunk(SCENES, body)

    def mods(self):
        """The modulators (waveod/src/fw/preset_format.h struct preset_mod): MOD_COUNT dicts (type 'off' when
        unused): type, shape, sync (0 = rate in Hz, 1..11 = note division + 1), rate, steps, phase, glide, sens,
        attack, release, step (MOD_STEPS values 0..100)."""
        out = [mod_default('off') for _ in range(MOD_COUNT)]
        b = self.chunk(MODS) or b''
        for at in range(0, len(b) - MOD_BYTES + 1, MOD_BYTES):
            i, typ, shape, sync, rate, steps, phase, glide, sens, att, rel = struct.unpack_from('<BBBBfBBBbHH', b, at)
            if i >= MOD_COUNT or typ >= len(MOD_TYPES):
                continue
            out[i] = dict(type=MOD_TYPES[typ], shape=MOD_SHAPES[shape] if shape < len(MOD_SHAPES) else 'sine',
                          sync=sync, rate=rate, steps=steps, phase=phase, glide=glide, sens=sens, attack=att,
                          release=rel, step=list(b[at + 16:at + 16 + MOD_STEPS]))
        return out

    def set_mods(self, mods):
        """Store mods() as given (the chunk is left out when every one is off)."""
        body = bytearray()
        for i, m in enumerate(mods[:MOD_COUNT]):
            if m['type'] == 'off':
                continue
            body += struct.pack('<BBBBfBBBbHH', i, MOD_TYPES.index(m['type']), MOD_SHAPES.index(m['shape']),
                                m['sync'], m['rate'], m['steps'], m['phase'], m['glide'], m['sens'], m['attack'],
                                m['release'])
            body += bytes((list(m['step']) + [0] * MOD_STEPS)[:MOD_STEPS])
        self.chunks = [(t, b) for t, b in self.chunks if t != MODS]
        if body:
            self.chunks.append((MODS, bytes(body)))

    def set_controls(self, table):
        """Store a control table (controls.Table). The chunk then wins over the original's footswitch fields."""
        self.set_chunk(CONTROLS, table.encode())

    # ------------------------------------------------------------------ bytes
    def encode(self):
        out = bytearray(b'WP' + bytes([self.version, self.flags]))
        for tag, body in self.chunks:
            out += struct.pack('<BH', tag, len(body)) + body
        return bytes(out)

    @classmethod
    def decode(cls, data):
        data = bytes(data)
        if len(data) < 4 or data[:2] != b'WP' or data[2] < 1:
            raise FormatError(tr('py.preset_not_waveod'))
        p, at = cls(version=data[2], flags=data[3]), 4
        while at + 3 <= len(data):
            tag, n = struct.unpack_from('<BH', data, at)
            if at + 3 + n > len(data):
                raise FormatError(tr('py.preset_chunk', tag=tag))
            p.chunks.append((tag, data[at + 3:at + 3 + n]))
            at += 3 + n
        return p


def preset_label(i):
    """the pedal's numbering: 4 presets per bank, 01A .. 60D"""
    return '%02d%s' % (i // 4 + 1, 'ABCD'[i % 4])
