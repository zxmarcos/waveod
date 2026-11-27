"""Talking to the waveod firmware over USB MIDI: the editor protocol's packets (usb/htmidi.py) carrying
waveod's own group 0xF (src/editor/wv_msgs.c).

A Pedal opens the MIDI port for as long as it is used (`with Pedal() as p:`), so the firmware updater and the
original editor can take the port between operations.
"""
import struct
import time

from waveod_studio import controls, irs, wvlib
from waveod_studio.i18n import tr
from waveod_studio.i18n_lazy import TextList, Texts
from waveod_studio.usb import htmidi

FLASH, INFO, LIST, GET, PUT, LOAD, MOVE, COPY, DESCRIBE, GLOBAL, GLOBAL_PUT, LATENCY, LATENCY_GET = \
    range(0x1F000000, 0x1F00000D)
LIB_INFO, LIB_WRITE, LIB_COMMIT = range(0x1F00000D, 0x1F000010)      # protocol 5: waveod's effect library
LIB_SECTOR = 0x1000
# the original's group 8 (src/editor/media_msgs.c): Clone slot names, data (answered CLONE_ACK), rename
CLONE_NAMES, CLONE_DATA, CLONE_RENAME, CLONE_ACK = 0x18000000, 0x18000001, 0x18000002, 0x18000003
CLONE_SLOTS = 10
IR_NAMES, IR_DATA, IR_RENAME, IR_ACK = 0x14000000, 0x14000001, 0x14000002, 0x14000003   # user IRs (irs.py)
CLONES = 0x1F000010                                                 # protocol 6: what each Clone slot holds
CLONE_EMPTY, CLONE_ORIGINAL, CLONE_NAM, CLONE_NAM_48K, CLONE_NAM_BROKEN = range(5)
CLONE_KINDS = Texts({CLONE_EMPTY: 'py.empty', CLONE_ORIGINAL: 'py.clone_original', CLONE_NAM: 'py.clone_nam',
                     CLONE_NAM_48K: 'py.clone_nam48', CLONE_NAM_BROKEN: 'py.clone_broken'})
DRUMS, DRUM_GET, DRUM_PUT, DRUM_PLAY = range(0x1F000011, 0x1F000015)   # protocol 7: MEUS RITMOS (drums.py)
DRUM_BANK = 0xFFFFFFFF
RIG, BLOCK, CATALOG, EDIT = range(0x1F000015, 0x1F000019)          # protocol 8: the chain screen (wv_rig_msgs.c)
(EDIT_PARAM, EDIT_ON, EDIT_PLACE, EDIT_MOVE, EDIT_REMOVE, EDIT_REPLACE, EDIT_ROUTE, EDIT_JOINT, EDIT_SCENE,
 EDIT_VOLUME, EDIT_BPM, EDIT_SAVE,
 EDIT_SCENE_PICK, EDIT_SCENE_COPY, EDIT_SCENE_REVERT, EDIT_SCENE_NAME, EDIT_SCENE_PARAM,   # 12..: protocol 10
 EDIT_NAME) = range(18)                                                                    # protocol 12
SETTINGS, SETTINGS_PUT = 0x1F000019, 0x1F00001A                     # protocol 9: the GLOBAL screen (wv_settings_msgs.c)
FLASH_STATUS = 0x1F00001E                                           # protocol 13: the flash chip's status registers
LOCK_TEST = 0x1F00001F                                              # test builds only (WAVEOD_LOCK_TEST)
FLASH_LOCKS = TextList(('py.lock_none', 'py.lock_locked', 'py.lock_failed', 'py.lock_unsupported'))  # enum flash_lock
SETTING_IDS = dict(
    input_mode=1, input_level=2, aux=3, cabs=4, usb_mode=5, usb_rec=6, usb_mon=7, usb_wet=8, usb_aux=9,
    midi_source=10, midi_trs=11, midi_usb=12, clock_source=13, clock_out=14, eq_on=15, tempo=16, exp2_jack=17,
    ext1=18, ext2=19, exp1_min=20, exp1_max=21, exp2_min=22, exp2_max=23,
    a4=32, tuner_mute=33, tuner_strobe=34, brightness=35, cursor=36, cab_original=37, spill=38, scene_manual=39, language=40,
    cpu_temp=48, cpu_temp_max=49, cpu_temp_min=50)                  # read only: degrees C, None = unknown
