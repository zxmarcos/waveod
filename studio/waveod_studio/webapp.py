"""waveod Studio's new interface: the page in ui/ (Svelte, built into waveod_studio/web by `npm run build`) in a
pywebview window, talking to the pedal through this module.

    python -m waveod_studio --web                     the pedal on USB
    python -m waveod_studio --web --emu FW.bin        the firmware in the emulator (tools/emu.py)
    python -m waveod_studio --web --dev               the page from Vite's dev server (cd ui && npm run dev)

The page calls the methods of Api (window.pywebview.api); they reach the pedal one at a time. The port stays
open while the window is: close the original editor first, the MIDI port is exclusive.
"""
import argparse
import json
import os
import re
import struct
import sys
import threading
import time

from waveod_studio import backup as B
from waveod_studio import bench as BN
from waveod_studio import clonefit as CF
from waveod_studio import controls as CT
from waveod_studio import drums as D
from waveod_studio import flashmap as FM
from waveod_studio import irs as IRS
from waveod_studio import nam as NAM
from waveod_studio import protocol as P
from waveod_studio import wvlib as WL
from waveod_studio import wvpreset as WP
from waveod_studio.usb import htmidi
from waveod_studio.usb import qmeupdate as U

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, 'web')
DEV_URL = 'http://localhost:5173'
NEEDS = 8                                           # the protocol of the chain screen (src/editor/wv_rig_msgs.c)
SEARCH_S, RETRY_S, WATCH_S = 1.5, 3.0, 2.0         # looking for the pedal; a port that failed; a connected one
LOST = 'O pedal foi desconectado.'
PRESET_FILES = ('Preset do waveod (*.wvp)',)
BACKUP_FILES = ('Backup do waveod (*.wvbak)',)
FIRMWARE_FILES = ('Firmware do pedal (*.bin)',)     # pywebview takes only letters, digits and spaces before '('
LOG_DIR = os.path.join(os.path.expanduser('~/Library/Application Support') if sys.platform == 'darwin' else
                       os.environ.get('APPDATA', os.path.expanduser('~')), 'waveodStudio')
NAM_FILES = ('Captura NAM (*.nam)',)


def nam_arch(m):
    """a .nam's network in words: 'WaveNet A1 (16 / 8 canais)', 'A2 (SlimmableContainer)', 'LSTM', ..."""
    arch = str(m.get('architecture', '?'))
    cfg = m.get('config') or {}
    if arch == 'SlimmableContainer':
        return 'A2 (SlimmableContainer)'
    if arch == 'WaveNet':
        layers = cfg.get('layers') or []
        ch = [l.get('channels') for l in layers if isinstance(l, dict)]
        if ch:
            kind = 'A1' if ch == [16, 8] else 'A2' if len(ch) == 1 else ''
            return ('WaveNet %s (%s canais)' % (kind, ' / '.join(str(c) for c in ch))).replace('  ', ' ')
    return arch
MODULE_FILES = ('Módulo de efeito do waveod (*.elf)',)
BENCH_FILES = ('Módulo ou fonte de um bloco (*.elf;*.c)', 'Módulo (*.elf)', 'Fonte de um bloco (*.c)')
DUMP_FILES = ('Imagem da flash (*.bin)',)
IR_FILES = ('Resposta ao impulso (*.wav)',)
MIDI_FILES = ('Arquivo MIDI (*.mid;*.midi)', 'Todos os arquivos (*.*)')


# ---------------------------------------------------------------- the grid as the pedal lays it out on loading
# (src/model/route.c route_fit: a port, so a stored preset shows here where the pedal will put its blocks)
COLS, CELLS = 6, 12
PRE, LANE_A, LANE_B, POST, HIDDEN = 0, 1, 2, 3, -1


def route_lane(mode, split, merge, cell):
    if cell < 0 or cell >= CELLS:
        return HIDDEN
    if mode == 0:
        return PRE
    if cell >= COLS:
        return LANE_B
    end = COLS if mode == 2 else merge
    col = cell % COLS
    return PRE if col < split else LANE_A if col < end else POST


def route_path(mode, merge):
    if mode == 0:
        return list(range(CELLS))
    end = COLS if mode == 2 else merge
    return list(range(end)) + list(range(COLS, CELLS)) + list(range(end, COLS))


def route_fit(mode, split, merge, cells, order):
    """(mode, split, merge, cells) as the pedal plays them: cells[slot] for the slots of `order` (the chain)"""
    def default():
        c = [-1] * len(cells)
        for pos, s in enumerate(order):
            c[s] = pos
        return c
    if mode > 2 or split >= COLS or merge <= split or merge > COLS:
        return 0, 1, 4, default()
    cells = list(cells)
    valid = [False] * len(cells)
    seen, ok = set(), True
    chain = set(order)
    for s in range(len(cells)):
        if s not in chain:
            cells[s] = -1
            continue
        c = cells[s]
        if route_lane(mode, split, merge, c) == HIDDEN or c in seen:
            ok = False
            continue
        seen.add(c)
        valid[s] = True
    path = route_path(mode, merge)
    if ok:
        by_cell = {cells[s]: s for s in order}
        if [by_cell[c] for c in path if c in by_cell] == list(order):
            return mode, split, merge, cells
    # repack: the blocks keep their lanes (one without a usable cell joins the lane of the one before it), packed
    # from the start of each lane in chain order; a lane that overflows sends the chain back to series
    lane_cells = {PRE: [], LANE_A: [], LANE_B: [], POST: []}
    for c in path:
        lane_cells[route_lane(mode, split, merge, c)].append(c)
    prev = PRE if mode == 0 or split > 0 else LANE_A
    lanes = []
    for s in order:
        lane = route_lane(mode, split, merge, cells[s]) if valid[s] else prev
        lane = max(lane, prev)
        lanes.append(lane)
        prev = lane
    used = {k: 0 for k in lane_cells}
    out = list(cells)
    for s, lane in zip(order, lanes):
        if used[lane] >= len(lane_cells[lane]):
            return 0, split, merge, default()
        out[s] = lane_cells[lane][used[lane]]
        used[lane] += 1
    return mode, split, merge, out


