"""The control table editor: what each footswitch, pedal and MIDI CC does in a preset (controls.py), laid out like
the pedal's CONTROLES / PARA ONDE screens: the keys on the left, the selected key's links on the right, and below
them where the selected link goes (destination, what of it, when or the curve, its values).

The footswitches' links belong to a footswitch mode (the bar on top); the toe switch, the pedals and MIDI work in
every mode. Nothing reaches the pedal until "Gravar no pedal".

The same dialog edits the global table (is_global: no blocks, no "preset ativo" key). On a preset, the global
links of the groups it has none in show greyed below its own: "Copiar" makes the group the preset's, "Sem globais"
gives it a NOTHING link (the key does nothing there in this preset).
"""
import tkinter as tk

from waveod_studio import controls as C, wvpreset
from waveod_studio.ui import kit as W

T = W.T

DEST_SCENES, DEST_FUNCTIONS, DEST_MIDI = 'Cenas', 'Funções', 'MIDI'
MOD_TYPE_NAMES = ('Desligado', 'LFO', 'Envelope', 'Sequência')                 # wvpreset.MOD_TYPES' order
MOD_SHAPE_NAMES = ('Senoide', 'Triângulo', 'Quadrada', 'Dente subindo', 'Dente descendo', 'Aleatória',
                   'Aleatória suave')                                       # wvpreset.MOD_SHAPES' order
DIVISIONS = ('Livre', '1/1', '1/2', '1/2D', '1/2T', '1/4', '1/4D', '1/4T', '1/8', '1/8D', '1/8T', '1/16')