SETTING_NAMES = {v: k for k, v in SETTING_IDS.items()}
SETTING_EQ = 64                                                     # + enum eq_param
SETTING_FLOATS = {48, 49, 50}                                       # the system's: tenths of a degree, NaN unknown
SETTING_ERRORS = Texts({-10: 'py.setting_unknown', -11: 'py.setting_pedal_only', -12: 'py.setting_range'})
ITEM_KINDS = ('library', 'builtin', 'module', 'missing', 'routing')
P_SYNCED, P_SCENE, P_LIST = 1, 2, 4
EDITED = 0xFFFF
# the latency test (src/engine/latency.h, protocol 4)
LAT_ROUTES = {'L': 0, 'R': 1, 'SEND': 2, 'PRESET': 3}
LAT_ROUTE_NAMES = TextList(('py.lat_route_l', 'py.lat_route_r', 'py.lat_route_send', 'py.lat_route_preset'))
LAT_BUSY = 1
LAT_STATUS = Texts({0: 'py.lat_none', 1: 'py.lat_busy', 2: 'py.lat_ok', 3: 'py.lat_unstable', 4: 'py.lat_no_signal',
                    5: 'py.lat_clipped', 6: 'py.cancelled'})
CHUNK = 0x2000
FLASH_SIZE = 0x800000
ORIGINS = Texts({0: 'py.origin_user', 1: 'py.origin_factory', 2: 'py.empty'})
PLACES = Texts({0: 'py.place_waveod', 1: 'py.place_original', 2: 'py.empty'})
ERRORS = Texts({-10: 'py.err_position', -11: 'py.err_flash', -12: 'py.err_preset', -21: 'py.err_args',
                -22: 'py.err_full', -23: 'py.err_flash'})


class PedalError(RuntimeError):
    pass


class NotWaveod(PedalError):
    """the pedal answers nothing in group 0xF: it runs another firmware"""


class Cancelled(PedalError):
    """a long request (a flash dump) stopped by its cancel()"""


def error_text(err):
    return ERRORS.get(err) or tr('py.err_n', n=err)


def _settings(r):
    """protocol 9's list (wv_settings_msgs.c) -> {name: dict(value, min, max, step, ro), 'eq': [...]}"""
    out, eq = {}, [None] * 31
    for k in range(r[0]):
        sid, flags = struct.unpack_from('<BB', r, 4 + 20 * k)
        v, lo, hi, step = struct.unpack_from('<4f', r, 8 + 20 * k)
        if sid in SETTING_FLOATS:
            v = None if v != v else round(v, 1)
        elif sid < SETTING_EQ:
            v = round(v)
        d = dict(value=v, min=lo, max=hi, step=step, ro=bool(flags & 1))
        if sid >= SETTING_EQ:
            if sid - SETTING_EQ < len(eq):
                eq[sid - SETTING_EQ] = d
        elif sid in SETTING_NAMES:
            out[SETTING_NAMES[sid]] = d
    out['eq'] = eq
    return out


