"""waveod Studio: the PC app for the waveod firmware.

Presets in waveod's own format (list, details, rename, move, copy, activate, export / import, backup /
restore), the effect modules installed (ui/effectsview.py), NAM captures in the Clone slots (ui/namview.py), the drum patterns imported from MIDI files (ui/drumsview.py), the pedal (firmware version, the preset store, firmware update) and the flash (dumps). Built on
Matribox Studio's visual kit (ui/kit.py) and USB tools (usb/); every pedal operation opens the MIDI port
for its duration on a worker thread, so the updater and the original editor can use the port in between.

    python -m waveod_studio
"""
import ctypes
import json
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, ttk

from waveod_studio.ui import kit as W
from waveod_studio.ui.controlsview import ControlsDialog
from waveod_studio.ui.drumsview import DrumsView
from waveod_studio.ui.effectsview import EffectsView
from waveod_studio.ui.namview import NamView
from waveod_studio import backup, controls, flashmap, wvpreset
from waveod_studio.protocol import FLASH_SIZE, LAT_ROUTE_NAMES, LAT_ROUTES, ORIGINS, PLACES, NotWaveod, Pedal
from waveod_studio.wvpreset import preset_label

T = W.T
APPNAME = 'waveod Studio'
SESSION = os.path.join(os.environ.get('APPDATA', os.path.expanduser('~')), 'waveodStudio', 'session.json')
MENU_TAGS = ('DYN', 'FX', 'WAH', 'DRV', 'AMP', 'CAB', 'IR', 'EQ', 'MOD', 'DLY', 'RVB', 'CLN', 'LOOP', 'SND', 'RTN',
             'VOL')                                 # waveod/src/fw/library.c
PLACE_COLORS = {0: T['accent'], 1: T['base_bar'], 2: T['dim']}


def parse_place(text):
    """'05B' / '5b' / '18' (1-based) -> index, or None"""
    t = text.strip().upper()
    if len(t) >= 2 and t[-1] in 'ABCD' and t[:-1].isdigit():
        i = (int(t[:-1]) - 1) * 4 + 'ABCD'.index(t[-1])
    elif t.isdigit():
        i = int(t) - 1
    else:
        return None
    return i if 0 <= i < 240 else None


# ------------------------------------------------------------------------------------------------ worker
class Worker:
    """one pedal operation at a time on a thread: fn(pedal, progress) -> result; done / fail run on the UI"""

    def __init__(self, app):
        self.app, self.q, self.busy = app, queue.Queue(), False
        app.root.after(50, self._pump)

    def run(self, label, fn, done=None, fail=None):
        if self.busy:
            self.app.status('Aguarde: uma operação ainda está em andamento')
            return
        self.busy = True
        self.app.progress(label, None)

        def work():
            try:
                if self.app.emu_fw and self.app.emu is None:      # the emulated pedal boots on first use
                    from waveod_studio.emulated import EmuTransport
                    self.app.emu = EmuTransport(self.app.emu_fw)
                with Pedal(self.app.session.get('port') or None, transport=self.app.emu,
                           timeout=60 if self.app.emu else 4.0) as p:
                    r = fn(p, lambda d, t: self.q.put(('progress', label, (d, t))))
                self.q.put(('done', done, r))
            except Exception as e:                  # noqa: BLE001 - every failure is shown to the user
                self.q.put(('fail', fail, e))
        threading.Thread(target=work, daemon=True).start()

    def _pump(self):
        try:
            while True:
                kind, fn, v = self.q.get_nowait()
                if kind == 'progress':
                    self.app.progress(fn, v)
                    continue
                self.busy = False
                self.app.progress(None, None)
                self.app.status('Pronto')               # the callback may say more
                if kind == 'done':
                    if fn:
                        fn(v)
                else:
                    (fn or self.app.failed)(v)
        except queue.Empty:
            pass
        self.app.root.after(50, self._pump)


