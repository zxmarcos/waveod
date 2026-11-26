"""The preset's control table (waveod/src/fw/preset_format.h, struct preset_controls; chunk 11 of the preset
format, fw/wv_preset.c): links "key -> what" per footswitch mode, and a name and LED colour per footswitch.

Keys are the footswitches, their pairs, the active preset's key again, EXP 1's toe switch, the pedals (EXP 1A /
1B / 2), the preset's modulators (MOD 1-4) and the MIDI control changes (MIDI_KEY + number). The pedal's own describe (protocol.Pedal.describe)
hands over the table as the pedal plays it (converted from the original's fields when the preset has none) and
the names of the blocks and parameters it can point at.

The pedal also has a global table (protocol 3: Pedal.global_controls), the same format with functions, scenes and
MIDI out only: a key's group (key, mode, the press or the long press links) runs from the preset's table when that
has a link there (a NOTHING link too), else from the global one.
"""
import struct

# keys
A, B, C, D, BANK_DOWN, BANK_UP = range(6)
COMBO = 6                                   # .. +3: A+B, B+C, C+D, BANK- + BANK+
AGAIN, TOE, EXP1A, EXP1B, EXP2, MOD1 = range(10, 16)  # MOD1 .. +3: the preset's modulators (wvpreset.Preset.mods)
KEY_COUNT = MOD1 + 4
MIDI_KEY = 0x80
FOOTSWITCH_KEYS = 6                         # the ones with a name and a colour
# modes (bits), when, actions, curves, flags
PRESET, STOMP, SCENE, ALL = 1, 2, 4, 7
PRESS, HOLD, MOMENT = range(3)
NONE, FN, BLOCK, PARAM, SCENE_ACT, MIDI, NOTHING = range(7)    # NOTHING: not even the global links
ACTION_COUNT = 7
LINEAR, SLOW, FAST = range(3)
CF_ORDER, CF_PC = 1, 2
SCENE_NEXT, SCENE_PREV = 0xFE, 0xFD
SCENE_COUNT = 4
CONTROL_MAX, LABEL_LEN = 48, 8
SLOTS, KNOBS = 12, 15
TAG = 11

KEY_NAMES = ('A', 'B', 'C', 'D', 'BANK −', 'BANK +', 'A + B', 'B + C', 'C + D', 'BANK − E +', 'PRESET ATIVO',
             'BICO DO EXP 1', 'EXP 1A', 'EXP 1B', 'EXP 2', 'MOD 1', 'MOD 2', 'MOD 3', 'MOD 4')
MODE_NAMES = {PRESET: 'PRESET', STOMP: 'STOMP', SCENE: 'CENAS'}
WHEN_NAMES = ('APERTAR', 'SEGURAR', 'PISANDO')
CURVE_NAMES = ('LINEAR', 'LENTA', 'RÁPIDA')
COLOUR_NAMES = ('AUTOMÁTICA', 'VERMELHO', 'VERDE', 'AMARELO', 'AZUL', 'MAGENTA', 'CIANO', 'BRANCO')

# footswitch functions (enum fs_function), in the order the pedal offers them (ui_functions in ui/ui.c)
FUNCTIONS = ((8, 'TAP'), (7, 'AFINADOR'), (5, 'LOOPER'), (6, 'BATERIA'), (19, 'RITMO -'), (18, 'RITMO +'),
             (20, 'EXP A/B'), (12, 'PRESET -'), (11, 'PRESET +'), (10, 'BANCO -'), (9, 'BANCO +'),
             (17, 'BANCOS'), (13, 'PRESET A'), (14, 'PRESET B'), (15, 'PRESET C'), (16, 'PRESET D'), (23, 'MODO'))
FN_NAMES = dict(FUNCTIONS)
FN_NAMES.update({0: '-', 1: 'CTRL 1', 2: 'CTRL 2', 3: 'CTRL 3', 4: 'CTRL 4'})


def midi_cc_ok(cc):
    return 0 < cc < 128 and cc not in (68, 69)


def is_midi(key):
    return key >= MIDI_KEY


def is_pedal(key):
    return EXP1A <= key <= EXP2


def is_mod(key):
    """a modulator of the preset (parameter links only, moved by the pedal's audio engine)"""
    return MOD1 <= key < MOD1 + 4


def sweeps(key):
    """a pedal, a MIDI CC or a modulator: its links follow a position (heel / toe values, a curve)"""
    return is_pedal(key) or is_midi(key) or is_mod(key)


def every_mode(key):
    """the toe switch, the pedals and MIDI work in every mode"""
    return key >= TOE