class _Reader:
    """little-endian fields and {u8 n, bytes} texts (UTF-8) of an answer"""
    def __init__(self, data):
        self.d, self.at = bytes(data), 0

    def _take(self, fmt):
        v = struct.unpack_from(fmt, self.d, self.at)[0]
        self.at += struct.calcsize(fmt)
        return v

    def u8(self): return self._take('<B')
    def i8(self): return self._take('<b')
    def u16(self): return self._take('<H')
    def i16(self): return self._take('<h')
    def u32(self): return self._take('<I')
    def i32(self): return self._take('<i')
    def f32(self): return self._take('<f')

    def text(self):
        n = self.u8()
        s = self.d[self.at:self.at + n].decode('utf-8', 'replace')
        self.at += n
        return s

    def left(self):
        return len(self.d) - self.at


def _param(r, k):
    knob, flags = r.u8(), r.u8()
    p = dict(k=k, knob=knob, synced=bool(flags & P_SYNCED), scene=bool(flags & P_SCENE), min=r.f32(), max=r.f32(),
             step=r.f32(), default=r.f32(), value=r.f32(), name=r.text(), text=r.text())
    p['options'] = [r.text() for _ in range(r.u8())]
    return p


class Pedal:
    def __init__(self, port=None, transport=None, timeout=4.0):
        """port: MIDI port name / number (default: the pedal's); transport: an object with send(sysex) and
        receive() -> [sysex] used instead of a MIDI port (tests, the emulator)"""
        self.want, self.port, self.timeout = port, transport, timeout
        self.own = transport is None

    def __enter__(self):
        if self.port is None:
            self.port = htmidi.Port(self.want)
        return self

    def __exit__(self, *a):
        self.close()

    def close(self):
        if self.own and self.port is not None:
            self.port.close()
            self.port = None

    @property
    def name(self):
        return getattr(self.port, 'name', 'pedal')

    def request(self, cmd, payload=b'', timeout=None, retries=0):
        """send a request, return its answer; `retries` only for requests that change nothing (a second try
        after a timeout must not do anything twice)"""
        for attempt in range(retries + 1):
            try:
                return htmidi.transact(self.port, cmd, payload, flag=htmidi.FLAG_REQ, want=cmd,
                                       timeout=timeout or self.timeout, gap=0.0, log=lambda *a: None)
            except TimeoutError:
                if attempt == retries:
                    raise NotWaveod(tr('py.no_answer', cmd='%#010x' % cmd)) from None

    # ------------------------------------------------------------------ queries
    def info(self):
        # the first request of a connection: the pedal may still be sending what it queued while no app was
        # reading its USB MIDI (an older firmware queues its notifications), so it gets a second try
        r = self.request(INFO, retries=1)
        if len(r) < 52 or r[:4] != b'WVOD':
            raise NotWaveod(tr('py.bad_info'))
        proto, presets, active, dirty = struct.unpack_from('<HHHB', r, 4)
        sectors, used, free, live = struct.unpack_from('<4I', r, 12)
        return dict(protocol=proto, presets=presets, active=active, modified=bool(dirty),
                    store=dict(sectors=sectors, used=used, free=free, live=live),
                    version=r[28:52].split(b'\0')[0].decode('latin-1'))

    def flash_status(self):
        """the flash chip's JEDEC id, status registers 1-3 as the chip came up (`sr`) and as last read, after the
        lock (`sr_now`) (None: not read), the lock (FLASH_LOCKS index) and the pedal's flash diagnostics text
        (protocol 13; 14 tells the start-up values from the last read)"""
        r = self.request(FLASH_STATUS, retries=1)
        if len(r) < 12:
            raise PedalError(tr('py.flash_status_short'))
        jedec, = struct.unpack_from('<I', r)
        sr = [None if v < 0 else v for v in struct.unpack_from('<3h', r, 4)]
        head = r[11] or 12
        now = [None if v < 0 else v for v in struct.unpack_from('<3h', r, 12)] if head >= 18 else sr
        return dict(jedec=jedec or None, sr=sr, sr_now=now, lock=r[10],
                    diag=r[head:].split(b'\0')[0].decode('latin-1'))

    def lock_test(self):
        """the flash lock test of a WAVEOD_LOCK_TEST build: the top 4 KB (locked at start-up until power-off)
        erased and programmed, compared with a copy kept in RAM and put back if anything changed; `reads` = the
        status registers 1-3 read 8 times right before"""
        r = self.request(LOCK_TEST, struct.pack('<I', 0x4B434F4C), timeout=20)
        if len(r) < 24:
            raise PedalError(tr('py.lock_test_short'))
        state, erase, prog, diff, restored, spot = struct.unpack_from('<iiiIiI', r)
        reads = [tuple(r[24 + 3 * k:27 + 3 * k]) for k in range((len(r) - 24) // 3)]
        return dict(state=state, erase=erase, program=prog, changed_at=None if diff == 0xFFFFFFFF else diff,
                    restored=restored, spot=None if spot == 0xFFFFFFFF else spot, reads=reads)

    def presets(self):
        r = self.request(LIST)
        count, active = struct.unpack_from('<HH', r)
        out = []
        for i in range(count):
            e = r[4 + 19 * i:4 + 19 * (i + 1)]
            out.append(dict(index=i, origin=e[0], place=e[1], name=e[2:].split(b'\0')[0].decode('latin-1'),
                            active=i == active))
        return out

    def get(self, index):
        """(origin, waveod-format bytes) of preset `index` (EDITED: the edited one, unsaved edits included)"""
        r = self.request(GET, struct.pack('<I', index))
        idx, origin, n = struct.unpack_from('<IiI', r)
        if idx != index or origin < 0:
            raise PedalError(tr('py.get_failed', n=index))
        return origin, r[12:12 + n]

    def describe(self, index):
        """(Table, [Block]) of preset `index` (EDITED: the edited one): its control table as the pedal plays it
        (converted from the original's fields when it has none) and the names and ranges of its blocks'
        parameters (controls.py); needs protocol 2"""
        r = self.request(DESCRIBE, struct.pack('<I', index))
        idx, origin, table, blocks = controls.parse_describe(r)
        if idx != index or origin < 0 or table is None:
            raise PedalError(tr('py.describe_failed', n=index))
        return table, blocks

    def global_controls(self):
        """the global control table (controls.Table); needs protocol 3"""
        t = controls.Table.from_chunk(self.request(GLOBAL))
        if t is None:
            raise PedalError(tr('py.global_failed'))
        return t

    def latency_result(self):
        """the pedal's last latency test (protocol 4): status, route, runs, level_db, min / max / mean (samples),
        match, total runs, buffers (samples), rate, ms (the mean)"""
        r = self.request(LATENCY_GET)
        seq, status, route, runs, level = struct.unpack_from('<IBBBb', r)
        lo, hi, mean, match = struct.unpack_from('<4f', r, 8)
        total, buffers, rate = struct.unpack_from('<HHI', r, 24)
        return dict(seq=seq, status=status, status_text=LAT_STATUS.get(status, '?'), route=route, runs=runs,
                    level_db=level, min=lo, max=hi, mean=mean, match=match, total=total, buffers=buffers, rate=rate,
                    ms=mean * 1000.0 / rate if rate else 0.0)

    def measure_latency(self, route, progress=None, cancel=None, timeout=60.0):
        """run the pedal's latency test on route (LAT_ROUTES' values) and wait for it: latency_result(). A cable
        route needs the cable plugged in and plays a click on that output. The transport may offer
        latency_cable(route) (the emulator plugs its cable) and wait(seconds) (the emulator's time)."""
        cable = getattr(self.port, 'latency_cable', None)
        if cable:
            cable(route)
        err, = struct.unpack_from('<i', self.request(LATENCY, struct.pack('<I', route)))
        if err:
            raise PedalError(tr('py.lat_not_started', error=error_text(err)))
        wait = getattr(self.port, 'wait', time.sleep)
        waited = 0.0                                # the pedal's time (the emulator's runs slower than the clock)
        while True:
            wait(0.25)
            waited += 0.25
            res = self.latency_result()
            if progress:
                progress(min(res['runs'] + (res['status'] == LAT_BUSY), res['total']), res['total'])
            if res['status'] != LAT_BUSY:
                return res
            if (cancel and cancel()) or waited > timeout:
                self.request(LATENCY, struct.pack('<I', 0xFFFFFFFF))
                raise PedalError(tr('py.lat_cancelled') if cancel and cancel() else tr('py.lat_unfinished'))

    # ------------------------------------------------------------------ changes
    def set_global_controls(self, table):
        """store the global control table (the links that may not be global are left out by the pedal); it plays
        at once"""
        body = table.chunk()
        r = self.request(GLOBAL_PUT, struct.pack('<I', len(body)) + body, timeout=8.0)
        err, = struct.unpack_from('<i', r)
        if err:
            raise PedalError(tr('py.global_not_written', error=error_text(err)))

    def put(self, index, data):
        r = self.request(PUT, struct.pack('<II', index, len(data)) + bytes(data), timeout=8.0)
        idx, err = struct.unpack_from('<Ii', r)
        if err:
            raise PedalError(tr('py.put_failed', n=index, error=error_text(err)))

    def load(self, index):
        self.request(LOAD, struct.pack('<I', index))

    def move(self, src, dst):
        self._pair(MOVE, src, dst)

    def copy(self, src, dst):
        self._pair(COPY, src, dst)

    def _pair(self, cmd, a, b):
        r = self.request(cmd, struct.pack('<II', a, b), timeout=8.0)
        err = struct.unpack_from('<i', r, 8)[0]
        if err:
            raise PedalError(error_text(err))

    # ------------------------------------------------------------------ waveod's effect library (protocol 5)
    def library_info(self):
        """where the library is: offset, capacity, total (0 = none), crc"""
        off, cap, total, crc = struct.unpack_from('<4I', self.request(LIB_INFO))
        return dict(offset=off, capacity=cap, total=total, crc=crc)

    def library(self, progress=None):
        """the effect modules installed (wvlib.Library, empty when there is none)"""
        i = self.library_info()
        return wvlib.Library.parse(self.dump(i['offset'], i['total'], progress) if i['total'] else b'')

    def write_library(self, img, progress=None):
        """install a library image (wvlib.Library.pack()): written sector by sector, then checked by the pedal,
        which reads it again and rebuilds the edited preset (its edits kept)"""
        cap = self.library_info()['capacity']
        if len(img) > cap:
            raise PedalError(tr('py.lib_too_big', size=len(img) // 1024, capacity=cap // 1024))
        for off in range(0, len(img), LIB_SECTOR):
            chunk = img[off:off + LIB_SECTOR]
            got, err = struct.unpack_from('<Ii', self.request(LIB_WRITE, struct.pack('<II', off, len(chunk)) + chunk,
                                                              timeout=10.0))
            if got != off or err:
                raise PedalError(tr('py.lib_write_failed', offset='%#x' % off, error=error_text(err)))
            if progress:
                progress(off + len(chunk), len(img))
        err, = struct.unpack_from('<i', self.request(LIB_COMMIT, struct.pack('<I', len(img)), timeout=20.0))
        if err:
            raise PedalError(tr('py.lib_check_failed', error=error_text(err)))

    # ------------------------------------------------------------------ Clone slots (the original's group 8)
    def clone_names(self):
        """the names of the 10 Clone slots"""
        r = self.request(CLONE_NAMES)
        return [r[80 + 17 * i:80 + 17 * (i + 1)].split(b'\0')[0].decode('latin-1') for i in range(CLONE_SLOTS)]

    def clones(self):
        """what the 10 Clone slots hold (protocol 6): kind (CLONE_*), named (stored with a name), name; for a NAM
        capture also sample_rate, loudness, crc, flags"""
        r = self.request(CLONES)
        out = []
        for i in range(CLONE_SLOTS):
            kind, named = r[40 * i], bool(r[40 * i + 1])
            rate, loud, crc, flags = struct.unpack_from('<ffII', r, 40 * i + 4)
            e = dict(slot=i, kind=kind, named=named, name=r[40 * i + 20:40 * i + 40].split(b'\0')[0].decode('latin-1'))
            if kind in (CLONE_NAM, CLONE_NAM_48K, CLONE_NAM_BROKEN):
                e.update(sample_rate=rate, loudness=loud, crc=crc, flags=flags)
            out.append(e)
        return out

    def write_clone(self, slot, name, data):
        """store `data` (up to 0x2000 - 48 bytes: a capture from nam.capture) in Clone slot `slot` (0..9) under
        `name`; the pedal rebuilds the chain. This replaces what the slot held."""
        if not 0 <= slot < CLONE_SLOTS:
            raise PedalError(tr('py.clone_slot_bad', n=slot))
        nb = name.encode('latin-1', 'replace')[:16]
        payload = struct.pack('<III', slot, 0, sum(data) & 0xffffffff) + nb.ljust(20, b'\0') + bytes(data)
        r = htmidi.transact(self.port, CLONE_DATA, payload, flag=htmidi.FLAG_DATA, want=CLONE_ACK, timeout=10.0,
                            gap=0.0, log=lambda *a: None)
        got, ok = struct.unpack_from('<II', r)
        if got != slot or not ok:
            raise PedalError(tr('py.clone_not_written', n=slot + 1))

    # ------------------------------------------------------------------ user IRs (the original editor's group 4)
    def ir_names(self):
        """the names of the 20 user IR slots (the directory's "User IR n" for one never written)"""
        r = self.request(IR_NAMES)
        return [r[160 + 17 * i:160 + 17 * (i + 1)].split(b'\0')[0].decode('latin-1') for i in range(irs.SLOTS)]

    def user_irs(self):
        """what the 20 slots hold, read from the flash (irs.user_slots: the directory and the used blocks only):
        [None or dict(name, data)]"""
        return irs.user_slots(lambda off, n: self.dump(irs.FS_BASE + off, n))

    def write_ir(self, slot, name, data):
        """store `data` (4096 bytes: irs.pack) in user IR slot `slot` (0..19) under `name`; the pedal rebuilds
        the chain. This replaces what the slot held."""
        if not 0 <= slot < irs.SLOTS:
            raise PedalError(tr('py.ir_slot_bad', n=slot))
        if len(data) != irs.DATA_SIZE:
            raise PedalError(tr('py.ir_size', size=len(data), need=irs.DATA_SIZE))
        nb = name.encode('latin-1', 'replace')[:irs.NAME_MAX]
        payload = struct.pack('<III', slot, 0, sum(data) & 0xffffffff) + nb.ljust(20, b'\0') + bytes(data)
        r = htmidi.transact(self.port, IR_DATA, payload, flag=htmidi.FLAG_DATA, want=IR_ACK, timeout=10.0,
                            gap=0.0, log=lambda *a: None)
        got, ok = struct.unpack_from('<II', r)
        if got != slot or not ok:
            raise PedalError(tr('py.ir_not_written', n=slot + 1))

    def rename_ir(self, slot, name):
        """a slot under a new name (the pedal does not answer: the names are read again to see it)"""
        if not 0 <= slot < irs.SLOTS:
            raise PedalError(tr('py.ir_slot_bad', n=slot))
        nb = name.encode('latin-1', 'replace')[:irs.NAME_MAX]
        for p in htmidi.packets(htmidi.message(IR_RENAME, struct.pack('<I', slot) + nb.ljust(20, b'\0')),
                                htmidi.FLAG_DATA):
            self.port.send(p)

    # ------------------------------------------------------------------ MEUS RITMOS (protocol 7)
    def drums(self):
        """the drum patterns of waveod's own: dict(places, sounds, max_events, playing_place (None = the bank's
        pattern), playing, slots=[None or dict(place, once, bpm, events, name)])"""
        r = self.request(DRUMS)
        places, sounds, most, sel, playing = struct.unpack_from('<BBHBB', r)
        slots = []
        for i in range(places):
            used, flags, bpm, count = struct.unpack_from('<BBHH', r, 8 + 22 * i)
            name = r[8 + 22 * i + 6:8 + 22 * i + 22].split(b'\0')[0].decode('latin-1')
            slots.append(dict(place=i, once=bool(flags & 1), bpm=bpm, events=count, name=name) if used else None)
        return dict(places=places, sounds=sounds, max_events=most, playing_place=None if sel == 0xFF else sel,
                    playing=bool(playing), slots=slots)

    def drum_get(self, place):
        """the record stored in place (b'' when empty)"""
        r = self.request(DRUM_GET, struct.pack('<I', place))
        got, n = struct.unpack_from('<II', r)
        if got != place:
            raise PedalError(tr('py.drum_get_failed', n=place + 1))
        return r[8:8 + n]

    def drum_put(self, place, rec):
        """store a record (drums.record) in place, b'' empties it"""
        r = self.request(DRUM_PUT, struct.pack('<II', place, len(rec)) + bytes(rec), timeout=8.0)
        got, err = struct.unpack_from('<Ii', r)
        if err:
            raise PedalError(tr('py.drum_put_failed', n=place + 1,
                                error=tr('py.drum_invalid') if err == -12 else error_text(err)))

    def drum_play(self, place, on=True):
        """choose place (None: the bank's pattern) and start (on) or stop the drum machine"""
        r = self.request(DRUM_PLAY, struct.pack('<II', DRUM_BANK if place is None else place, 1 if on else 0))
        err, = struct.unpack_from('<i', r)
        if err:
            raise PedalError(tr('py.drum_empty', n=place + 1))

    # ------------------------------------------------------------------ the chain screen (protocol 8)
    def rig(self):
        """the edited preset as the pedal's chain screen shows it: gen (moves on every change), loads, index,
        dirty, volume, tempo, route {mode, split, merge, joints}, scene {active, editing, names}, name, order
        (slots in signal order), blocks [{slot, on, menu, index, id, cell, kind, params, name, type}]"""
        r = _Reader(self.request(RIG))
        out = dict(gen=r.u32(), loads=r.u32(), index=r.u16(), dirty=bool(r.u8()), volume=r.u8(), tempo=r.u16())
        out['route'] = dict(mode=r.u8(), split=r.u8(), merge=r.u8(), joints=[r.u8() for _ in range(6)])
        active, editing = r.u8(), bool(r.u8())
        out['name'] = r.text()
        out['scene'] = dict(active=active, editing=editing, names=[r.text() for _ in range(4)])
        out['order'] = [r.i8() for _ in range(r.u8())]
        blocks = []
        for _ in range(r.u8()):
            b = dict(slot=r.u8(), on=bool(r.u8()), menu=r.i8(), index=r.i16(), id=r.u32(), cell=r.i8())
            kind = r.u8()
            b.update(kind=ITEM_KINDS[kind] if kind < len(ITEM_KINDS) else '?', params=r.u8(), name=r.text(),
                     type=r.text())
            blocks.append(b)
        out['blocks'] = blocks
        return out

    def block(self, slot):
        """the parameters of the block in `slot`, in library order: [{k, knob, synced, scene, min, max, step,
        default, value, name, text, options}] (texts as the pedal shows them)"""
        r = _Reader(self.request(BLOCK, struct.pack('<I', slot)))
        got, err = r.u32(), r.i32()
        if got != slot or err:
            raise PedalError(tr('py.block_missing', n=slot))
        return [_param(r, k) for k in range(r.u8())]

    def catalog(self, menu):
        """everything a slot of `menu` can hold: [{index, id, kind, name, type}] (type = the browser's group)"""
        items, first = [], 0
        while True:
            r = _Reader(self.request(CATALOG, struct.pack('<II', menu, first)))
            got, count, _first, n = r.u32(), r.u32(), r.u32(), r.u8()
            for _ in range(n):
                index, id_, kind = r.u16(), r.u32(), r.u8()
                items.append(dict(index=index, id=id_, kind=ITEM_KINDS[kind] if kind < len(ITEM_KINDS) else '?',
                                  name=r.text(), type=r.text()))
            if not n or not items or items[-1]['index'] + 1 >= count:
                return items
            first = items[-1]['index'] + 1

    def edit(self, op, a=0, b=0, c=0, value=0.0, text=None):
        """a live edit (EDIT_*, wv_rig_msgs.c): {result, gen}, plus `param` for EDIT_PARAM; `text` goes after the
        value ({u8 n, bytes}: EDIT_SCENE_NAME's or EDIT_NAME's name)"""
        payload = struct.pack('<IiiIf', op, a, b, c, value)
        if text is not None:
            t = str(text).encode('utf-8')[:255]
            payload += bytes([len(t)]) + t
        r = _Reader(self.request(EDIT, payload))
        r.u32()
        out = dict(result=r.i32(), gen=r.u32())
        if op == EDIT_PARAM and out['result'] == 0 and r.left():
            out['param'] = _param(r, b)
        return out

    # ------------------------------------------------------------------ the GLOBAL screen's settings (protocol 9)
    def settings(self):
        """every setting of the pedal's GLOBAL screen: {name: dict(value, min, max, step, ro)} and the global EQ's
        31 parameters as 'eq' (a list in enum eq_param order) (wv_settings_msgs.c)"""
        return _settings(self.request(SETTINGS))

    def set_settings(self, changes):
        """change settings ({name: value}, 'eq' as {parameter: value}) the way the GLOBAL screen does; the settings
        afterwards (as settings()). The first one refused raises PedalError (the ones before it are changed)."""
        pairs = []
        for name, v in changes.items():
            if name == 'eq':
                pairs += [(SETTING_EQ + int(k), float(x)) for k, x in dict(v).items()]
            else:
                if name not in SETTING_IDS:
                    raise PedalError(tr('py.setting_unknown_name', name=name))
                pairs.append((SETTING_IDS[name], float(v)))
        payload = struct.pack('<I', len(pairs)) + b''.join(struct.pack('<If', i, v) for i, v in pairs)
        r = self.request(SETTINGS_PUT, payload, timeout=8.0)
        err, bad = struct.unpack_from('<iI', r)
        out = _settings(r[8:])
        if err:
            what = 'EQ %d' % (bad - SETTING_EQ) if bad >= SETTING_EQ else SETTING_NAMES.get(bad, bad)
            raise PedalError(tr('py.setting_refused', what=what, error=SETTING_ERRORS.get(err) or tr('py.err_n', n=err)))
        return out

    # ------------------------------------------------------------------ flash
    def read_flash(self, off, n):
        for attempt in range(4):
            try:
                r = self.request(FLASH, struct.pack('<II', off, n))
            except NotWaveod:
                if attempt == 3:
                    raise
                time.sleep(0.2)
                continue
            got_off, got_n = struct.unpack_from('<II', r)
            if got_off == off and got_n == n and len(r) == 8 + n:
                return r[8:]
        raise PedalError(tr('py.flash_read_failed', offset='%#08x' % off))

    def dump(self, start, length, progress=None, cancel=None):
        out = bytearray()
        for off in range(start, start + length, CHUNK):
            if cancel and cancel():
                raise Cancelled(tr('py.cancelled'))
            out += self.read_flash(off, min(CHUNK, start + length - off))
            if progress:
                progress(len(out), length)
        return bytes(out)