# ------------------------------------------------------------------------------------------------ app
class App:
    def __init__(self, root, emu_fw=None):
        self.root = root
        self.emu_fw, self.emu = emu_fw, None             # --emu: the firmware in the emulator instead of USB
        self.S = max(1.0, root.winfo_fpixels('1i') / 96)
        self.F = W.Fonts(root, self.S)
        self.session = self._load_session()
        self.info, self.list, self.current, self.detail = None, [], None, None
        self.pending_select = None
        self.dialog_hook = None                         # --controls: called with the controls dialog once drawn
        self.glob = None                                # the pedal's global control table (protocol 3)
        root.title(APPNAME)
        root.configure(bg=T['bg'])
        w, h = round(1240 * self.S), round(800 * self.S)
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        w, h = min(w, sw - 40), min(h, sh - round(120 * self.S))
        root.geometry('%dx%d+%d+%d' % (w, h, (sw - w) // 2, max(0, (sh - h) // 2 - round(30 * self.S))))
        root.minsize(round(980 * self.S), round(620 * self.S))
        self._style()
        self._layout()
        self.worker = Worker(self)
        root.protocol('WM_DELETE_WINDOW', self.on_close)
        root.after(200, self.connect)

    def p(self, v):
        return round(v * self.S)

    # ------------------------------------------------------------------ session
    def _load_session(self):
        try:
            return json.load(open(SESSION, encoding='utf-8'))
        except (OSError, ValueError):
            return {}

    def _save_session(self):
        try:
            os.makedirs(os.path.dirname(SESSION), exist_ok=True)
            json.dump(self.session, open(SESSION, 'w', encoding='utf-8'), indent=1)
        except OSError:
            pass

    def on_close(self):
        self._save_session()
        self.root.destroy()

    # ------------------------------------------------------------------ setup
    def _style(self):
        s, S, F = ttk.Style(self.root), self.S, self.F
        s.theme_use('clam')
        s.configure('Wv.Treeview', background=T['card'], fieldbackground=T['card'], foreground=T['text'],
                    rowheight=round(34 * S), borderwidth=0, relief='flat', font=F.body)
        s.map('Wv.Treeview', background=[('selected', T['sel'])], foreground=[('selected', T['bright'])])
        s.layout('Wv.Treeview', [('Wv.Treeview.treearea', {'sticky': 'nswe'})])
        s.configure('Wv.Treeview.Heading', background=T['thead'], foreground=T['muted'], relief='flat', font=F.th,
                    borderwidth=0, padding=(round(10 * S), round(8 * S)))
        s.map('Wv.Treeview.Heading', background=[('active', T['hover'])])
        for name in ('Wv.Vertical.TScrollbar', 'Dark.Vertical.TScrollbar'):     # Dark: kit.ScrollFrame's
            s.configure(name, troughcolor=T['card'], background=T['seg_on'], bordercolor=T['card'],
                        lightcolor=T['seg_on'], darkcolor=T['seg_on'], arrowcolor=T['muted'], gripcount=0,
                        arrowsize=round(10 * S))
            s.layout(name, [('Vertical.Scrollbar.trough', {'sticky': 'ns', 'children': [
                ('Vertical.Scrollbar.thumb', {'expand': '1', 'sticky': 'nswe'})]})])

    def _layout(self):
        p, F, root = self.p, self.F, self.root
        hdr = tk.Frame(root, bg=T['panel'], height=p(60))
        hdr.pack(fill='x')
        hdr.pack_propagate(False)
        tk.Frame(root, bg=T['line'], height=1).pack(fill='x')
        hdr.columnconfigure(0, weight=1, uniform='side')
        hdr.columnconfigure(2, weight=1, uniform='side')
        hdr.rowconfigure(0, weight=1)
        brand = tk.Frame(hdr, bg=T['panel'])
        brand.grid(row=0, column=0, sticky='w', padx=(p(20), 0))
        logo = tk.Canvas(brand, width=p(30), height=p(30), bg=T['panel'], highlightthickness=0)
        logo.pack(side='left')
        W.rrect(logo, 0, 0, p(30), p(30), p(8), fill=T['accent'], bg=T['panel'])
        logo.create_text(p(15), p(15), text='W', font=F.mono_b, fill=T['accent_ink'])
        tk.Label(brand, text=APPNAME, font=F.h3, bg=T['panel'], fg=T['text']).pack(side='left', padx=(p(12), 0))
        self.nav = W.SegBar(hdr, self, [('presets', 'Presets'), ('effects', 'Efeitos'), ('nam', 'NAM'), ('drums', 'Bateria'),
                                        ('pedal', 'Pedal'), ('flash', 'Flash')], 'presets',
                            self.set_mode, height=38, radius=9, inner=6, pad=18)
        self.nav.grid(row=0, column=1)
        right = tk.Frame(hdr, bg=T['panel'])
        right.grid(row=0, column=2, sticky='e', padx=(0, p(20)))
        W.Btn(right, self, 'Conectar', None, self.connect, 'ghost', height=34,
              tip='Lê o pedal de novo pela USB (feche o editor original antes)').pack(side='right')
        self.conn = tk.Label(right, text='', font=F.small, bg=T['panel'], fg=T['muted'])
        self.conn.pack(side='right', padx=(0, p(12)))
        self.dot = tk.Canvas(right, width=p(10), height=p(10), bg=T['panel'], highlightthickness=0)
        self.dot.pack(side='right', padx=(0, p(8)))
        # footer: status and progress
        foot = tk.Frame(root, bg=T['foot'], height=p(30))
        foot.pack(side='bottom', fill='x')
        foot.pack_propagate(False)
        tk.Frame(root, bg=T['line'], height=1).pack(side='bottom', fill='x')
        self.f_status = tk.Label(foot, text='', font=F.xs, bg=T['foot'], fg=T['muted'])
        self.f_status.pack(side='left', padx=(p(20), 0))
        self.f_bar = tk.Canvas(foot, width=p(220), height=p(6), bg=T['foot'], highlightthickness=0)
        self.f_bar.pack(side='right', padx=(0, p(20)))
        # views
        self.content = tk.Frame(root, bg=T['bg'])
        self.content.pack(fill='both', expand=True)
        self.effects = EffectsView(self, self.content)
        self.nam = NamView(self, self.content)
        self.drums = DrumsView(self, self.content)
        self.views = {'presets': self._presets_view(), 'effects': self.effects.frame, 'nam': self.nam.frame,
                      'drums': self.drums.frame,
                      'pedal': self._pedal_view(), 'flash': self._flash_view()}
        self.mode = None
        self.set_mode('presets')
        self._connection(None)

    def set_mode(self, mode):
        if mode == self.mode:
            return
        for v in self.views.values():
            v.pack_forget()
        self.views[mode].pack(fill='both', expand=True)
        self.mode = mode
        self.nav.set(active=mode)
        if mode == 'effects':
            self.effects.shown()
        elif mode == 'nam':
            self.nam.shown()
        elif mode == 'drums':
            self.drums.shown()

    # ------------------------------------------------------------------ status
    def status(self, text, color=None):
        self.f_status.configure(text=text, fg=color or T['muted'])

    def progress(self, label, v):
        c, p = self.f_bar, self.p
        c.delete('all')
        if label is None:
            return
        self.status(label + '…')
        w, h = int(c.cget('width')), int(c.cget('height'))
        W.rrect(c, 0, 0, w, h, h / 2, fill=T['track'], bg=T['foot'])
        if v and v[1]:
            fw = max(h, w * v[0] / v[1])
            W.rrect(c, 0, 0, fw, h, h / 2, fill=T['accent'], bg=T['foot'])
            self.status('%s… %d / %d' % (label, v[0], v[1]))

    def toast_msg(self, text, kind='info'):
        """messages of Matribox Studio's dialogs (the firmware updater)"""
        self.status(text, {'error': T['red'], 'ok': T['green_soft']}.get(kind))

    def failed(self, e):
        text = str(e)
        if 'MIDI port not found' in text:
            text = 'Pedal não encontrado na USB: conecte o cabo, feche o editor original e clique em Conectar'
        elif 'is the PC editor still open' in text:
            text = 'A porta MIDI do pedal está em uso: feche o editor original ou o Matribox Studio'
        if isinstance(e, NotWaveod) or text != str(e):
            self._connection(None, text)
        self.status(text, T['red'])

    def _connection(self, info, why=None):
        p = self.p
        self.dot.delete('all')
        ok = info is not None
        W.aa.draw(self.dot, 0, 0, p(10), p(10), [('circle', (p(5), p(5), p(4)), T['green'] if ok else T['dim'])],
                  T['panel'])
        if ok:
            self.conn.configure(text='waveod %s · %s' % (info['version'], 'editado' if info['modified'] else 'conectado'),
                                fg=T['soft'])
        else:
            self.conn.configure(text='desconectado', fg=T['muted'])
            if why:
                self.status(why, T['red'])

    # ------------------------------------------------------------------ connecting
    def connect(self):
        self.worker.run('Lendo o pedal', lambda p, prog: (p.info(), p.presets()), self._got_pedal)

    def _got_pedal(self, r):
        self.info, self.list = r
        self._connection(self.info)
        self.status('%d presets lidos' % len(self.list))
        self._fill_list()
        self._fill_pedal()
        self.effects.lib = None                         # read again when the tab shows
        self.nam.slots = None
        self.drums.state = None
        if self.mode == 'effects':
            self.effects.shown()
        elif self.mode == 'nam':
            self.nam.shown()
        elif self.mode == 'drums':
            self.drums.shown()
        if self.pending_select is not None:
            i, self.pending_select = self.pending_select, None
            self.tree.selection_set(str(i))
            self.tree.see(str(i))

    # ================================================================== presets
    def _presets_view(self):
        p, F = self.p, self.F
        v = tk.Frame(self.content, bg=T['bg'])
        bar = tk.Frame(v, bg=T['bg'])
        bar.pack(fill='x', padx=p(20), pady=(p(16), p(10)))
        tk.Label(bar, text='Presets', font=F.h2, bg=T['bg'], fg=T['bright']).pack(side='left')
        W.Btn(bar, self, 'Restaurar backup…', None, self.restore, 'ghost', height=34,
              tip='Grava no pedal os presets de um arquivo .wvbak').pack(side='right')
        W.Btn(bar, self, 'Backup…', None, self.backup, 'primary', height=34,
              tip='Salva todos os presets do pedal num arquivo .wvbak').pack(side='right', padx=(0, p(8)))
        W.Btn(bar, self, 'Controles globais…', None, self.edit_global, 'ghost', height=34,
              tip='O que footswitches, pedais e MIDI fazem nos presets sem ligação própria na tecla').pack(
            side='right', padx=(0, p(8)))
        body = tk.Frame(v, bg=T['bg'])
        body.pack(fill='both', expand=True, padx=p(20), pady=(0, p(18)))
        # list
        card = W.Card(body, self, T['card'], T['cardline'], radius=10, fit='both')
        card.pack(side='left', fill='both', expand=True)
        tv = self.tree = ttk.Treeview(card.body, columns=('num', 'name', 'place'), show='headings',
                                      style='Wv.Treeview', selectmode='browse')
        for col, text, width, anchor in (('num', 'Nº', 70, 'w'), ('name', 'Nome', 300, 'w'),
                                         ('place', 'Onde', 150, 'w')):
            tv.heading(col, text=text, anchor=anchor)
            tv.column(col, width=p(width), anchor=anchor, stretch=col == 'name')
        sb = ttk.Scrollbar(card.body, orient='vertical', command=tv.yview, style='Wv.Vertical.TScrollbar')
        tv.configure(yscrollcommand=sb.set)
        sb.pack(side='right', fill='y')
        tv.pack(fill='both', expand=True)
        tv.tag_configure('active', foreground=T['accent'])
        tv.tag_configure('empty', foreground=T['dim'])
        tv.tag_configure('original', foreground=T['soft'])
        tv.bind('<<TreeviewSelect>>', lambda e: self._select())
        tv.bind('<Double-1>', lambda e: self.activate())
        # details
        side = tk.Frame(body, bg=T['bg'], width=p(380))
        side.pack(side='right', fill='y', padx=(p(16), 0))
        side.pack_propagate(False)
        self.det_sf = W.ScrollFrame(side, self, T['bg'])            # the details may be taller than the window
        self.det_sf.pack(fill='both', expand=True)
        self.det = W.Card(self.det_sf.inner, self, T['panel'], T['cardline'], radius=10, fit='x')
        self.det.pack(fill='x')
        self.root.bind_all('<MouseWheel>', self._wheel, add='+')
        self._details(None, None)
        return v

    def _wheel(self, e):
        """the wheel scrolls the details when the pointer is over them"""
        if str(e.widget).startswith(str(self.det_sf)):
            self.det_sf._wheel(e)

    def _fill_list(self):
        tv = self.tree
        keep = self.current
        tv.delete(*tv.get_children())
        for e in self.list:
            place = e['place']
            where = PLACES.get(place, '?')
            if place == 1:
                where = 'original · %s' % ORIGINS.get(e['origin'], '?')
            tags = ('active',) if e['active'] else ('empty',) if place == 2 or e['origin'] == 2 else \
                ('original',) if place == 1 else ()
            tv.insert('', 'end', iid=str(e['index']), tags=tags,
                      values=(preset_label(e['index']) + (' ●' if e['active'] else ''), e['name'] or '—', where))
        if keep is not None and tv.exists(str(keep)):
            tv.selection_set(str(keep))
            tv.see(str(keep))

    def _select(self):
        sel = self.tree.selection()
        if not sel:
            return
        self.current = int(sel[0])
        i = self.current
        proto = (self.info or {}).get('protocol', 1)

        def got(r):
            self.glob = r[2]
            self._details(i, r[0], r[1])
        self.worker.run('Lendo o preset %s' % preset_label(i),
                        lambda p, prog: (p.get(i), p.describe(i) if proto >= 2 else None,
                                         p.global_controls() if proto >= 3 else None), got)

    def _details(self, index, r, desc=None):
        p, F, b = self.p, self.F, self.det.body
        for w in b.winfo_children():
            w.destroy()
        pad = p(18)
        if index is None:
            tk.Label(b, text='Selecione um preset na lista.', font=F.body, bg=T['panel'], fg=T['muted']).pack(
                anchor='w', padx=pad, pady=pad)
            return
        origin, data = r
        e = self.list[index] if index < len(self.list) else {}
        try:
            pr = wvpreset.Preset.decode(data)
        except wvpreset.FormatError as ex:
            tk.Label(b, text=str(ex), font=F.body, bg=T['panel'], fg=T['red']).pack(anchor='w', padx=pad, pady=pad)
            return
        self.detail = (index, pr, desc)
        top = tk.Frame(b, bg=T['panel'])
        top.pack(fill='x', padx=pad, pady=(pad, p(4)))
        tk.Label(top, text=preset_label(index), font=F.mono_b, bg=T['panel'], fg=T['accent']).pack(side='left')
        W.Pill(top, self, PLACES.get(e.get('place'), '?'), PLACE_COLORS.get(e.get('place'), T['dim']),
               bg=T['panel']).pack(side='right')
        tk.Label(b, text=pr.name or '(sem nome)', font=F.h_name, bg=T['panel'], fg=T['bright'], anchor='w').pack(
            fill='x', padx=pad)
        m = pr.main() or {}
        facts = [('Estilo', pr.style or '—'), ('BPM', m.get('bpm', '—')), ('Volume', m.get('volume', '—')),
                 ('Roteamento', pr.route_mode()), ('Origem', ORIGINS.get(origin, '?'))]
        g = tk.Frame(b, bg=T['panel'])
        g.pack(fill='x', padx=pad, pady=(p(12), 0))
        for k, (name, value) in enumerate(facts):
            tk.Label(g, text=name.upper(), font=F.tiny, bg=T['panel'], fg=T['muted']).grid(row=k, column=0,
                                                                                         sticky='w', pady=p(2))
            tk.Label(g, text=str(value), font=F.body, bg=T['panel'], fg=T['text']).grid(row=k, column=1, sticky='w',
                                                                                      padx=(p(14), 0))
        tk.Label(b, text='CADEIA', font=F.tiny, bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=pad,
                                                                                   pady=(p(14), p(6)))
        chips = tk.Frame(b, bg=T['panel'])
        chips.pack(fill='x', padx=pad)
        chain = pr.chain()
        if not chain:
            tk.Label(chips, text='vazia', font=F.small, bg=T['panel'], fg=T['dim']).pack(anchor='w')
        row = None
        for k, s in enumerate(chain):
            if k % 6 == 0:
                row = tk.Frame(chips, bg=T['panel'])
                row.pack(anchor='w', pady=(0, p(6)))
            tag = MENU_TAGS[s.menu] if 0 <= s.menu < len(MENU_TAGS) else '?'
            W.Pill(row, self, tag, W.menu_color(s.menu) if s.on else T['dim'], bg=T['panel'],
                   height=24).pack(side='left', padx=(0, p(6)))
        self._scenes(b, pr, chain)
        self._controls(b, desc)
        acts = tk.Frame(b, bg=T['panel'])
        acts.pack(fill='x', padx=pad, pady=(p(16), pad))
        rows = (('Ativar no pedal', 'primary', self.activate), ('Controles…', 'ghost', self.edit_controls),
                ('Renomear…', 'ghost', self.rename),
                ('Mover para…', 'ghost', lambda: self.move_copy(True)),
                ('Copiar para…', 'ghost', lambda: self.move_copy(False)),
                ('Exportar .wvp…', 'ghost', self.export_one), ('Importar .wvp…', 'ghost', self.import_one))
        for k, (text, kind, cmd) in enumerate(rows):
            W.Btn(acts, self, text, None, cmd, kind, height=32, width=156).grid(
                row=k // 2, column=k % 2, sticky='w', padx=(0, p(8)), pady=(0, p(8)))

    def _scenes(self, b, pr, chain):
        """the scenes: a row each with its blocks (lit when on there), the active one marked"""
        p, F, pad = self.p, self.F, self.p(18)
        tk.Label(b, text='CENAS', font=F.tiny, bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=pad,
                                                                                  pady=(p(10), p(6)))
        if pr.chunk(wvpreset.SCENES) is None:
            tk.Label(b, text='sem cenas: as quatro iguais aos blocos', font=F.small, bg=T['panel'],
                     fg=T['dim']).pack(anchor='w', padx=pad)
            return
        sc = pr.scenes()
        box = tk.Frame(b, bg=T['panel'])
        box.pack(fill='x', padx=pad)
        for n, scene in enumerate(sc['scenes']):
            active = n == sc['active']
            tk.Label(box, text='ABCD'[n], font=F.mono_b, bg=T['panel'],
                     fg=T['accent'] if active else T['muted']).grid(row=n, column=0, sticky='w', pady=p(2))
            tk.Label(box, text=scene['name'] or 'CENA %d' % (n + 1), font=F.small, bg=T['panel'],
                     fg=T['bright'] if active else T['text'], width=12, anchor='w').grid(
                row=n, column=1, sticky='w', padx=(p(8), p(6)))
            marks = tk.Frame(box, bg=T['panel'])
            marks.grid(row=n, column=2, sticky='w')
            for s in chain:
                on = scene['on'].get(s.slot, False)
                tk.Frame(marks, bg=W.menu_color(s.menu) if on else T['cardline'], width=p(12),
                         height=p(10)).pack(side='left', padx=(0, p(3)))
        if sc['params']:
            k = len(sc['params'])
            tk.Label(b, text='%d parâmetro%s com um valor por cena' % (k, '' if k == 1 else 's'), font=F.small,
                     bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=pad, pady=(p(4), 0))

    def _controls(self, b, desc):
        """what the footswitches (stomp mode), the pedals and MIDI do: a line per key, its first link"""
        p, F, pad = self.p, self.F, self.p(18)
        tk.Label(b, text='CONTROLES', font=F.tiny, bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=pad,
                                                                                      pady=(p(12), p(6)))
        if desc is None:
            tk.Label(b, text='o firmware do pedal não descreve os controles (atualize o waveod)', font=F.small,
                     bg=T['panel'], fg=T['dim']).pack(anchor='w', padx=pad)
            return
        table, blocks = desc
        glob = self.glob
        keys = [controls.A, controls.B, controls.C, controls.D, controls.BANK_DOWN, controls.BANK_UP,
                controls.EXP1A, controls.EXP1B, controls.EXP2]
        midi = sorted(set(table.midi_keys()) | set(glob.midi_keys() if glob else ()))
        parts = []
        for key in keys + midi:
            links = controls.runs(table, glob, key, controls.STOMP)
            if links:
                first, g = links[0]
                parts.append('%s %s%s%s' % (controls.key_name(key).replace('MIDI ', ''), controls.brief(first, blocks),
                                            ' (G)' if g else '', ' +%d' % (len(links) - 1) if len(links) > 1 else ''))
        tk.Label(b, text='   '.join(parts) or 'nenhum', font=F.small, bg=T['panel'], fg=T['text'], justify='left',
                 wraplength=p(330), anchor='w').pack(fill='x', padx=pad)
        tk.Label(b, text='%d ligações na tabela · footswitches no modo stomp%s' % (
            len(table.links), ' · (G) = dos controles globais' if glob else ''), font=F.xs,
            bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=pad, pady=(p(3), 0))

    def edit_controls(self):
        if not self._need() or not self.detail or self.detail[0] != self.current:
            return
        i, pr, desc = self.detail
        if desc is None:
            self.status('O firmware do pedal não descreve os controles: atualize o waveod', T['red'])
            return
        table, blocks = desc
        knobs = {s.slot: s.knobs for s in pr.slots()}
        names = [s['name'] for s in pr.scenes()['scenes']]
        dlg = ControlsDialog(self, 'Controles de %s · %s' % (preset_label(i), pr.name or 'sem nome'), table, blocks,
                             knobs, names, glob=self.glob, mods=pr.mods())
        got = dlg.show()
        if got is None:
            return
        if self.info and self.info.get('active') == i and self.info.get('modified') and not W.ask(
                self, 'Gravar os controles', 'O preset %s está ativo e tem edições não salvas no pedal. Gravar os '
                'controles grava o preset guardado e o carrega de novo: as edições do pedal se perdem.'
                % preset_label(i), 'Gravar mesmo assim', 'danger'):
            return
        pr.set_controls(got)
        pr.set_mods(dlg.mods)
        data = pr.encode()
        self.worker.run('Gravando %s' % preset_label(i), lambda p, prog: p.put(i, data),
                        lambda r: self._reload('Controles de %s gravados' % preset_label(i)))

    def edit_global(self):
        """the global control table: what keys do in every preset that has no links of its own there"""
        if (self.info or {}).get('protocol', 1) < 3:
            self.status('O firmware do pedal não tem controles globais: atualize o waveod', T['red'])
            return

        def edit(glob):
            self.glob = glob
            names = [''] * controls.SCENE_COUNT
            got = ControlsDialog(self, 'Controles globais', glob, [], None, names, is_global=True).show()
            if got is None:
                return
            self.worker.run('Gravando os controles globais', lambda p, prog: p.set_global_controls(got),
                            lambda r: self._reload('Controles globais gravados'))
        self.worker.run('Lendo os controles globais', lambda p, prog: p.global_controls(), edit)

    # ------------------------------------------------------------------ preset actions
    def _need(self):
        if self.current is None:
            self.status('Selecione um preset primeiro')
            return False
        return True

    def _reload(self, msg):
        def done(r):
            self._got_pedal(r)
            self.status(msg, T['green_soft'])
            if self.current is not None:
                self._select()
        self.worker.run('Lendo o pedal', lambda p, prog: (p.info(), p.presets()), done)

    def activate(self):
        if self._need():
            i = self.current
            self.worker.run('Ativando %s' % preset_label(i), lambda p, prog: p.load(i),
                            lambda r: self._reload('%s ativado no pedal' % preset_label(i)))

    def _ask_text(self, title, label, value, ok):
        d = W.Dialog(self, title, 440)
        tk.Label(d.body, text=label, font=self.F.small, bg=T['panel'], fg=T['muted']).pack(anchor='w',
                                                                                           pady=(0, self.p(6)))
        var = tk.StringVar(value=value)
        f = W.Field(d.body, self, var, search=False)
        f.pack(fill='x')
        f.entry.focus_set()
        f.entry.select_range(0, 'end')
        f.entry.bind('<Return>', lambda e: d.close(var.get()))
        d.buttons([('Cancelar', 'ghost', d.close), (ok, 'primary', lambda: d.close(var.get()))])
        return d.show(wait=True)

    def rename(self):
        if not self._need() or not self.detail or self.detail[0] != self.current:
            return
        i, pr = self.detail[:2]
        name = self._ask_text('Renomear %s' % preset_label(i), 'Nome (até 17 caracteres)', pr.name, 'Renomear')
        if name is None:
            return
        pr.name = name[:wvpreset.NAME_LEN]
        data = pr.encode()
        self.worker.run('Gravando %s' % preset_label(i), lambda p, prog: p.put(i, data),
                        lambda r: self._reload('%s renomeado' % preset_label(i)))

    def move_copy(self, move):
        if not self._need():
            return
        i = self.current
        verb = 'Mover' if move else 'Copiar'
        text = self._ask_text('%s %s' % (verb, preset_label(i)), 'Destino (como 05B, ou o número de 1 a 240)', '',
                              verb)
        if text is None:
            return
        to = parse_place(text)
        if to is None:
            self.status('Destino inválido: %s' % text, T['red'])
            return
        if not move and self.list[to]['place'] != 2 and not W.ask(
                self, 'Copiar por cima?', '%s "%s" será substituído por uma cópia de %s.' % (
                    preset_label(to), self.list[to]['name'], preset_label(i)), 'Copiar', 'danger'):
            return
        fn = (lambda p, prog: p.move(i, to)) if move else (lambda p, prog: p.copy(i, to))
        self.current = to if move else self.current
        self.worker.run('%s %s → %s' % (verb, preset_label(i), preset_label(to)), fn,
                        lambda r: self._reload('%s → %s' % (preset_label(i), preset_label(to))))

    def export_one(self):
        if not self._need() or not self.detail or self.detail[0] != self.current:
            return
        i, pr = self.detail[:2]
        path = filedialog.asksaveasfilename(parent=self.root, defaultextension='.wvp',
                                            initialfile='%s %s.wvp' % (preset_label(i), pr.name.strip() or 'preset'),
                                            filetypes=[('Preset do waveod', '*.wvp')])
        if path:
            open(path, 'wb').write(pr.encode())
            self.status('%s salvo' % os.path.basename(path), T['green_soft'])

    def import_one(self):
        if not self._need():
            return
        i = self.current
        path = filedialog.askopenfilename(parent=self.root, filetypes=[('Preset do waveod', '*.wvp')])
        if not path:
            return
        data = open(path, 'rb').read()
        try:
            pr = wvpreset.Preset.decode(data)
        except wvpreset.FormatError as e:
            self.status('%s: %s' % (os.path.basename(path), e), T['red'])
            return
        if not W.ask(self, 'Importar por cima?', '"%s" vai substituir %s "%s".' % (
                pr.name, preset_label(i), self.list[i]['name']), 'Importar', 'danger'):
            return
        self.worker.run('Gravando %s' % preset_label(i), lambda p, prog: p.put(i, data),
                        lambda r: self._reload('"%s" importado em %s' % (pr.name, preset_label(i))))

    def backup(self):
        path = filedialog.asksaveasfilename(parent=self.root, defaultextension='.wvbak',
                                            initialdir=self.session.get('backup_dir'),
                                            initialfile='waveod presets.wvbak',
                                            filetypes=[('Backup do waveod', '*.wvbak')])
        if not path:
            return
        self.session['backup_dir'] = os.path.dirname(path)
        self.worker.run('Backup', lambda p, prog: backup.save(p, path, prog),
                        lambda m: self.status('Backup de %d presets%s em %s' % (
                            len(m['presets']), ' e dos controles globais' if m.get('global_controls') else '',
                            os.path.basename(path)), T['green_soft']))

    def restore(self):
        path = filedialog.askopenfilename(parent=self.root, initialdir=self.session.get('backup_dir'),
                                          filetypes=[('Backup do waveod', '*.wvbak')])
        if not path:
            return
        try:
            manifest, data = backup.read(path)
        except (OSError, ValueError, KeyError) as e:
            self.status('%s: %s' % (os.path.basename(path), e), T['red'])
            return
        glob = manifest.get('global_controls')
        if glob and (self.info or {}).get('protocol', 1) < 3:
            extra = ' O backup tem controles globais, mas o firmware do pedal não os aceita (atualize o waveod): ' \
                    'eles ficam de fora.'
        elif glob:
            extra = ' Os controles globais do pedal também são substituídos pelos do backup.'
        else:
            extra = ' O backup não tem controles globais: os do pedal ficam como estão.'
        if not W.ask(self, 'Restaurar o backup?', 'Grava %d presets de %s (feito em %s) por cima dos que estão nas '
                     'mesmas posições do pedal.%s' % (len(data), os.path.basename(path), manifest.get('created', '?'),
                                                      extra), 'Restaurar', 'danger'):
            return
        self.worker.run('Restaurando', lambda p, prog: backup.restore(p, path, prog),
                        lambda n: self._reload('%d presets restaurados%s' % (
                            n, ' e os controles globais' if glob and self.info.get('protocol', 1) >= 3 else '')))

    # ================================================================== pedal
    def _pedal_view(self):
        p, F = self.p, self.F
        v = tk.Frame(self.content, bg=T['bg'])
        tk.Label(v, text='Pedal', font=F.h2, bg=T['bg'], fg=T['bright']).pack(anchor='w', padx=p(20),
                                                                              pady=(p(16), p(10)))
        row = tk.Frame(v, bg=T['bg'])
        row.pack(fill='x', padx=p(20))
        self.ped_card = W.Card(row, self, T['panel'], T['cardline'], radius=10, fit='x')
        self.ped_card.pack(side='left', fill='x', expand=True, anchor='n')
        up = W.Card(row, self, T['panel'], T['cardline'], radius=10, fit='x')
        up.pack(side='left', fill='x', expand=True, anchor='n', padx=(p(16), 0))
        b = up.body
        tk.Label(b, text='Atualizar o firmware', font=F.h3, bg=T['panel'], fg=T['text']).pack(anchor='w', padx=p(18),
                                                                                             pady=(p(16), p(4)))
        tk.Label(b, text='Grava um .bin do waveod (ou o firmware original) pela USB. Os presets do waveod ficam numa '
                 'área que nenhuma atualização apaga; mesmo assim, faça um backup antes.', font=F.small,
                 bg=T['panel'], fg=T['muted'], wraplength=p(420), justify='left').pack(anchor='w', padx=p(18))
        W.Btn(b, self, 'Atualizar firmware…', None, self.update_fw, 'primary', height=34).pack(
            anchor='w', padx=p(18), pady=(p(14), p(18)))

        lat = W.Card(v, self, T['panel'], T['cardline'], radius=10, fit='x')
        lat.pack(fill='x', padx=p(20), pady=(p(16), 0))
        b = lat.body
        tk.Label(b, text='Latência', font=F.h3, bg=T['panel'], fg=T['text']).pack(anchor='w', padx=p(18),
                                                                                 pady=(p(16), p(4)))
        tk.Label(b, text='O pedal mede quanto o som leva da entrada à saída: ligue um cabo da saída escolhida na '
                 'entrada (o teste toca um clique nessa saída; desligue o amplificador e o fone) ou meça, sem cabo, '
                 'quanto o preset ativo acrescenta. O mesmo teste fica em GLOBAL › LATÊNCIA no pedal.',
                 font=F.small, bg=T['panel'], fg=T['muted'], wraplength=p(860), justify='left').pack(
            anchor='w', padx=p(18))
        row = tk.Frame(b, bg=T['panel'])
        row.pack(anchor='w', padx=p(18), pady=(p(12), 0))
        self.lat_route = 'L'
        W.SegBar(row, self, [('L', 'Saída L'), ('R', 'Saída R'), ('SEND', 'Send → Return'),
                             ('PRESET', 'Preset (sem cabo)')], 'L', self._lat_route, height=34).pack(side='left')
        W.Btn(row, self, 'Medir', None, self.measure_latency, 'primary', height=34).pack(side='left',
                                                                                     padx=(p(12), 0))
        self.lat_text = tk.Label(b, text='Sem medição. Só os buffers dão 128 amostras (2,90 ms) a 44,1 kHz.',
                                 font=F.body, bg=T['panel'], fg=T['muted'], justify='left')
        self.lat_text.pack(anchor='w', padx=p(18), pady=(p(12), p(18)))
        self._fill_pedal()
        return v

    def _lat_route(self, key):
        self.lat_route = key

    def measure_latency(self):
        if not self.info:
            self.status('Conecte o pedal primeiro')
            return
        if self.info.get('protocol', 1) < 4:
            self.status('O firmware do pedal não mede latência: atualize o waveod', T['red'])
            return
        route = LAT_ROUTES[self.lat_route]
        if self.lat_route != 'PRESET' and not W.ask(
                self, 'Medir a latência?', 'Ligue um cabo da %s. O teste toca um clique nessa saída: desligue o '
                'amplificador e o fone antes.' % ('saída L no INPUT', 'saída R no INPUT',
                                                 'FX SEND no FX RETURN')[route], 'Medir', 'primary'):
            return
        self.worker.run('Medindo a latência', lambda p, prog: p.measure_latency(route, prog), self._got_latency)

    def _got_latency(self, r):
        name = LAT_ROUTE_NAMES[r['route']] if r['route'] < len(LAT_ROUTE_NAMES) else '?'
        name = name[:1].upper() + name[1:]
        if r['status'] not in (2, 3):
            why = {4: 'nada voltou: confira o cabo e a rota' if r['route'] != 3 else
                   'o preset não deixou o sinal passar (gate? volume?)',
                   5: 'a entrada saturou já a -30 dBFS: use o tipo de entrada LINHA (GLOBAL)'}.get(r['status'], '')
            self.lat_text.configure(text='%s: %s%s' % (name, r['status_text'], ' · ' + why if why else ''),
                                    fg=T['red'])
            self.status('Latência: %s' % r['status_text'], T['red'])
            return
        fmt = lambda v, n=2: ('%.*f' % (n, v)).replace('.', ',')     # noqa: E731
        lines = ['%s: %s ms · %s amostras' % (name, fmt(r['ms'], 3), fmt(r['mean']))]
        if r['route'] != 3:
            rest = r['mean'] - r['buffers']
            lines.append('buffers %d amostras (%s ms) + conversores %s amostras (%s ms)' % (
                r['buffers'], fmt(r['buffers'] * 1000.0 / r['rate'], 3), fmt(rest), fmt(rest * 1000.0 / r['rate'], 3)))
        else:
            lines.append('o que o preset ativo soma à latência do pedal')
        lines.append('%d medidas de %s a %s%s · semelhança %d%%%s' % (
            r['runs'], fmt(r['min']), fmt(r['max']), ' · %d dBFS' % r['level_db'] if r['route'] != 3 else '',
            round(r['match'] * 100), ' · INSTÁVEL' if r['status'] == 3 else ''))
        self.lat_text.configure(text='\n'.join(lines), fg=T['text'] if r['status'] == 2 else T['red'])
        self.status('Latência: %s ms' % fmt(r['ms'], 3), T['green_soft'])

    def _fill_pedal(self):
        p, F = self.p, self.F
        b = self.ped_card.body
        for w in b.winfo_children():
            w.destroy()
        tk.Label(b, text='Firmware e presets', font=F.h3, bg=T['panel'], fg=T['text']).pack(anchor='w', padx=p(18),
                                                                                           pady=(p(16), p(8)))
        if not self.info:
            tk.Label(b, text='Conecte o pedal com o waveod pela USB e clique em Conectar.', font=F.small,
                     bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=p(18), pady=(0, p(18)))
            return
        i, st = self.info, self.info['store']
        own = sum(1 for e in self.list if e['place'] == 0)
        orig = sum(1 for e in self.list if e['place'] == 1 and e['origin'] != 2)
        rows = [('Versão', 'waveod %s' % i['version']), ('Protocolo', i['protocol']),
                ('Preset ativo', preset_label(i['active']) + (' (editado)' if i['modified'] else '')),
                ('Presets do waveod', own), ('Presets originais', orig),
                ('Posições vazias', len(self.list) - own - orig),
                ('Armazenamento', '%d KB em uso · %d de %d setores livres' % (st['live'] // 1024, st['free'],
                                                                               st['sectors']))]
        g = tk.Frame(b, bg=T['panel'])
        g.pack(fill='x', padx=p(18), pady=(0, p(18)))
        for k, (name, value) in enumerate(rows):
            tk.Label(g, text=name.upper(), font=F.tiny, bg=T['panel'], fg=T['muted']).grid(row=k, column=0,
                                                                                         sticky='w', pady=p(3))
            tk.Label(g, text=str(value), font=F.body, bg=T['panel'], fg=T['text']).grid(row=k, column=1, sticky='w',
                                                                                      padx=(p(16), 0))

    def update_fw(self):
        from waveod_studio.ui import updateview
        updateview.UpdateDialog(self, W, self.session.get('fw_path', ''))

    # ================================================================== flash
    def _flash_view(self):
        p, F = self.p, self.F
        v = tk.Frame(self.content, bg=T['bg'])
        tk.Label(v, text='Flash', font=F.h2, bg=T['bg'], fg=T['bright']).pack(anchor='w', padx=p(20),
                                                                              pady=(p(16), p(10)))
        card = W.Card(v, self, T['panel'], T['cardline'], radius=10, fit='x')
        card.pack(fill='x', padx=p(20))
        b = card.body
        tk.Label(b, text='Copia a flash do pedal para um arquivo, só lendo (nada é gravado). O dump completo inclui '
                 'todos os presets, IRs e capturas: guarde-o como backup.', font=F.small, bg=T['panel'],
                 fg=T['muted'], wraplength=p(760), justify='left').pack(anchor='w', padx=p(18), pady=(p(16), p(12)))
        row = tk.Frame(b, bg=T['panel'])
        row.pack(fill='x', padx=p(18), pady=(0, p(18)))
        self.dump_kind = 'all'
        self.dump_seg = W.SegBar(row, self, [('all', 'Flash inteira (8 MB)'), ('boot', 'Bootloader (128 KB)')],
                                 'all', self._dump_kind, height=34)
        self.dump_seg.pack(side='left')
        W.Btn(row, self, 'Fazer dump…', None, self.dump, 'primary', height=34).pack(side='left', padx=(p(12), 0))
        self.sum_card = W.Card(v, self, T['panel'], T['cardline'], radius=10, fit='x')
        self.sum_card.pack(fill='x', padx=p(20), pady=(p(16), 0))
        tk.Label(self.sum_card.body, text='O resumo aparece aqui depois de um dump completo.', font=F.small,
                 bg=T['panel'], fg=T['muted']).pack(anchor='w', padx=p(18), pady=p(16))
        return v

    def _dump_kind(self, k):
        self.dump_kind = k
        self.dump_seg.set(active=k)

    def dump(self):
        full = self.dump_kind == 'all'
        path = filedialog.asksaveasfilename(parent=self.root, defaultextension='.bin',
                                            initialdir=self.session.get('dump_dir'),
                                            initialfile='flash_full.bin' if full else 'bootloader.bin',
                                            filetypes=[('Imagem binária', '*.bin')])
        if not path:
            return
        self.session['dump_dir'] = os.path.dirname(path)
        start, length = (0, FLASH_SIZE) if full else (0, 0x20000)

        def work(p, prog):
            data = p.dump(start, length, lambda d, t: prog(d // 1024, t // 1024))
            open(path, 'wb').write(data)
            return data

        self.worker.run('Dump da flash (KB)', work, lambda data: self._dumped(path, data))

    def _dumped(self, path, data):
        self.status('%s: %d KB' % (os.path.basename(path), len(data) // 1024), T['green_soft'])
        if len(data) != FLASH_SIZE:
            return
        p, F, b = self.p, self.F, self.sum_card.body
        for w in b.winfo_children():
            w.destroy()
        tk.Label(b, text='Resumo de %s' % os.path.basename(path), font=F.h3, bg=T['panel'], fg=T['text']).pack(
            anchor='w', padx=p(18), pady=(p(16), p(8)))
        g = tk.Frame(b, bg=T['panel'])
        g.pack(fill='x', padx=p(18), pady=(0, p(18)))
        for c, h in enumerate(('ÁREA', 'FAIXA', 'BYTES USADOS', 'ÚLTIMO')):
            tk.Label(g, text=h, font=F.tiny, bg=T['panel'], fg=T['muted']).grid(row=0, column=c, sticky='w',
                                                                               padx=(0, p(24)))
        for r, (name, lo, hi, n, last) in enumerate(flashmap.summary(data), 1):
            for c, text in enumerate((name, '%06X-%06X' % (lo, hi), '%d KB' % ((n + 1023) // 1024),
                                      '%06X' % last if last is not None else '—')):
                tk.Label(g, text=text, font=F.mono if c else F.body, bg=T['panel'],
                         fg=T['text'] if n else T['dim']).grid(row=r, column=c, sticky='w', padx=(0, p(24)), pady=p(2))


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(prog='waveod_studio')
    ap.add_argument('--emu', metavar='FW.bin', help='usar o firmware no emulador do waveod em vez do pedal')
    ap.add_argument('--tab', choices=('presets', 'effects', 'nam', 'drums', 'pedal', 'flash'))
    ap.add_argument('--midi', metavar='F.mid', help='importar esse MIDI no lugar M1 da bateria depois de ler o pedal, '
                    'sem perguntar')
    ap.add_argument('--select', type=int, help='selecionar este preset (0..239) depois de ler o pedal')
    ap.add_argument('--shot', metavar='PNG', help='salvar uma captura da janela e sair')
    ap.add_argument('--after', type=int, default=4000, help='ms até a captura')
    ap.add_argument('--controls', metavar='[G]TECLA[:LIGAÇÃO]', help='com --shot: abrir os controles do preset '
                    'selecionado (G na frente: os controles globais) nessa tecla (número; 128+CC = MIDI), nessa '
                    'ligação, e capturar o diálogo')
    ap.add_argument('--latency', choices=sorted(LAT_ROUTES), help='medir a latência nessa rota depois de ler o '
                    'pedal, sem perguntar (com --emu: o emulador liga o cabo)')
    a = ap.parse_args(argv)
    if sys.platform == 'win32':
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:                           # noqa: BLE001
            pass
        try:
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('rea.waveod.studio')
        except Exception:                           # noqa: BLE001
            pass
    W.load_fonts()
    root = tk.Tk()
    app = App(root, a.emu)
    W.dark_titlebar(root)
    if a.tab:
        app.set_mode(a.tab)
    if a.select is not None:
        app.pending_select = a.select
    if a.latency:
        def measure():
            if app.worker.busy or app.worker.q.qsize() or not app.info:
                root.after(300, measure)
                return
            route = LAT_ROUTES[a.latency]

            def end(r, ok):
                (app._got_latency if ok else app.failed)(r)
                app.lat_done = True
            app.worker.run('Medindo a latência', lambda p, prog: p.measure_latency(route, prog),
                           lambda r: end(r, True), lambda e: end(e, False))
        root.after(500, measure)
    if a.midi:
        from waveod_studio import drums

        def import_midi():
            if app.worker.busy or app.worker.q.qsize() or not app.info:
                root.after(300, import_midi)
                return
            pat = drums.convert(open(a.midi, 'rb').read(), name=drums.name_from_path(a.midi))
            rec = drums.record(pat)

            def work(p, prog):
                p.drum_put(0, rec)
                p.drum_play(0, True)
                return p.drums()
            app.worker.run('Gravando o ritmo M1', work, lambda s: app.drums._done(s, 'M1: %s' % pat['name']))
        root.after(500, import_midi)
    if a.shot and a.controls:
        key, _, link = a.controls.lstrip('G').partition(':')

        def hook(dlg):
            dlg.key = int(key)
            if link:
                dlg._select(dlg._links()[int(link)])
            dlg._draw()

            def take():
                from waveod_studio import shot as S
                dlg.d.top.update()
                print('%s: %dx%d' % ((a.shot,) + S.window(dlg.d.top, a.shot)))
                root.destroy()
            root.after(800, take)
        app.dialog_hook = hook

        def open_controls():
            if app.worker.busy or app.worker.q.qsize() or not app.detail:
                root.after(300, open_controls)
                return
            if a.controls.startswith('G'):
                app.edit_global()
            else:
                app.edit_controls()
        root.after(a.after, open_controls)
    elif a.shot:
        def shot():
            if app.worker.busy or app.worker.q.qsize() or (a.latency and not getattr(app, 'lat_done', False)):
                root.after(300, shot)
                return
            from waveod_studio import shot as S
            print('%s: %dx%d' % ((a.shot,) + S.window(root, a.shot)))
            root.destroy()
        root.after(a.after, shot)
    root.mainloop()


if __name__ == '__main__':
    main()