def preset_view(index, origin, data):
    """what the presets screen shows of a stored preset: its fields, its blocks in signal order with the grid
    cells the pedal gives them when it loads the preset (route_fit over the route chunk; without the chunk the
    pedal's default: series, the chain order row by row) and its scenes"""
    pr = WP.Preset.decode(data)
    m = pr.main() or {}
    chain = pr.chain()
    rb = pr.chunk(WP.ROUTE)
    order = [s.slot for s in chain]
    if rb and len(rb) >= 4 + WP.SLOTS and rb[1] < len(WP.ROUTE_MODES):
        cells = [c - 256 if c > 127 else c for c in rb[4:4 + WP.SLOTS]]
        mode, split, merge, cells = route_fit(rb[1], rb[2], rb[3], cells, order)
    else:
        mode, split, merge, cells = route_fit(0, 1, 4, [-1] * WP.SLOTS, order)
    sc = pr.scenes()
    return dict(
        index=index, origin=origin, name=pr.name, style=pr.style.strip(), volume=m.get('volume'),
        bpm=m.get('bpm'), bytes=len(data), route=dict(mode=mode, split=split, merge=merge),
        blocks=[dict(slot=s.slot, on=bool(s.on), menu=s.menu, index=s.index, id=s.id, cell=cells[s.slot])
                for s in chain],
        scenes=dict(own=pr.chunk(WP.SCENES) is not None, active=sc['active'],
                    names=[x['name'] for x in sc['scenes']],
                    on=[[s for s, on in x['on'].items() if on] for x in sc['scenes']],
                    params=len(sc['params'])),
        mods=sum(1 for x in pr.mods() if x['type'] != 'off'),
        controls=pr.chunk(WP.CONTROLS) is not None)


def module_view(m):
    """an effect module (wvlib.Module) as the effects screen shows it"""
    return dict(id=m.id, hex='%08X' % m.id, menu=m.menu, name=m.name, category=m.category, flags=m.flags,
                state=m.state, buffer=m.buffer, size=len(m.elf),
                params=[dict(name=q['name'] or '', min=q['min'], max=q['max'], step=q['step'], default=q['default'],
                             unit=q['unit'] or '', options=q['options'] or [], knob=q['knob'], sync=bool(q['sync']))
                        for q in m.params])


def table_view(t):
    """a control table (controls.Table) as the page edits it"""
    return dict(links=[{k: getattr(l, k) for k in CT.Link.__slots__} for l in t.links], colour=list(t.colour),
                label=list(t.label))


def table_from(d, is_global=False):
    """the page's table back into a controls.Table, checked the way the pedal reads it: links still without a
    destination are left out (as the classic editor does), the global table keeps what may be global; a link the
    pedal would drop is an error instead of being lost silently"""
    links = []
    for n, x in enumerate(d.get('links') or []):
        try:
            l = CT.Link(int(x['key']), int(x['modes']) & CT.ALL, int(x['when']), int(x['action']), int(x['slot']),
                        int(x['arg']), int(x.get('curve', 0)), int(x.get('flags', 0)), float(x.get('off', 0)),
                        float(x.get('on', 0)))
        except (KeyError, TypeError, ValueError):
            raise RuntimeError('ligação %d malformada' % (n + 1)) from None
        if l.action == CT.FN and l.arg == 0:
            continue
        if is_global and not CT.global_ok(l):
            continue
        if not CT.every_mode(l.key) and not l.modes:
            continue
        links.append(l)
    if len(links) > CT.CONTROL_MAX:
        raise RuntimeError('a tabela tem %d ligações; o pedal guarda até %d' % (len(links), CT.CONTROL_MAX))
    colour = [int(c) & 7 for c in (list(d.get('colour') or []) + [0] * CT.FOOTSWITCH_KEYS)[:CT.FOOTSWITCH_KEYS]]
    label = [str(s).upper()[:CT.LABEL_LEN] for s in (list(d.get('label') or []) + [''] * CT.FOOTSWITCH_KEYS)
             [:CT.FOOTSWITCH_KEYS]]
    t = CT.Table(links, colour, label)
    back = CT.Table.decode(t.encode())
    if back is None or len(back.links) != len(links):
        bad = next((k for k, l in enumerate(links) if not back or k >= len(back.links) or back.links[k] != l), 0)
        raise RuntimeError('o pedal não aceitaria a ligação de %s: %s' % (
            CT.key_name(links[bad].key), CT.link_text(links[bad], [])))
    return t


MOD_LIMITS = dict(sync=(0, 11), steps=(1, WP.MOD_STEPS), phase=(0, 95), glide=(0, 100), sens=(-20, 40),
                  attack=(1, 2000), release=(10, 5000))


def mods_from(mods):
    """the page's modulators, clamped to what the pedal's screens allow (docs/modulators.md)"""
    out = []
    for m in list(mods or [])[:WP.MOD_COUNT]:
        t = m.get('type', 'off')
        x = WP.mod_default(t if t in WP.MOD_TYPES else 'off')
        if x['type'] != 'off':
            x['shape'] = m.get('shape') if m.get('shape') in WP.MOD_SHAPES else 'sine'
            for k, (lo, hi) in MOD_LIMITS.items():
                x[k] = max(lo, min(hi, int(m.get(k, x[k]))))
            x['rate'] = max(0.05, min(20.0, float(m.get('rate', 1.0))))
            x['step'] = [max(0, min(100, int(v))) for v in (list(m.get('step') or []) + [0] * WP.MOD_STEPS)
                         [:WP.MOD_STEPS]]
        out.append(x)
    while len(out) < WP.MOD_COUNT:
        out.append(WP.mod_default('off'))
    return out


DUMPS = {'all': (0, FM.FLASH, 'flash_full.bin'), 'boot': (0, FM.BOOTLOADER, 'bootloader.bin')}


def flash_view(data):
    """an 8 MB dump as the pedal screen draws it: the areas (flashmap.AREAS) with what they use"""
    more = FM.details(data)
    return [dict(name=name, start=lo, end=hi, used=n, last=last, note=FM.NOTES.get(name, ''), detail=more.get(name, ''))
            for name, lo, hi, n, last in FM.summary(data)]


def file_name(text):
    """a name Windows takes for a file"""
    return re.sub(r'[\\/:*?"<>|]+', ' ', text).strip() or 'preset'