def key_valid(key):
    return 0 <= key < KEY_COUNT or (is_midi(key) and midi_cc_ok(key - MIDI_KEY))


def key_name(key):
    if is_midi(key):
        return 'MIDI CC %d' % (key - MIDI_KEY)
    return KEY_NAMES[key] if 0 <= key < KEY_COUNT else '?'


def fn_name(fn):
    return FN_NAMES.get(fn, 'FUNÇÃO %d' % fn)


def keys():
    """every key but MIDI's, in the pedal's order"""
    return list(range(KEY_COUNT))


class Link:
    __slots__ = ('key', 'modes', 'when', 'action', 'slot', 'arg', 'curve', 'flags', 'off', 'on')

    def __init__(self, key=A, modes=STOMP, when=PRESS, action=FN, slot=-1, arg=0, curve=LINEAR, flags=0,
                 off=0.0, on=0.0):
        self.key, self.modes, self.when, self.action = key, modes, when, action
        self.slot, self.arg, self.curve, self.flags, self.off, self.on = slot, arg, curve, flags, off, on

    def copy(self):
        return Link(*(getattr(self, k) for k in self.__slots__))

    def __eq__(self, o):
        return isinstance(o, Link) and all(getattr(self, k) == getattr(o, k) for k in self.__slots__)

    def __repr__(self):
        return 'Link(%s)' % ', '.join('%s=%r' % (k, getattr(self, k)) for k in self.__slots__)

    @property
    def pc(self):
        return self.action == MIDI and bool(self.flags & CF_PC)


class Table:
    def __init__(self, links=None, colour=None, label=None):
        self.links = list(links or [])
        self.colour = list(colour or [0] * FOOTSWITCH_KEYS)
        self.label = list(label or [''] * FOOTSWITCH_KEYS)

    def copy(self):
        return Table([l.copy() for l in self.links], self.colour, self.label)

    # ------------------------------------------------------------------ the chunk (fw/wv_preset.c)
    def encode(self):
        """the chunk's body"""
        out = bytearray([len(self.links)])
        for l in self.links:
            out += struct.pack('<BBBBbB', l.key, l.modes, l.when, l.action, l.slot, l.arg)
            if sweeps(l.key) or l.action == MIDI:
                out += bytes([l.curve, l.flags])
            if l.action in (PARAM, MIDI):
                out += struct.pack('<ff', l.off, l.on)
        for k in range(FOOTSWITCH_KEYS):
            name = self.label[k].encode('latin-1', 'replace')[:LABEL_LEN]
            out += bytes([self.colour[k], len(name)]) + name
        return bytes(out)

    @classmethod
    def decode(cls, b):
        """the chunk's body, as the firmware reads it (links it would drop are dropped); None = malformed"""
        b = bytes(b)
        if len(b) < 1:
            return None
        t, at = cls(), 1
        for _ in range(b[0]):
            if at + 6 > len(b):
                return None
            key, modes, when, action, slot, arg = struct.unpack_from('<BBBBbB', b, at)
            l = Link(key, modes, when, action, slot, arg)
            at += 6
            if sweeps(key) or action == MIDI:
                if at + 2 > len(b):
                    return None
                l.curve = b[at] if b[at] < 3 else LINEAR
                l.flags = b[at + 1]
                at += 2
            if action in (PARAM, MIDI):
                if at + 8 > len(b):
                    return None
                l.off, l.on = struct.unpack_from('<ff', b, at)
                at += 8
            midi_ok = action == MIDI and 0 <= slot < 16 and arg < 128 and 0 <= l.off <= 127 and 0 <= l.on <= 127
            if (key_valid(key) and NONE < action < ACTION_COUNT and when <= MOMENT and
                    (action in (FN, SCENE_ACT, NOTHING) or midi_ok or (action != MIDI and 0 <= slot < SLOTS)) and
                    (action != PARAM or (l.flags & CF_ORDER) or arg < KNOBS) and len(t.links) < CONTROL_MAX):
                t.links.append(l)
        for k in range(FOOTSWITCH_KEYS):
            if at + 2 > len(b) or at + 2 + b[at + 1] > len(b):
                break
            t.colour[k] = b[at]
            t.label[k] = b[at + 2:at + 2 + min(b[at + 1], LABEL_LEN)].split(b'\0')[0].decode('latin-1')
            at += 2 + b[at + 1]
        return t

    # ------------------------------------------------------------------ queries
    def of(self, key, mode=ALL):
        """table indexes of key's links in `mode` (a bit; every mode for the keys that have no modes)"""
        return [i for i, l in enumerate(self.links) if l.key == key and (every_mode(key) or l.modes & mode)]

    def midi_keys(self):
        return sorted({l.key for l in self.links if is_midi(l.key)})

    def group(self, key, mode, hold):
        """table indexes of a key's group in `mode`: the press (and while down) links, or the long press ones"""
        return [i for i in self.of(key, mode) if (self.links[i].when == HOLD) == bool(hold)]

    def chunk(self):
        """the whole chunk (tag, length, body), as the global table travels"""
        body = self.encode()
        return struct.pack('<BH', TAG, len(body)) + body

    @classmethod
    def from_chunk(cls, b):
        if len(b) < 3 or b[0] != TAG or 3 + struct.unpack_from('<H', b, 1)[0] > len(b):
            return None
        return cls.decode(b[3:3 + struct.unpack_from('<H', b, 1)[0]])

    def own(self, i, mode):
        """link i made `mode`'s own: a link shared with other modes splits (the copy right after it), like the
        pedal's links screen; its index"""
        l = self.links[i]
        if every_mode(l.key) or l.modes == mode:
            return i
        if len(self.links) >= CONTROL_MAX:
            raise ValueError('tabela de controles cheia (%d ligações)' % CONTROL_MAX)
        c = l.copy()
        c.modes = mode
        l.modes &= ~mode
        self.links.insert(i + 1, c)
        return i + 1

    def add(self, link):
        if len(self.links) >= CONTROL_MAX:
            raise ValueError('tabela de controles cheia (%d ligações)' % CONTROL_MAX)
        self.links.append(link)
        return len(self.links) - 1

    def remove(self, i, mode):
        """the link out of `mode` (out of the table when that was its only mode)"""
        l = self.links[i]
        if not every_mode(l.key) and l.modes != mode and l.modes & mode:
            l.modes &= ~mode
        else:
            del self.links[i]