class ControlsDialog:
    def __init__(self, app, title, table, blocks, knobs=None, scene_names=None, glob=None, is_global=False,
                 mods=None):
        """table: controls.Table (edited in place on a copy), blocks: [controls.Block], knobs: {slot: [values]}
        for the footswitch defaults, scene_names: the preset's four (or empty), glob: the global table (shown
        where the preset has no links), is_global: table is the global one, mods: the preset's modulators
        (wvpreset.Preset.mods(); edited on a copy, read back from .mods after show)"""
        self.app, self.blocks, self.knobs = app, blocks, knobs or {}
        self.glob, self.is_global = (None if is_global else glob), is_global
        self.scene_names = list(scene_names or [''] * C.SCENE_COUNT)
        self.table = table.copy()
        self.mods = [dict(m, step=list(m['step'])) for m in mods] if mods and not is_global else None
        self.mode, self.key, self.sel = C.STOMP, C.A, None
        self.extra_keys = set()                         # MIDI keys added here that have no link yet
        p = self.p = app.p
        F = app.F
        d = self.d = W.Dialog(app, title, 1000)
        top = tk.Frame(d.body, bg=T['panel'])
        top.pack(fill='x', pady=(0, p(12)))
        self.seg = W.SegBar(top, app, [(C.PRESET, 'Modo preset'), (C.STOMP, 'Modo stomp'), (C.SCENE, 'Modo cenas')],
                            self.mode, self._set_mode)
        self.seg.pack(side='left')
        tk.Label(top, text='Valem nos presets sem ligação própria na tecla · bico, pedais e MIDI em todos os modos'
                 if is_global else 'Bico, pedais e MIDI valem em todos os modos', font=F.small, bg=T['panel'],
                 fg=T['muted']).pack(side='right')
        body = tk.Frame(d.body, bg=T['panel'])
        body.pack(fill='both', expand=True)
        left = W.Card(body, app, T['card'], T['cardline'], radius=10, fit='both')
        left.configure(width=p(250))
        left.pack(side='left', fill='y')
        self.keys_sf = W.ScrollFrame(left.body, app, T['card'])
        self.keys_sf.configure(width=p(230), height=p(500))
        self.keys_sf.pack_propagate(False)
        self.keys_sf.pack(fill='both', expand=True, padx=p(6), pady=p(6))
        d.top.bind('<MouseWheel>', lambda e: self.keys_sf._wheel(e) if str(e.widget).startswith(
            str(self.keys_sf)) else None)
        self.right = tk.Frame(body, bg=T['panel'])
        self.right.pack(side='left', fill='both', expand=True, padx=(p(18), 0))
        self.right.configure(width=p(700), height=p(512))
        self.right.pack_propagate(False)
        d.buttons([('Cancelar', 'ghost', d.close), ('Gravar no pedal', 'primary', lambda: d.close(True))])
        self._draw()
        if getattr(app, 'dialog_hook', None):
            app.root.after(400, lambda: app.dialog_hook(self))

    def show(self):
        """the edited table (new links left without a destination dropped), or None when cancelled"""
        if not self.d.show(wait=True):
            return None
        self.table.links = [l for l in self.table.links if not (l.action == C.FN and l.arg == 0) and
                            (not self.is_global or C.global_ok(l))]
        return self.table

    # ------------------------------------------------------------------ state
    def _set_mode(self, mode):
        self.mode, self.sel = mode, None
        if self.key == C.AGAIN and mode != C.PRESET:
            self.key = C.A
        self.seg.set(active=mode)
        self._draw()

    def _set_key(self, key):
        self.key, self.sel = key, None
        self._draw()

    def _links(self):
        return self.table.of(self.key, self.mode)

    def _block(self, slot):
        return next((b for b in self.blocks if b.slot == slot), None)

    # ------------------------------------------------------------------ drawing
    def _draw(self):
        self._draw_keys()
        self._draw_right()

    def _draw_keys(self):
        p, F = self.p, self.app.F
        box = self.keys_sf.inner
        self.keys_sf.keep()
        for w in box.winfo_children():
            w.destroy()

        def row(key):
            n = len(self.table.of(key, self.mode))
            g = len(C.inherited(self.table, self.glob, key, self.mode))
            count = ('%d' % n if n else '') + (' +%dG' % g if g and n else '%dG' % g if g else '')
            W.RowButton(box, self.app, C.key_name(key) if key != C.AGAIN else 'PRESET ATIVO DE NOVO',
                        count, active=key == self.key, height=30,
                        command=lambda k=key: self._set_key(k)).pack(fill='x', pady=(0, p(2)))

        def heading(text):
            tk.Label(box, text=text, font=F.tiny, bg=T['card'], fg=T['muted']).pack(anchor='w', padx=p(8),
                                                                                  pady=(p(8), p(4)))
        heading('FOOTSWITCHES')
        for k in range(C.AGAIN + 1):
            if k != C.AGAIN or (self.mode == C.PRESET and not self.is_global):
                row(k)
        heading('PEDAIS')
        for k in (C.TOE, C.EXP1A, C.EXP1B, C.EXP2):
            row(k)
        if self.mods:
            heading('MODULADORES')
            for k in range(C.MOD1, C.MOD1 + 4):
                row(k)
        heading('MIDI')
        glob_cc = set(self.glob.midi_keys()) if self.glob else set()
        for k in sorted(set(self.table.midi_keys()) | glob_cc | self.extra_keys |
                        ({self.key} if C.is_midi(self.key) else set())):
            row(k)
        add = tk.Frame(box, bg=T['card'])
        add.pack(fill='x', padx=p(6), pady=(p(6), p(8)))
        self.cc_var = tk.StringVar()
        f = W.Field(add, self.app, self.cc_var, 'CC 1-127', search=False, height=32, width=90, bg=T['input'])
        f.pack(side='left')
        f.entry.bind('<Return>', lambda e: self._add_cc())
        W.Btn(add, self.app, 'Adicionar', None, self._add_cc, 'ghost', height=32, padx=10).pack(side='left',
                                                                                              padx=(p(6), 0))

    def _add_cc(self):
        try:
            cc = int(self.cc_var.get().strip())
        except ValueError:
            cc = -1
        if not C.midi_cc_ok(cc):
            self.app.toast_msg('CC de 1 a 127 (menos 68 e 69, o tempo)', 'warn')
            return
        self.extra_keys.add(C.MIDI_KEY + cc)
        self._set_key(C.MIDI_KEY + cc)

    def _draw_right(self):
        p, F, r = self.p, self.app.F, self.right
        for w in r.winfo_children():
            w.destroy()
        key = self.key
        head = tk.Frame(r, bg=T['panel'])
        head.pack(fill='x')
        tk.Label(head, text=C.key_name(key), font=F.h3, bg=T['panel'], fg=T['bright']).pack(side='left')
        where = 'todos os modos' if C.every_mode(key) else 'modo ' + C.MODE_NAMES[self.mode].lower()
        tk.Label(head, text='· ' + where, font=F.small, bg=T['panel'], fg=T['muted']).pack(side='left',
                                                                                         padx=(p(8), 0))
        if key < C.FOOTSWITCH_KEYS:
            self._draw_card_fields(head)
        if C.is_mod(key) and self.mods:
            self._draw_mod(r)
        # the links
        lst = tk.Frame(r, bg=T['panel'])
        lst.pack(fill='x', pady=(p(12), 0))
        links = self._links()
        inh = C.inherited(self.table, self.glob, key, self.mode)
        if not links and not inh:
            tk.Label(lst, text='Nenhuma ligação.', font=F.body, bg=T['panel'], fg=T['dim']).pack(anchor='w')
        for i in links[:8]:
            self._link_row(lst, i)
        if len(links) > 8:
            tk.Label(lst, text='+%d ligações (a lista mostra 8)' % (len(links) - 8), font=F.small, bg=T['panel'],
                     fg=T['muted']).pack(anchor='w')
        for gi in inh[:max(0, 8 - len(links))]:
            self._global_row(lst, gi)
        acts = tk.Frame(r, bg=T['panel'])
        acts.pack(fill='x', pady=(p(8), 0))
        W.Btn(acts, self.app, 'Nova ligação', W.ICO['add'], self._new, 'ghost', height=32).pack(side='left')
        tk.Label(acts, text='%d de %d na tabela' % (len(self.table.links), C.CONTROL_MAX), font=F.mono_xs,
                 bg=T['panel'], fg=T['faint']).pack(side='right')
        if self.sel is not None and self.sel < len(self.table.links):
            self._draw_editor(r)

    def _draw_card_fields(self, head):
        p = self.p
        k = self.key
        cv = W.Dropdown(head, self.app, C.COLOUR_NAMES, C.COLOUR_NAMES[self.table.colour[k] & 7],
                        lambda v: self._set_colour(C.COLOUR_NAMES.index(v)), width=150, height=32)
        cv.pack(side='right')
        tk.Label(head, text='COR', font=self.app.F.tiny, bg=T['panel'], fg=T['muted']).pack(side='right',
                                                                                          padx=(p(14), p(6)))
        var = tk.StringVar(value=self.table.label[k])
        f = W.Field(head, self.app, var, 'automático', search=False, height=32, width=130)
        f.pack(side='right')
        f.entry.bind('<FocusOut>', lambda e: self._set_label(var.get()))
        f.entry.bind('<Return>', lambda e: self._set_label(var.get()))
        tk.Label(head, text='NOME NO CARD', font=self.app.F.tiny, bg=T['panel'], fg=T['muted']).pack(
            side='right', padx=(0, p(6)))

    # ------------------------------------------------------------------ a modulator's settings
    def _draw_mod(self, r):
        """the selected modulator's settings (waveod/docs/modulators.md), above its links"""
        p, F = self.p, self.app.F
        m = self.mods[self.key - C.MOD1]
        box = W.Card(r, self.app, T['card'], T['cardline'], radius=10, fit='x')
        box.pack(fill='x', pady=(p(12), 0))
        g = tk.Frame(box.body, bg=T['card'])
        g.pack(fill='x', padx=p(16), pady=p(12))
        col = [0]

        def drop(title, names, cur, apply, width=150):
            tk.Label(g, text=title, font=F.tiny, bg=T['card'], fg=T['muted']).grid(row=0, column=col[0], sticky='w',
                                                                                  padx=(0, p(12)), pady=(0, p(4)))
            W.Dropdown(g, self.app, list(names), names[cur], lambda v: (apply(list(names).index(v)), self._draw()),
                       width=width, height=34, bg=T['input']).grid(row=1, column=col[0], sticky='w', padx=(0, p(12)))
            col[0] += 1

        def number(title, value, apply, hint, lo, hi, scale=1.0):
            def put(v):
                apply(max(lo, min(hi, v)) * scale)
            self._number(g, col[0], title, value, put, hint)
            col[0] += 1

        def set_type(i):
            t = wvpreset.MOD_TYPES[i]
            if m['type'] == 'off' and t != 'off':
                m.update(wvpreset.mod_default(t))
            m['type'] = t
        drop('TIPO', MOD_TYPE_NAMES, wvpreset.MOD_TYPES.index(m['type']), set_type)
        if m['type'] == 'lfo':
            drop('FORMA', MOD_SHAPE_NAMES, wvpreset.MOD_SHAPES.index(m['shape']),
                 lambda i: m.update(shape=wvpreset.MOD_SHAPES[i]), 170)
        if m['type'] in ('lfo', 'seq'):
            drop('SINCRONIA', DIVISIONS, m['sync'] % len(DIVISIONS), lambda i: m.update(sync=i), 100)
            if not m['sync']:
                number('VELOCIDADE', '%.2f' % m['rate'], lambda v: m.update(rate=round(v, 2)), '0,05 a 20 Hz', 0.05,
                       20.0)
        if m['type'] == 'lfo':
            number('FASE INICIAL', str(m['phase']), lambda v: m.update(phase=int(v)), '0 a 95 %', 0, 95)
        elif m['type'] == 'env':
            number('SENSIBILIDADE', str(m['sens']), lambda v: m.update(sens=int(v)), '-20 a 40 dB', -20, 40)
            number('ATAQUE', str(m['attack']), lambda v: m.update(attack=int(v)), '1 a 2000 ms', 1, 2000)
            number('RETORNO', str(m['release']), lambda v: m.update(release=int(v)), '10 a 5000 ms', 10, 5000)
        elif m['type'] == 'seq':
            drop('PASSOS', [str(n) for n in range(1, wvpreset.MOD_STEPS + 1)], m['steps'] - 1,
                 lambda i: m.update(steps=i + 1), 80)
            number('DESLIZE', str(m['glide']), lambda v: m.update(glide=int(v)), '0 a 100 %', 0, 100)
            steps = tk.Frame(box.body, bg=T['card'])
            steps.pack(fill='x', padx=p(16), pady=(0, p(12)))
            for i in range(m['steps']):
                var = tk.StringVar(value=str(m['step'][i]))
                tk.Label(steps, text=str(i + 1), font=F.tiny, bg=T['card'], fg=T['muted']).grid(row=0, column=i)
                f = W.Field(steps, self.app, var, '', search=False, height=30, width=48, bg=T['input'], font=F.mono)
                f.grid(row=1, column=i, padx=(0, p(3)))

                def done(e=None, i=i, var=var):
                    try:
                        m['step'][i] = max(0, min(100, int(float(var.get().replace(',', '.')))))
                    except ValueError:
                        pass
                    var.set(str(m['step'][i]))
                f.entry.bind('<Return>', done)
                f.entry.bind('<FocusOut>', done)
        if m['type'] != 'off' and not self._links():
            tk.Label(box.body, text='Ligue o modulador a um parâmetro: Nova ligação, abaixo.', font=F.small,
                     bg=T['card'], fg=T['muted']).pack(anchor='w', padx=p(16), pady=(0, p(12)))

    def _set_colour(self, c):
        self.table.colour[self.key] = c

    def _set_label(self, text):
        self.table.label[self.key] = text.upper()[:C.LABEL_LEN]

    def _link_row(self, parent, i):
        p, F = self.p, self.app.F
        l = self.table.links[i]
        sel = i == self.sel
        bg = T['sel'] if sel else T['tile']
        card = W.Card(parent, self.app, bg, T['accent'] if sel else T['line2'], radius=8, fit='x')
        card.pack(fill='x', pady=(0, p(6)))
        b = card.body
        b.configure(height=p(34), cursor='hand2')
        b.pack_propagate(False)
        when = C.when_text(l)
        if when:
            W.Pill(b, self.app, when, T['accent'] if l.when else T['muted'], bg=bg, height=20).pack(
                side='left', padx=(p(8), 0))
        modes = [C.MODE_NAMES[m] for m in (C.PRESET, C.STOMP, C.SCENE) if l.modes & m and m != self.mode]
        text = C.link_text(l, self.blocks) + ('   (também: %s)' % ', '.join(modes) if modes and when else '')
        lbl = tk.Label(b, text=text, font=F.body, bg=bg, fg=T['bright'] if sel else T['text'], anchor='w',
                       cursor='hand2')
        lbl.pack(side='left', fill='x', expand=True, padx=(p(10), 0))
        W.Btn(b, self.app, '', W.ICO['remove'], lambda: self._remove(i), 'ghost', height=26, padx=8,
              tip='Remover a ligação').pack(side='right', padx=(0, p(6)))
        for w in (card, b, lbl):
            w.bind('<ButtonRelease-1>', lambda e: self._select(i))

    def _global_row(self, parent, gi):
        """a global link that runs in this preset (greyed), with Copiar / Sem globais"""
        p, F = self.p, self.app.F
        l = self.glob.links[gi]
        bg = T['panel']
        card = W.Card(parent, self.app, bg, T['line2'], radius=8, fit='x')
        card.pack(fill='x', pady=(0, p(6)))
        b = card.body
        b.configure(height=p(34))
        b.pack_propagate(False)
        W.Pill(b, self.app, 'GLOBAL', T['dim'], bg=bg, height=20).pack(side='left', padx=(p(8), 0))
        when = C.when_text(l)
        if when:
            W.Pill(b, self.app, when, T['muted'], bg=bg, height=20).pack(side='left', padx=(p(6), 0))
        tk.Label(b, text=C.link_text(l, self.blocks), font=F.body, bg=bg, fg=T['muted'], anchor='w').pack(
            side='left', fill='x', expand=True, padx=(p(10), 0))
        W.Btn(b, self.app, 'Sem globais', None, lambda: self._nothing(gi), 'ghost', height=26, padx=8,
              tip='A tecla não faz nada aqui neste preset').pack(side='right', padx=(0, p(6)))
        W.Btn(b, self.app, 'Copiar', None, lambda: self._copy_group(gi), 'ghost', height=26, padx=8,
              tip='Copia as ligações globais desta tecla para o preset, para editar').pack(side='right',
                                                                                          padx=(0, p(6)))

    def _key_modes(self):
        return C.ALL if C.every_mode(self.key) else self.mode

    def _copy_group(self, gi, select=True):
        """the global links of gi's group copied into the preset (which then has that group); the copy of gi
        selected"""
        hold = self.glob.links[gi].when == C.HOLD
        at = None
        try:
            for i in self.glob.group(self.key, self.mode, hold):
                l = self.glob.links[i].copy()
                l.modes = self._key_modes()
                n = self.table.add(l)
                if i == gi:
                    at = n
        except ValueError as e:
            self.app.toast_msg(str(e), 'warn')
        if select:
            self.sel = at
            self._draw()

    def _nothing(self, gi):
        hold = self.glob.links[gi].when == C.HOLD
        try:
            self.table.add(C.Link(self.key, self._key_modes(), C.HOLD if hold else C.PRESS, C.NOTHING, -1, 0))
        except ValueError as e:
            self.app.toast_msg(str(e), 'warn')
        self.sel = None
        self._draw()

    # ------------------------------------------------------------------ link changes
    def _select(self, i):
        try:
            self.sel = self.table.own(i, self.mode)     # a link shared by several modes becomes this mode's
        except ValueError as e:
            self.app.toast_msg(str(e), 'warn')
            return
        self._draw()

    def _new(self):
        if self.glob and not self.table.group(self.key, self.mode, 0) and self.glob.group(self.key, self.mode, 0):
            self._copy_group(self.glob.group(self.key, self.mode, 0)[0], select=False)  # joins the global ones
        try:
            self.sel = self.table.add(C.new_link(self.key, self.mode))
        except ValueError as e:
            self.app.toast_msg(str(e), 'warn')
            return
        self.extra_keys.discard(self.key)
        self._draw()

    def _remove(self, i):
        self.table.remove(i, self.mode)
        self.sel = None
        self._draw()

    # ------------------------------------------------------------------ the selected link
    def _dests(self):
        """[(label, kind, block)] the link can go to"""
        out, seen = [], {}
        for blk in self.blocks:
            seen[blk.name] = seen.get(blk.name, 0) + 1
            name = blk.name if seen[blk.name] == 1 else '%s (%d)' % (blk.name, seen[blk.name])
            out.append((name, 'block', blk))
        if C.is_mod(self.key):                          # a modulator: parameters of blocks only
            return out
        if not C.is_pedal(self.key):
            out += [(DEST_SCENES, 'scenes', None), (DEST_FUNCTIONS, 'fn', None)]
        out.append((DEST_MIDI, 'midi', None))
        return out

    def _choices(self, kind, blk):
        """[(label, apply(link))] of a destination"""
        key = self.key
        if kind == 'scenes':
            names = [n or 'CENA %d' % (k + 1) for k, n in enumerate(self.scene_names)]
            out = [('%d · %s' % (k + 1, n) if n.upper() != 'CENA %d' % (k + 1) else n.upper(),
                    lambda l, k=k: C.set_simple(l, C.SCENE_ACT, -1, k)) for k, n in enumerate(names)]
            return out + [('PRÓXIMA CENA', lambda l: C.set_simple(l, C.SCENE_ACT, -1, C.SCENE_NEXT)),
                          ('CENA ANTERIOR', lambda l: C.set_simple(l, C.SCENE_ACT, -1, C.SCENE_PREV))]
        if kind == 'fn':
            return [(name, lambda l, fn=fn: C.set_simple(l, C.FN, -1, fn)) for fn, name in C.FUNCTIONS]
        if kind == 'midi':
            out = [('ENVIAR CC', lambda l: C.set_midi(l, False))]
            if not C.is_pedal(key):
                out.append(('ENVIAR PC', lambda l: C.set_midi(l, True)))
            return out
        if C.is_mod(key):
            out = []
        elif C.is_pedal(key):
            out = [('LIGA AO MOVER', lambda l: C.set_simple(l, C.BLOCK, blk.slot, 1))]
            if key != C.EXP2:
                out.append(('COM O GRUPO', lambda l: C.set_simple(l, C.BLOCK, blk.slot, 0)))
        else:
            out = [('LIGA O BLOCO', lambda l: C.set_simple(l, C.BLOCK, blk.slot, 0)),
                   ('DESLIGA O BLOCO', lambda l: C.set_simple(l, C.BLOCK, blk.slot, 1))]
        knobs = self.knobs.get(blk.slot)
        for prm in blk.params:
            if prm.knob < C.KNOBS:
                out.append((prm.name.upper(), lambda l, prm=prm: C.set_param(l, blk, prm, knobs)))
        return out

    def _current(self, l, dests):
        """(dest index, choice label) the link has now; dest -1 = none yet"""
        for n, (label, kind, blk) in enumerate(dests):
            if kind == 'block' and l.action in (C.BLOCK, C.PARAM) and blk.slot == l.slot:
                if l.action == C.BLOCK:
                    return n, C.block_choice_text(l)
                prm = C.link_param(l, self.blocks)
                return n, prm.name.upper() if prm else None
            if (kind == 'scenes' and l.action == C.SCENE_ACT) or (kind == 'midi' and l.action == C.MIDI):
                choice = next((c for c, f in self._choices(kind, None) if self._probe(l, f)), None)
                return n, choice
            if kind == 'fn' and l.action == C.FN and l.arg:
                return n, C.fn_name(l.arg)
        return -1, None

    @staticmethod
    def _probe(l, apply):
        """does `apply` give link l its kind (scene / MIDI choices)"""
        t = l.copy()
        apply(t)
        return t.action == l.action and (t.arg == l.arg if l.action == C.SCENE_ACT else t.flags == l.flags)

    def _draw_editor(self, r):
        p, F = self.p, self.app.F
        l = self.table.links[self.sel]
        dests = self._dests()
        cur, choice = self._current(l, dests)
        box = W.Card(r, self.app, T['card'], T['cardline'], radius=10, fit='x')
        box.pack(fill='x', pady=(p(14), 0))
        g = tk.Frame(box.body, bg=T['card'])
        g.pack(fill='x', padx=p(16), pady=p(14))

        def label(text, col, row=0):
            tk.Label(g, text=text, font=F.tiny, bg=T['card'], fg=T['muted']).grid(row=row, column=col, sticky='w',
                                                                                 padx=(0, p(12)), pady=(0, p(4)))
        label('PARA ONDE', 0)
        W.Dropdown(g, self.app, [d[0] for d in dests], dests[cur][0] if cur >= 0 else '—',
                   lambda v: self._pick_dest(v, dests), width=210, height=34, bg=T['input']).grid(
            row=1, column=0, sticky='w', padx=(0, p(12)))
        if cur >= 0:
            kind, blk = dests[cur][1], dests[cur][2]
            items = self._choices(kind, blk)
            label('O QUÊ', 1)
            W.Dropdown(g, self.app, [c[0] for c in items], choice or '—', lambda v: self._pick(v, items),
                       width=230, height=34, bg=T['input']).grid(row=1, column=1, sticky='w', padx=(0, p(12)))
        wv = self._when_values(l)
        if wv:
            label(wv[0], 2)
            W.Dropdown(g, self.app, wv[1], wv[1][wv[2]], lambda v: self._set_when(wv[1].index(v)), width=150,
                       height=34, bg=T['input']).grid(row=1, column=2, sticky='w')
        vals = tk.Frame(box.body, bg=T['card'])
        vals.pack(fill='x', padx=p(16), pady=(0, p(14)))
        if l.action == C.PARAM:
            self._param_values(vals, l)
        elif l.action == C.MIDI:
            self._midi_values(vals, l)
        elif l.action == C.FN and not l.arg:
            tk.Label(vals, text='Escolha para onde a ligação vai (sem isso ela não é gravada).', font=F.small,
                     bg=T['card'], fg=T['muted']).pack(anchor='w')

    def _when_values(self, l):
        """(title, names, current) of the when / curve choice, or None"""
        if C.sweeps(l.key):
            if l.action == C.PARAM or (l.action == C.MIDI and not l.pc):
                return 'CURVA', list(C.CURVE_NAMES), l.curve % 3
            return None
        if l.key == C.TOE:
            return None
        return 'QUANDO', list(C.WHEN_NAMES), l.when % 3

    def _set_when(self, v):
        l = self.table.links[self.sel]
        if C.sweeps(l.key):
            l.curve = v
        else:
            l.when = v
        self._draw_right()

    def _pick_dest(self, label, dests):
        n = next(k for k, d in enumerate(dests) if d[0] == label)
        items = self._choices(dests[n][1], dests[n][2])
        if items:
            self._pick(items[0][0], items)

    def _pick(self, label, items):
        l = self.table.links[self.sel]
        next(f for c, f in items if c == label)(l)
        self._draw()

    def _ends(self, l):
        if C.is_pedal(l.key):
            return 'CALCANHAR', 'PONTA'
        if C.is_midi(l.key):
            return 'EM 0', 'EM 127'
        if C.is_mod(l.key):
            return 'MOD EM 0%', 'MOD EM 100%'
        return 'OFF', 'ON'

    def _number(self, parent, col, title, value, apply, hint=''):
        p, F = self.p, self.app.F
        tk.Label(parent, text=title, font=F.tiny, bg=T['card'], fg=T['muted']).grid(row=0, column=col, sticky='w',
                                                                                  padx=(0, p(12)), pady=(0, p(4)))
        var = tk.StringVar(value=value)
        f = W.Field(parent, self.app, var, '', search=False, height=34, width=110, bg=T['input'], font=F.mono)
        f.grid(row=1, column=col, sticky='w', padx=(0, p(12)))
        if hint:
            tk.Label(parent, text=hint, font=F.mono_xs, bg=T['card'], fg=T['faint']).grid(row=2, column=col,
                                                                                         sticky='w', pady=(p(3), 0))

        def done(e=None):
            try:
                v = float(var.get().replace(',', '.'))
            except ValueError:
                self._draw_right()
                return
            apply(v)
            self._draw_right()
        f.entry.bind('<Return>', done)
        f.entry.bind('<FocusOut>', done)

    def _param_values(self, vals, l):
        prm = C.link_param(l, self.blocks)
        if not prm:
            return
        lo, hi = self._ends(l)
        hint = '%s a %s' % (prm.fmt(prm.min), prm.fmt(prm.max))

        def setter(which):
            def apply(v):
                v = prm.quantize(v)
                if which:
                    l.on = v
                else:
                    l.off = v
            return apply
        self._number(vals, 0, lo, prm.fmt(l.off), setter(0), hint)
        self._number(vals, 1, hi, prm.fmt(l.on), setter(1), hint)

    def _midi_values(self, vals, l):
        p, F = self.p, self.app.F

        def clamp(v, top):
            return max(0, min(top, int(round(v))))

        def set_arg(v):
            l.arg = clamp(v, 127)

        def set_end(which):
            def apply(v):
                if which:
                    l.on = float(clamp(v, 127))
                else:
                    l.off = float(clamp(v, 127))
            return apply
        self._number(vals, 0, 'PC' if l.pc else 'CC', str(l.arg), set_arg, '0 a 127')
        tk.Label(vals, text='CANAL', font=F.tiny, bg=T['card'], fg=T['muted']).grid(row=0, column=1, sticky='w',
                                                                                  padx=(0, p(12)), pady=(0, p(4)))
        chans = [str(c) for c in range(1, 17)]

        def set_channel(v):
            l.slot = int(v) - 1
            self._draw_right()
        W.Dropdown(vals, self.app, chans, str((l.slot & 15) + 1), set_channel, width=90, height=34,
                   bg=T['input']).grid(row=1, column=1, sticky='w', padx=(0, p(12)))
        if not l.pc:
            lo, hi = self._ends(l)
            self._number(vals, 2, lo, str(round(l.off)), set_end(0), '0 a 127')
            self._number(vals, 3, hi, str(round(l.on)), set_end(1), '0 a 127')