class Api:
    """What the page may call. Public methods are exposed to JavaScript; they return plain data or raise with a
    message for the user."""

    def __init__(self, emu=None, port=None):
        self._emu, self._port = emu, port
        self._lock = threading.Lock()               # one call to the pedal at a time
        self._pedal = None
        self._info = None
        self._window = None                         # set by main(): the file dialogs belong to it
        self._progress = None
        self._dirs = {}                             # the folder of the last file of each kind
        self._lib_cache = None                      # ((total, crc), wvlib.Library): the library last read
        self._updating = False                      # a firmware update has the port: the watcher keeps off
        self._sticky = ''                           # why the pedal is away (lost, rebooting), until it is back
        self._cancel_update = False
        self._cancel_task = False                   # cancel_task(): the flash dump stops at its next request
        self._wake = threading.Event()             # retry(): look again now
        self._closing = False
        self._attempts = 0
        self._connections = 0                       # bumped on every connection: the page reads everything again
        self._link = None
        self._booted = threading.Event()            # the emulator's first (and only) attempt is over
        self._set('starting', 'Iniciando o pedal emulado (leva alguns segundos)…' if emu else 'Procurando o pedal…')
        threading.Thread(target=self._watch, daemon=True).start()

    # ------------------------------------------------------------------ the connection
    # The page asks status() (never blocked by a call to the pedal) and shows the phase:
    #   starting    the first look (or the emulator booting)
    #   searching   no MIDI port of the pedal: switched off, not plugged in
    #   busy        its port is there but another program has it open (the original editor, the classic app)
    #   silent      its port opened but nothing answered: another firmware, or still booting
    #   old         waveod with a protocol older than this interface needs
    #   connected   ready; `connections` tells the page when it is a new connection
    #   failed      the emulator did not start (no retry)
    # A connected pedal's port is checked every WATCH_S; when it is gone (unplugged, switched off) the search
    # starts again by itself.

    def _set(self, phase, message='', **kw):
        self._link = dict(phase=phase, message=message, transport='emulador' if self._emu else 'usb',
                          attempts=self._attempts, connections=self._connections, name='', version='', protocol=0)
        self._link.update(kw)

    def _watch(self):
        if self._emu:
            try:
                from waveod_studio.emulated import EmuTransport
                pedal = P.Pedal(transport=EmuTransport(self._emu), timeout=20)
                self._attach(pedal, pedal.info())
            except Exception as e:                  # noqa: BLE001 - shown on the page
                self._set('failed', 'O emulador não iniciou: %s' % (str(e) or e.__class__.__name__))
            self._booted.set()
            return
        while not self._closing:
            if self._updating:
                wait = WATCH_S
            elif self._pedal is None:
                self._try()
                wait = SEARCH_S if self._link['phase'] in ('searching', 'starting') else RETRY_S
            else:
                if not self._present():
                    self._lost(LOST)
                    continue
                wait = WATCH_S
            self._wake.wait(wait)
            self._wake.clear()

    def _matches(self, name):
        n = name.lower()
        return str(self._port).lower() in n if self._port else any(h in n for h in htmidi.PORT_HINTS)

    def _present(self):
        try:
            return self._pedal is not None and self._pedal.name in htmidi.list_ports()[1]
        except Exception:                           # noqa: BLE001 - winmm itself failing: keep what we have
            return True

    def _try(self):
        self._attempts += 1
        try:
            outs = [n for n in htmidi.list_ports()[1] if self._matches(n)]
        except Exception as e:                      # noqa: BLE001
            self._set('searching', 'O Windows não listou as portas MIDI: %s' % e)
            return
        if not outs:                                # still away: why, until it comes back
            self._set('searching', self._sticky or 'Nenhum pedal conectado.')
            return
        if len(outs) > 1:
            self._set('searching', 'Mais de uma porta parece ser do pedal: %s.' % ', '.join(outs))
            return
        try:
            pedal = P.Pedal(self._port).__enter__()
        except RuntimeError as e:
            if 'Open failed' in str(e):
                self._set('busy', 'A porta MIDI do pedal está aberta em outro programa.', name=outs[0])
            else:
                self._set('searching', str(e))
            return
        try:
            info = pedal.info()
        except Exception:                           # noqa: BLE001 - no answer, or not waveod's
            pedal.close()
            self._set('silent', 'O pedal está conectado mas não respondeu.', name=outs[0])
            return
        if info['protocol'] < NEEDS:
            pedal.close()
            self._set('old', 'O waveod do pedal (protocolo %d) é anterior a esta interface (protocolo %d).' % (
                info['protocol'], NEEDS), name=outs[0], version=info['version'], protocol=info['protocol'])
            return
        self._attach(pedal, info)

    def _attach(self, pedal, info):
        self._sticky = ''
        with self._lock:
            self._pedal, self._info = pedal, info
            self._connections += 1
        self._set('connected', '', name=pedal.name, version=info['version'], protocol=info['protocol'])

    def _lost(self, why):
        """the pedal went away: drop it (after the call under way, if any) and look for it again"""
        with self._lock:
            pedal, self._pedal = self._pedal, None
        if pedal is not None:
            try:
                pedal.close()
            except Exception:                       # noqa: BLE001 - its handles may be gone with the device
                pass
        self._sticky = why
        self._set('searching', why)
        self._wake.set()

    def _call(self, fn):
        if self._emu:
            self._booted.wait()                     # scripts call right away; the page waits for status()
        if self._pedal is None:
            raise RuntimeError('pedal desconectado')
        with self._lock:
            pedal = self._pedal
            if pedal is None:
                raise RuntimeError('pedal desconectado')
            try:
                return fn(pedal)
            except (TimeoutError, P.NotWaveod, OSError, RuntimeError) as e:
                # no answer (NotWaveod is what a timeout becomes) or winmm failing: the pedal may be gone
                silent = isinstance(e, (TimeoutError, P.NotWaveod, OSError)) or (
                    not isinstance(e, P.PedalError) and 'failed' in str(e))
                if not silent:
                    raise
                gone = not self._emu and not self._present()
                err = RuntimeError('o pedal foi desconectado' if gone else 'o pedal não respondeu')
        if gone:
            self._lost(LOST)
        raise err

    # ------------------------------------------------------------------ the page's calls
    def status(self):
        """where the connection stands (see above); free of the pedal's lock"""
        return dict(self._link)

    def retry(self):
        """look for the pedal now instead of at the next try"""
        self._wake.set()

    def hello(self):
        if self._emu:
            self._booted.wait()
        s = self._link
        return dict(connected=s['phase'] == 'connected', transport=s['transport'], name=s['name'],
                    version=s['version'], protocol=s['protocol'], error=s['message'] or None)

    def rig(self):
        return self._call(lambda p: p.rig())

    def block(self, slot):
        return self._call(lambda p: p.block(int(slot)))

    def catalog(self, menu):
        return self._call(lambda p: p.catalog(int(menu)))

    def edit(self, op, a=0, b=0, c=0, value=0.0, text=None):
        return self._call(lambda p: p.edit(int(op), int(a), int(b), int(c), float(value), text))

    def presets(self):
        return self._call(lambda p: p.presets())

    def load(self, index):
        self._call(lambda p: p.load(int(index)))

    # ------------------------------------------------------------------ the presets screen
    def preset(self, index):
        """a stored preset as preset_view() shows it"""
        i = int(index)
        origin, data = self._call(lambda p: p.get(i))
        return preset_view(i, origin, data)

    def rename(self, index, name):
        """a stored preset under a new name (up to 17 characters); the pedal reloads it when it is the active one"""
        i = int(index)

        def go(p):
            pr = WP.Preset.decode(p.get(i)[1])
            pr.name = str(name).strip()[:WP.NAME_LEN].rstrip()
            p.put(i, pr.encode())
            return pr.name
        return self._call(go)

    def move(self, src, dst):
        """the preset taken out of src and put at dst; the ones between slide one place (the pedal's MOVE)"""
        self._call(lambda p: p.move(int(src), int(dst)))

    def copy(self, src, dst):
        self._call(lambda p: p.copy(int(src), int(dst)))

    def progress(self):
        """the long task under way (backup, restore): dict(label, done, total) or None; free of the pedal's lock"""
        return self._progress

    def export_preset(self, index):
        """a stored preset into a .wvp the user names; the file's name, or None when cancelled"""
        i = int(index)
        origin, data = self._call(lambda p: p.get(i))
        name = WP.Preset.decode(data).name.strip()
        path = self._ask_path(True, PRESET_FILES, 'presets', '%s %s.wvp' % (WP.preset_label(i), file_name(name)))
        if not path:
            return None
        with open(path, 'wb') as f:
            f.write(data)
        return os.path.basename(path)

    def pick_preset(self):
        """a .wvp the user picks, checked: dict(path, file, name, blocks), or None when cancelled"""
        path = self._ask_path(False, PRESET_FILES, 'presets')
        if not path:
            return None
        with open(path, 'rb') as f:
            data = f.read()
        try:
            pr = WP.Preset.decode(data)
        except WP.FormatError as e:
            raise RuntimeError('%s: %s' % (os.path.basename(path), e)) from None
        return dict(path=path, file=os.path.basename(path), name=pr.name, blocks=len(pr.chain()))

    def import_preset(self, index, path):
        """a .wvp (from pick_preset) stored at index"""
        with open(path, 'rb') as f:
            data = f.read()
        WP.Preset.decode(data)
        self._call(lambda p: p.put(int(index), data))

    def backup(self, path=None):
        """every preset (and the global controls) into a .wvbak; dict(file, presets, global) or None"""
        path = path or self._ask_path(True, BACKUP_FILES, 'backup', 'waveod presets.wvbak')
        if not path:
            return None
        m = self._long('Backup', lambda p, prog: B.save(p, path, prog))
        return dict(file=os.path.basename(path), presets=len(m['presets']), **{'global': bool(m.get('global_controls'))})

    def pick_backup(self):
        """a .wvbak the user picks, read: dict(path, file, presets, created, firmware, global), or None"""
        path = self._ask_path(False, BACKUP_FILES, 'backup')
        if not path:
            return None
        try:
            manifest, data = B.read(path)
        except (OSError, ValueError, KeyError) as e:
            raise RuntimeError('%s: %s' % (os.path.basename(path), e)) from None
        return dict(path=path, file=os.path.basename(path), presets=len(data), created=manifest.get('created', ''),
                    firmware=manifest.get('firmware', ''), **{'global': bool(manifest.get('global_controls'))})

    def restore(self, path):
        """a backup (from pick_backup) written over the same places; how many presets"""
        return self._long('Restaurando', lambda p, prog: B.restore(p, path, prog))

    # ------------------------------------------------------------------ the drums screen (MEUS RITMOS, protocol 7)
    def drums(self):
        """the 20 places: dict(places, sounds, max_events, playing_place, playing, slots=[None or dict])"""
        return self._call(lambda p: p.drums())

    def drum_pattern(self, place):
        """what a stored place plays, for its roll: dict(name, once, bpm, events, hits, length)"""
        i = int(place)
        r = D.parse_record(self._call(lambda p: p.drum_get(i)))
        if r is None:
            raise RuntimeError('o lugar M%d não tem um ritmo legível' % (i + 1))
        out = D.roll(r['events'])
        out.update(name=r['name'], once=r['once'], bpm=r['bpm'], events=len(r['events']))
        return out

    def pick_midi(self):
        """a MIDI file the user picks, converted with the defaults: dict(path, file, ...summary) or None"""
        path = self._ask_path(False, MIDI_FILES, 'midi')
        if not path:
            return None
        return self.midi_preview(path)

    def midi_preview(self, path, kit='acustico', once=False):
        """the pattern a MIDI file gives with that kit and ending (drums.summary), nothing written"""
        try:
            with open(path, 'rb') as f:
                data = f.read()
            pat = D.convert(data, kit=kit, once=bool(once), name=D.name_from_path(path))
        except (OSError, D.MidiError) as e:
            raise RuntimeError('%s não serve: %s' % (os.path.basename(path), e)) from None
        return dict(path=path, file=os.path.basename(path), **D.summary(pat))

    def drum_import(self, place, path, name, kit='acustico', once=False):
        """a MIDI file converted and stored in place; the places afterwards"""
        i = int(place)
        with open(path, 'rb') as f:
            data = f.read()
        try:
            pat = D.convert(data, kit=kit, once=bool(once), name=name)
        except D.MidiError as e:
            raise RuntimeError(str(e)) from None
        rec = D.record(pat, name)

        def go(p):
            p.drum_put(i, rec)
            return p.drums()
        return self._call(go)

    def drum_rename(self, place, name):
        i = int(place)

        def go(p):
            p.drum_put(i, D.renamed(p.drum_get(i), name))
            return p.drums()
        return self._call(go)

    def drum_empty(self, place):
        i = int(place)

        def go(p):
            p.drum_put(i, b'')
            return p.drums()
        return self._call(go)

    def drum_play(self, place, on=True):
        """place (None: the bank's pattern) chosen, the drum machine started or stopped; the places afterwards"""
        i = None if place is None else int(place)

        def go(p):
            p.drum_play(i, bool(on))
            return p.drums()
        return self._call(go)

    # ------------------------------------------------------------------ the effects screen (library, protocol 5)
    def library(self):
        """the effect modules on the pedal: dict(capacity, used (bytes of the packed library), modules=[...])"""
        self._lib_cache = None                      # the screen asks for it: read it from the pedal
        return self._long('Lendo os efeitos', lambda p, prog: self._library(p, prog), unit='KB')

    def pick_modules(self):
        """module files (.elf) the user picks, read: [dict(path, file, ...module, error)] or None when cancelled"""
        paths = self._ask_path(False, MODULE_FILES, 'modules', many=True)
        if not paths:
            return None
        out = []
        for path in paths:
            try:
                with open(path, 'rb') as f:
                    m = WL.describe(f.read())
                out.append(dict(module_view(m), path=path, file=os.path.basename(path), error=''))
            except (OSError, WL.LibraryError) as e:
                out.append(dict(path=path, file=os.path.basename(path), error=str(e)))
        return out

    def install_modules(self, paths):
        """module files added to the pedal's library (one with the same id is replaced); the library afterwards"""
        elfs = []
        for path in paths:
            with open(path, 'rb') as f:
                elfs.append(f.read())

        def go(p, prog):
            lib = self._lib_now(p, prog)
            for elf in elfs:
                try:
                    lib.add(elf)
                except WL.LibraryError as e:
                    raise RuntimeError(str(e)) from None
            p.write_library(lib.pack(), lambda d, t: prog(d // 1024, t // 1024))
            return self._written(p, lib)
        return self._long('Gravando os efeitos', go, unit='KB')

    def remove_modules(self, ids):
        """modules taken out of the pedal's library; the library afterwards"""
        ids = [int(i) for i in ids]

        def go(p, prog):
            lib = self._lib_now(p, prog)
            for i in ids:
                lib.remove(i)
            p.write_library(lib.pack(), lambda d, t: prog(d // 1024, t // 1024))
            return self._written(p, lib)
        return self._long('Gravando os efeitos', go, unit='KB')

    def _lib_now(self, p, prog=None):
        """the pedal's library: the one last read while the pedal still has it (same size and crc), else read"""
        info = p.library_info()
        c = self._lib_cache
        if c and c[0] == (info['total'], info['crc']):
            return c[1]
        lib = p.library((lambda d, t: prog(d // 1024, t // 1024)) if prog else None)
        self._lib_cache = ((info['total'], info['crc']), lib)
        return lib

    def _written(self, p, lib):
        # what was just written is the library now: no need to read it back
        info = p.library_info()
        self._lib_cache = ((info['total'], info['crc']), lib)
        return self._library(p)

    def _library(self, p, prog=None):
        lib = self._lib_now(p, prog)
        info = p.library_info()
        return dict(capacity=info['capacity'], used=len(lib.pack()) if len(lib) else 0,
                    modules=[module_view(m) for m in lib])

    # ------------------------------------------------------------------ the control tables (protocols 2 and 3)
    def controls(self, index):
        """what the controls editor needs of preset `index`: its table as the pedal plays it (converted from the
        original's fields when it has none), the blocks and parameters a link can point at, the knobs' stored
        values (a footswitch link starts from them), the scenes' names, the modulators and the global table"""
        i = int(index)

        def go(p):
            table, blocks = p.describe(i)
            origin, data = p.get(i)
            glob = p.global_controls() if (self._info or {}).get('protocol', 0) >= 3 else None
            return table, blocks, origin, data, glob
        table, blocks, origin, data, glob = self._call(go)
        pr = WP.Preset.decode(data)
        return dict(index=i, name=pr.name, own=pr.chunk(WP.CONTROLS) is not None, table=table_view(table),
                    blocks=[dict(slot=b.slot, menu=b.menu, name=b.name,
                                 params=[dict(index=q.index, knob=q.knob, name=q.name, min=q.min, max=q.max,
                                              step=q.step) for q in b.params]) for b in blocks],
                    knobs={str(s.slot): s.knobs for s in pr.slots()},
                    scenes=[s['name'] for s in pr.scenes()['scenes']], mods=pr.mods(), bpm=pr.bpm,
                    **{'global': table_view(glob) if glob is not None else None})

    def write_controls(self, index, table, mods=None):
        """the edited table (and modulators) into stored preset `index`; the pedal reloads it when it is the
        active one. The number of links written."""
        i = int(index)
        t = table_from(table)
        ms = mods_from(mods) if mods is not None else None

        def go(p):
            pr = WP.Preset.decode(p.get(i)[1])
            pr.set_controls(t)
            if ms is not None:
                pr.set_mods(ms)
            p.put(i, pr.encode())
        self._call(go)
        return len(t.links)

    def global_controls(self):
        """the pedal's global table (the links every preset plays where it has none of its own)"""
        return table_view(self._call(lambda p: p.global_controls()))

    def write_global_controls(self, table):
        t = table_from(table, is_global=True)
        self._call(lambda p: p.set_global_controls(t))
        return len(t.links)

    # ------------------------------------------------------------------ the NAM screen (Clone slots, protocol 6)
    def clones(self):
        """the 10 Clone slots: [dict(slot, kind, kind_text, named, name, sample_rate?, loudness?, crc?)]"""
        return [dict(s, kind_text=P.CLONE_KINDS.get(s['kind'], '?')) for s in self._call(lambda p: p.clones())]

    def pick_nam(self):
        """a .nam the user picks, checked: what nam_preview gives, or None when cancelled"""
        path = self._ask_path(False, NAM_FILES, 'nam')
        return self.nam_preview(path) if path else None

    def nam_preview(self, path):
        """what a .nam brings (nothing written): dict(path, file, name, arch (the network, in words), sample_rate,
        loudness, lite: it holds the A2-Lite network the NAM engine runs (then weights, bytes, rate48: trained at
        48 kHz, so it may run resampled or at the pedal's 44.1 kHz), convert: it can be converted for the original's
        Clone (the converter is built), di: the reference that conversion would use ('editor' or 'builtin'))"""
        try:
            m = NAM.load(path)
        except json.JSONDecodeError:
            raise RuntimeError('%s não é uma captura .nam (o arquivo não é o JSON de um modelo)'
                               % os.path.basename(path)) from None
        except (OSError, ValueError) as e:
            raise RuntimeError('%s não serve: %s' % (os.path.basename(path), e)) from None
        if not isinstance(m, dict) or 'architecture' not in m:
            raise RuntimeError('%s não é uma captura .nam (falta a arquitetura)' % os.path.basename(path))
        base = dict(path=path, file=os.path.basename(path), name=NAM.default_name(path), arch=nam_arch(m),
                    convert=CF.exe() is not None, di='editor' if CF.editor_di() else 'builtin')
        meta = m.get('metadata') or {}
        try:
            _, info = NAM.capture(path, m=m)
        except ValueError:
            fs = float(m.get('sample_rate') or 48000)
            return dict(base, lite=False, sample_rate=fs, loudness=meta.get('loudness'), weights=0, bytes=0,
                        rate48=False)
        return dict(base, lite=True, **info, rate48=47000 < info['sample_rate'] < 49000)

    def write_clone_fit(self, slot, path, name):
        """a .nam converted for the original's Clone (clonefit.py: the model renders the reference DI, the Clone
        model is fitted to it; progress() counts the steps) and stored in Clone slot `slot` under `name`, replacing
        what it held. The slots afterwards."""
        i = int(slot)
        nm = str(name).strip()[:NAM.NAME_MAX] or NAM.default_name(path)
        label = 'Convertendo %s para o Clone' % os.path.basename(path)

        def prog(done, total):
            self._progress = dict(label=label, done=done, total=total, unit='etapas')
        prog(0, CF.STEPS)
        try:
            data = CF.convert(path, prog)                  # minutes at most; the pedal is free meanwhile
        finally:
            self._progress = None

        def go(p):
            p.write_clone(i, nm, data[:CF.STORED])
            return p.clones()
        return [dict(s, kind_text=P.CLONE_KINDS.get(s['kind'], '?')) for s in self._call(go)]

    def write_nam(self, slot, path, name, native=False):
        """a .nam's A2-Lite network stored in Clone slot `slot` under `name` (native: a 48 kHz capture runs at
        44.1 kHz); it replaces what the slot held. The slots afterwards."""
        i = int(slot)
        data, _ = NAM.capture(path, native_rate=bool(native))
        nm = str(name).strip()[:NAM.NAME_MAX] or NAM.default_name(path)

        def go(p):
            p.write_clone(i, nm, data)
            return p.clones()
        return [dict(s, kind_text=P.CLONE_KINDS.get(s['kind'], '?')) for s in self._call(go)]

    # ------------------------------------------------------------------ the firmware (usb/qmeupdate.py)
    def pick_firmware(self):
        """a firmware .bin the user picks, checked as the original editor checks it; None when cancelled"""
        path = self._ask_path(False, FIRMWARE_FILES, 'firmware')
        return self.firmware_info(path) if path else None

    def firmware_info(self, path):
        try:
            img = U.Image.load(path)
        except (OSError, U.FirmwareError) as e:
            raise RuntimeError('%s não serve: %s' % (os.path.basename(path), e)) from None
        return dict(path=path, file=os.path.basename(path), version=img.version, size=len(img.raw),
                    sections=[dict(name=U.LOC_NAMES[loc], size=len(img.sections[loc])) for loc in U.ORDER])

    def update_firmware(self, path):
        """write a firmware .bin into the pedal like the original editor: the port taken from the connection, the
        pedal rebooted into its bootloader when it runs an app, the four sections sent (progress() counts the
        packets). Afterwards the connection looks for the pedal again; it reboots into the new firmware. A log of
        every message goes to %APPDATA%\\waveodStudio\\update-*.log. dict(seconds, version, log)"""
        if self._emu:
            raise RuntimeError('No emulador não há firmware para gravar: use o pedal na USB.')
        img = U.Image.load(path)
        self._updating, self._cancel_update = True, False
        with self._lock:                            # after the call under way
            pedal, self._pedal = self._pedal, None
        if pedal is not None:
            try:
                pedal.close()
            except Exception:                       # noqa: BLE001
                pass
        self._set('updating', 'Gravando o firmware %s.' % img.version)
        os.makedirs(LOG_DIR, exist_ok=True)
        log_path = os.path.join(LOG_DIR, 'update-%s.log' % time.strftime('%Y%m%d-%H%M%S'))
        trace = open(log_path, 'w', encoding='utf-8')
        t0 = time.time()

        def log(text):
            trace.write('%9.3f %s\n' % (time.time() - t0, text))

        def step(label, done=0, total=0):
            self._progress = dict(label=label, done=done, total=total, unit='pacotes', cancel=True)

        port, ok = None, False
        try:
            log('firmware %s (%s)' % (path, img.version))
            step('Procurando o pedal')
            port = htmidi.Port(self._port)
            if not U.probe(port, log=log):           # the app: reboot it into the bootloader
                step('Reiniciando o pedal no modo de atualização')
                log('pedido de reinício no modo de atualização (0x0500000E)')
                U.request_jump(port)
                port.close()
                port = U.wait_bootloader(self._port, log=log, cancel=lambda: self._cancel_update)
                if port is None:
                    raise U.UpdateError('cancelado')
            log('pedal em modo de atualização: %s' % port.name)
            step('Consultando o pedal')
            up = None

            def progress(done, total):
                step('Gravando: %s' % U.LOC_NAMES.get(up.loc, '') if done < total else 'Finalizando', done, total)
            up = U.Updater(img, port.send, progress, log)
            U.run(port, up, cancel=lambda: self._cancel_update)
            log('concluído')
            ok = True
            return dict(seconds=time.time() - t0, version=img.version, log=log_path)
        except Exception as e:                      # noqa: BLE001 - shown on the page
            log('erro: %s' % e)
            raise RuntimeError(str(e) or e.__class__.__name__) from None
        finally:
            if port is not None:
                try:
                    port.close()
                except Exception:                   # noqa: BLE001
                    pass
            trace.close()
            self._progress = None
            self._updating = False
            self._sticky = ('O pedal está reiniciando com o firmware novo.' if ok else
                            'A gravação não terminou: o pedal deve estar no modo de atualização.')
            self._set('searching', self._sticky)
            self._wake.set()

    def cancel_update(self):
        """stop a firmware update at the next packet (the pedal stays in its bootloader, ready to try again)"""
        self._cancel_update = True

    # ------------------------------------------------------------------ user IRs (the original's group 4)
    def irs(self):
        """the 20 user IR slots: dict(slot, name, used, samples (1024 floats, or None when empty)); the data comes
        from the pedal's flash"""
        def go(p):
            return p.ir_names(), p.user_irs()
        names, slots = self._call(go)
        return [dict(slot=i, name=(slots[i] or {}).get('name') or names[i], used=slots[i] is not None,
                     samples=[round(v, 6) for v in IRS.unpack(slots[i]['data'])] if slots[i] else None)
                for i in range(IRS.SLOTS)]

    def pick_ir(self):
        """a .wav the user picks, converted as the slot would hold it (ir_preview), or None when cancelled"""
        path = self._ask_path(False, IR_FILES, 'irs')
        return self.ir_preview(path, False) if path else None

    def ir_preview(self, path, normalize=False):
        """a .wav as a slot would hold it: dict(path, file, name, samples, and what was done: irs.convert)"""
        try:
            x, info = IRS.convert(path, bool(normalize))
        except (ValueError, OSError, struct.error) as e:
            raise RuntimeError('%s: %s' % (os.path.basename(path), e)) from None
        return dict(info, path=path, file=os.path.basename(path), name=IRS.default_name(path),
                    samples=[round(v, 6) for v in x])

    def write_ir(self, slot, path, name, normalize=False):
        """a .wav into user IR slot `slot` under `name` (the slot's IR is replaced); the slots afterwards"""
        x, _ = IRS.convert(path, bool(normalize))
        data = IRS.pack(x)
        n = str(name).strip()[:IRS.NAME_MAX] or IRS.default_name(path)
        self._call(lambda p: p.write_ir(int(slot), n, data))
        return self.irs()

    def rename_ir(self, slot, name):
        """a slot under a new name; the 20 names afterwards"""
        n = str(name).strip()[:IRS.NAME_MAX]
        if not n:
            raise RuntimeError('o nome não pode ficar vazio')

        def go(p):
            p.rename_ir(int(slot), n)
            time.sleep(0.15)                        # no answer: the names again once it is stored
            return p.ir_names()
        return self._call(go)

    # ------------------------------------------------------------------ the GLOBAL screen's settings (protocol 9)
    def _settings_ok(self):
        proto = (self._info or {}).get('protocol', 0)
        if proto < 9:
            raise RuntimeError('O waveod do pedal (protocolo %d) não passa os ajustes globais para o PC: grave um '
                               'waveod mais novo (protocolo 9) na aba Pedal.' % proto)

    def settings(self):
        """every GLOBAL setting with its range, and the global EQ (protocol.Pedal.settings)"""
        self._settings_ok()
        return self._call(lambda p: p.settings())

    def set_settings(self, changes):
        """settings changed as the GLOBAL screen changes them ({name: value}, 'eq': {parameter: value}); all of
        them afterwards. The pedal writes them to its flash after 2 s without changes."""
        self._settings_ok()
        ch = dict(changes or {})
        if 'eq' in ch:
            ch['eq'] = {int(k): float(v) for k, v in dict(ch['eq']).items()}
        return self._call(lambda p: p.set_settings(ch))

    # ------------------------------------------------------------------ flash dumps (read only)
    def dump_flash(self, kind):
        """the pedal's flash ('all': the 8 MB, 'boot': the bootloader) read into a file the user names, with the
        progress in KB (cancellable); dict(file, size, seconds, summary or None), or None when no file was named"""
        start, length, name = DUMPS[kind]
        path = self._ask_path(True, DUMP_FILES, 'dumps', name)
        if not path:
            return None
        self._cancel_task = False
        label = 'Copiando a flash inteira' if kind == 'all' else 'Copiando o bootloader'

        def prog(done, total):
            self._progress = dict(label=label, done=done // 1024, total=total // 1024, unit='KB', cancel=True,
                                  task='dump')
        prog(0, length)
        t0 = time.time()
        try:
            data = self._call(lambda p: p.dump(start, length, prog, cancel=lambda: self._cancel_task))
        except P.PedalError as e:
            if 'cancelado' in str(e):
                raise RuntimeError('Cópia cancelada: nenhum arquivo foi gravado.') from None
            raise
        finally:
            self._progress = None
        with open(path, 'wb') as f:
            f.write(data)
        return dict(file=os.path.basename(path), size=len(data), seconds=round(time.time() - t0, 1),
                    summary=flash_view(data) if len(data) == FM.FLASH else None)

    def pick_dump(self):
        """an 8 MB dump the user picks, summarised: dict(file, size, summary), or None when cancelled"""
        path = self._ask_path(False, DUMP_FILES, 'dumps')
        if not path:
            return None
        with open(path, 'rb') as f:
            data = f.read(FM.FLASH + 1)
        if len(data) != FM.FLASH:
            raise RuntimeError('%s tem %d KB: o resumo precisa de uma cópia da flash inteira (8 MB).' % (
                os.path.basename(path), len(data) // 1024))
        return dict(file=os.path.basename(path), size=len(data), seconds=0, summary=flash_view(data))

    def cancel_task(self):
        """stop the long task under way (a flash dump) at its next request"""
        self._cancel_task = True

    # ------------------------------------------------------------------ the pedal screen
    def pedal_info(self):
        """info() and what the places hold: own (waveod's), original (the original's), vacant"""
        def go(p):
            i, lst = p.info(), p.presets()
            own = sum(1 for e in lst if e['place'] == 0)
            orig = sum(1 for e in lst if e['place'] == 1 and e['origin'] != 2)
            return dict(i, name=p.name, own=own, original=orig, vacant=len(lst) - own - orig)
        return self._call(go)

    def latency(self):
        """the pedal's last latency test (protocol.Pedal.latency_result)"""
        return self._call(lambda p: p.latency_result())

    def measure_latency(self, route):
        """run the latency test on route (0 L, 1 R, 2 send, 3 the active preset) and wait for it (progress() shows
        the runs); a cable route plays a click on that output"""
        r = int(route)
        return self._long('Medindo a latência', lambda p, prog: p.measure_latency(r, prog), unit='medidas')

    # ------------------------------------------------------------------ the Desempenho screen (protocol 11)
    def perf_profile(self, reset=True, play=0.0):
        """the preset playing, slot by slot (bench.read_profile): the counts since the last reset; reset: counted
        afresh after this answer. play: seconds of audio the emulator runs first (its time passes only when asked;
        the pedal's own clock runs anyway)"""
        def go(p):
            wait = getattr(p.port, 'wait', None)
            if play and wait:
                wait(float(play))
            return BN.read_profile(p, bool(reset))
        return self._call(go)

    def pick_bench_module(self):
        """a module to measure, a .elf or a block's .c (built by the SDK, tools/wvfx.py), read again from the disk
        at every measurement: bench_module()'s answer, or None"""
        path = self._ask_path(False, BENCH_FILES, 'bench')
        return self.bench_module(path) if path else None

    def bench_module(self, path):
        """the module of a file as it is now (a .c built first): its description, the file's time (bench.stamp),
        and for a source the build's words; a source that does not build answers with error and log instead"""
        base = dict(path=path, file=os.path.basename(path), source=path.lower().endswith('.c'),
                    modified=BN.stamp(path), error='', log='', seconds=0.0)
        try:
            data, b = BN.module_source(path)
        except BN.BuildError as e:
            return dict(base, error=str(e), log=e.log)
        try:
            m = WL.describe(data)
        except WL.LibraryError as e:
            return dict(base, error='%s: %s' % (os.path.basename(path), e))
        return dict(base, **module_view(m), log=b['log'] if b else '', seconds=b['seconds'] if b else 0.0)

    def bench_stamp(self, path):
        """bench.stamp: the page watches a module's file with it (no pedal involved)"""
        try:
            return BN.stamp(path)
        except OSError:
            return 0.0

    def perf_bench(self, target, opts):
        """one effect on its own (bench.measure), the audio silent meanwhile: target {kind: 'catalog', menu,
        index} or {kind: 'file', path} (the ELF as it is on the disk now, into the pedal's RAM only); opts: blocks,
        skip, cold, bypass. The result has the file's state (modified) when it was one."""
        o = dict(opts or {})
        knobs, elf, extra = [BN.NAN] * BN.KNOBS, None, {}
        if target.get('kind') == 'file':
            path = target['path']
            modified = BN.stamp(path)
            try:
                elf, b = BN.module_source(path)                 # a .c is built again, a .elf read again
            except BN.BuildError as e:
                return dict(status=-100, status_text='não compilou', error=str(e), log=e.log, modified=modified)
            m = WL.describe(elf)
            for q in m.params:
                if q['knob'] < BN.KNOBS:
                    knobs[q['knob']] = q['default']
            menu, index, source = m.menu, 0, BN.SOURCE_ELF
            extra = dict(modified=modified, size=len(elf), name=m.name, log=b['log'] if b else '',
                         build_s=b['seconds'] if b else 0.0)
        else:
            menu, index, source = int(target['menu']), int(target['index']), BN.SOURCE_CATALOG
        blocks, skip = int(o.get('blocks', 1000)), int(o.get('skip', 50))

        def go(p):
            try:
                r = BN.measure(p, source, menu, index, elf, knobs, blocks, skip, bool(o.get('cold')),
                               bool(o.get('bypass')), timeout=600.0 if self._emu else None)
            except BN.BenchError as e:
                raise RuntimeError(str(e)) from None
            r.pop('knobs')
            return dict(r, menu=menu, emu=bool(self._emu), **extra)
        return self._long('Medindo o efeito', lambda p, prog: go(p), unit='')

    def _long(self, label, fn, unit='presets'):
        self._progress = dict(label=label, done=0, total=0, unit=unit)

        def prog(done, total):
            self._progress = dict(label=label, done=done, total=total, unit=unit)
        try:
            return self._call(lambda p: fn(p, prog))
        finally:
            self._progress = None

    def _ask_path(self, save, types, kind, name='', many=False):
        """a file dialog of the window (the folder of the last one of that kind); the path or None"""
        import webview
        if self._window is None:
            raise RuntimeError('sem janela para escolher o arquivo')
        r = self._window.create_file_dialog(webview.FileDialog.SAVE if save else webview.FileDialog.OPEN,
                                            directory=self._dirs.get(kind, ''), save_filename=name,
                                            file_types=types, allow_multiple=many)
        if many:
            r = list(r or [])
            if r:
                self._dirs[kind] = os.path.dirname(r[0])
            return r or None
        if isinstance(r, (list, tuple)):
            r = r[0] if r else None
        if r:
            self._dirs[kind] = os.path.dirname(r)
        return r or None

    def _close(self):
        self._closing = True
        self._wake.set()
        with self._lock:
            pedal, self._pedal = self._pedal, None
        if pedal is not None:
            try:
                pedal.close()
            except Exception:                       # noqa: BLE001 - closing anyway
                pass


def main(argv=None):
    ap = argparse.ArgumentParser(prog='waveod_studio --web')
    ap.add_argument('--emu', metavar='FW.bin', help='o firmware no emulador do waveod em vez do pedal')
    ap.add_argument('--port', help='a porta MIDI do pedal (padrão: a dele)')
    ap.add_argument('--dev', action='store_true', help='a página do servidor do Vite (npm run dev), com recarga')
    ap.add_argument('--debug', action='store_true', help='ferramentas de desenvolvedor na janela')
    a = ap.parse_args(argv)
    try:
        import webview
    except ImportError:
        print('A interface nova precisa do pywebview: pip install pywebview', file=sys.stderr)
        return 1
    index = os.path.join(WEB, 'index.html')
    if not a.dev and not os.path.exists(index):
        print('A página não foi gerada: cd studio/ui && npm install && npm run build', file=sys.stderr)
        return 1
    api = Api(a.emu, a.port)
    window = webview.create_window('waveod Studio', DEV_URL if a.dev else index, js_api=api, width=1440,
                                   height=900, min_size=(1100, 720), background_color='#0A0A09')
    api._window = window
    window.events.closed += api._close
    webview.start(http_server=not a.dev, debug=a.debug)
    return 0


if __name__ == '__main__':
    sys.exit(main())