def global_ok(link):
    """whether a link may be in the global table (nothing tied to a preset's blocks)"""
    return link.action in (FN, SCENE_ACT, MIDI)


def inherited(table, glob, key, mode):
    """indexes in the global table `glob` of key's links that run in a preset with `table`: the groups (press,
    long press) the preset has no link in"""
    if glob is None:
        return []
    return [i for hold in (0, 1) if not table.group(key, mode, hold) for i in glob.group(key, mode, hold)]


def runs(table, glob, key, mode):
    """[(Link, from_global)] key does in `mode` (NOTHING links left out)"""
    out = [(table.links[i], False) for i in table.of(key, mode)]
    out += [(glob.links[i], True) for i in inherited(table, glob, key, mode)]
    return [(l, g) for l, g in out if l.action != NOTHING]


def new_link(key, mode):
    """a link that does nothing yet (the pedal's + NOVA LIGAÇÃO): the editor gives it a destination"""
    return Link(key, ALL if every_mode(key) else mode, PRESS, FN, -1, 0)


# ---------------------------------------------------------------------- blocks and parameters (describe)
class Param:
    __slots__ = ('index', 'knob', 'name', 'min', 'max', 'step')

    def __init__(self, index, knob, name, lo, hi, step):
        self.index, self.knob, self.name, self.min, self.max, self.step = index, knob, name, lo, hi, step

    def fmt(self, v):
        step = self.step if self.step > 0 else 1
        if abs(step - round(step)) < 1e-6 and abs(self.min - round(self.min)) < 1e-6:
            return '%d' % round(v)
        digits = 1 if step >= 0.1 - 1e-6 else 2
        return '%.*f' % (digits, v)

    def quantize(self, v):
        step = self.step if self.step > 0 else 1.0
        v = min(max(v, self.min), self.max)
        n = int((v - self.min) / step + 0.5)
        return min(self.min + n * step, self.max)


class Block:
    __slots__ = ('slot', 'menu', 'name', 'params')

    def __init__(self, slot, menu, name, params):
        self.slot, self.menu, self.name, self.params = slot, menu, name, params

    def by_knob(self, knob):
        return next((p for p in self.params if p.knob == knob), None)


def parse_describe(r):
    """describe's answer -> (index, origin, Table, [Block]) (protocol.Pedal.describe)"""
    index, origin = struct.unpack_from('<Ii', r)
    if origin < 0:
        return index, origin, None, []
    at = 8
    tag, n = struct.unpack_from('<BH', r, at)
    if tag != TAG:
        raise ValueError('describe: control table missing')
    table = Table.decode(r[at + 3:at + 3 + n])
    at += 3 + n
    blocks = []
    count, at = r[at], at + 1
    for _ in range(count):
        slot, menu, ln = struct.unpack_from('<BbB', r, at)
        at += 3
        name = r[at:at + ln].decode('latin-1')
        at += ln
        nparams, at = r[at], at + 1
        params = []
        for k in range(nparams):
            knob, pn = r[at], r[at + 1]
            pname = r[at + 2:at + 2 + pn].decode('latin-1')
            at += 2 + pn
            lo, hi, step = struct.unpack_from('<3f', r, at)
            at += 12
            params.append(Param(k, knob, pname, lo, hi, step))
        blocks.append(Block(slot, menu, name, params))
    return index, origin, table, blocks


# ---------------------------------------------------------------------- editing (model/control.c)
def link_param(link, blocks):
    """the Param a parameter link moves, or None"""
    blk = next((b for b in blocks if b.slot == link.slot), None)
    if not blk or link.action != PARAM:
        return None
    if link.flags & CF_ORDER:
        return blk.params[link.arg] if link.arg < len(blk.params) else None
    return blk.by_knob(link.arg)


def set_param(link, block, param, knobs=None):
    """make link a parameter link to `param` of `block` with the pedal's defaults: a pedal or a MIDI CC sweeps the
    whole range, a key goes from the value now (knobs: the slot's knob values) to the other end"""
    link.action, link.slot, link.arg = PARAM, block.slot, param.knob
    link.flags &= ~(CF_ORDER | CF_PC)
    if sweeps(link.key):
        link.off, link.on = param.min, param.max
    else:
        now = knobs[param.knob] if knobs and param.knob < len(knobs) else param.min
        link.off = now
        link.on = param.max if now < param.max else param.min


def set_midi(link, program):
    """make link a MIDI out link with the pedal's defaults (control_set_midi)"""
    channel = link.slot & 15 if link.action == MIDI else 0
    link.action, link.slot = MIDI, channel
    link.arg = 0 if program else link.key - MIDI_KEY if is_midi(link.key) else 80
    link.flags = CF_PC if program else 0
    link.off, link.on = 0.0, 127.0


def set_simple(link, action, slot, arg):
    """a block, scene or function link"""
    link.action, link.slot, link.arg, link.flags = action, slot if action == BLOCK else -1, arg, 0


# ---------------------------------------------------------------------- text
def scene_text(arg):
    return 'PRÓXIMA CENA' if arg == SCENE_NEXT else 'CENA ANTERIOR' if arg == SCENE_PREV else 'CENA %d' % (arg + 1)


def block_choice_text(link):
    if is_pedal(link.key):
        return 'LIGA AO MOVER' if link.arg else 'COM O GRUPO'
    return 'DESLIGA O BLOCO' if link.arg else 'LIGA O BLOCO'


def link_text(link, blocks):
    """what a link does, in the pedal's words"""
    blk = next((b for b in blocks if b.slot == link.slot), None)
    bname = blk.name if blk else 'bloco %d' % (link.slot + 1)
    if link.action == BLOCK:
        return '%s · %s' % (bname, block_choice_text(link))
    if link.action == PARAM:
        p = link_param(link, blocks)
        if not p:
            return '%s · parâmetro ?' % bname
        text = '%s · %s %s » %s' % (bname, p.name.upper(), p.fmt(link.off), p.fmt(link.on))
        if sweeps(link.key) and link.curve != LINEAR:
            text += ' · ' + CURVE_NAMES[link.curve % 3]
        return text
    if link.action == SCENE_ACT:
        return scene_text(link.arg)
    if link.action == MIDI:
        if link.pc:
            return 'MIDI PC %d · CANAL %d' % (link.arg, (link.slot & 15) + 1)
        text = 'MIDI CC %d %d » %d · CANAL %d' % (link.arg, round(link.off), round(link.on), (link.slot & 15) + 1)
        if sweeps(link.key) and link.curve != LINEAR:
            text += ' · ' + CURVE_NAMES[link.curve % 3]
        return text
    if link.action == NOTHING:
        return 'NADA (SEM AS GLOBAIS)'
    if link.action == FN and link.arg == 0:
        return '(nada ainda)'
    return fn_name(link.arg)


def brief(link, blocks):
    """a link in a word or two (the block, the parameter, the scene, the function, the MIDI message)"""
    blk = next((b for b in blocks if b.slot == link.slot), None)
    if link.action == BLOCK:
        return blk.name if blk else '?'
    if link.action == PARAM:
        p = link_param(link, blocks)
        return p.name if p else '?'
    if link.action == MIDI:
        return '» %s %d' % ('PC' if link.pc else 'CC', link.arg)
    return link_text(link, blocks)


def when_text(link):
    """the press kind of a footswitch link; '' for the keys that have none"""
    return '' if every_mode(link.key) else WHEN_NAMES[link.when % 3]
