"""The NAM tab: the 10 Clone slots of the pedal and NAM captures written into them (waveod/docs/nam.md).

A slot that holds a NAM capture (the A2-Lite network of a .nam file, nam.py) runs on waveod's NAM engine; the
others keep the original's Clone engine. A capture trained at 48 kHz runs at 48 kHz through a resampler unless it
is written to run at the pedal's 44.1 kHz. Writing a slot replaces what it held (the user's own captures live
there), so every write asks first, naming what will be lost."""
import os
import tkinter as tk
from tkinter import filedialog, ttk

from waveod_studio import nam
from waveod_studio.protocol import (CLONE_EMPTY, CLONE_KINDS, CLONE_NAM, CLONE_NAM_48K, CLONE_NAM_BROKEN,
                                    CLONE_ORIGINAL)
from waveod_studio.ui import kit as W

T = W.T
KIND_TAGS = {CLONE_EMPTY: 'empty', CLONE_ORIGINAL: 'orig', CLONE_NAM: 'nam', CLONE_NAM_48K: 'nam48',
             CLONE_NAM_BROKEN: 'bad'}


class NamView:
    def __init__(self, app, parent):
        self.app, self.slots = app, None
        p, F = app.p, app.F
        v = self.frame = tk.Frame(parent, bg=T['bg'])
        bar = tk.Frame(v, bg=T['bg'])
        bar.pack(fill='x', padx=p(20), pady=(p(16), p(10)))
        tk.Label(bar, text='Capturas NAM', font=F.h2, bg=T['bg'], fg=T['bright']).pack(side='left')
        W.Btn(bar, app, 'Gravar captura no slot…', None, self.write, 'primary', height=34,
              tip='Escolhe um arquivo .nam e grava a rede A2-Lite dele no slot selecionado').pack(side='right')
        W.Btn(bar, app, 'Ler de novo', None, self.refresh, 'ghost', height=34).pack(side='right', padx=(0, p(8)))
        tk.Label(v, text='Os 10 slots do Clone do pedal. Um slot com captura NAM (a rede A2-Lite de um arquivo .nam) '
                 'roda no motor NAM do waveod; os outros continuam no Clone original. Capturas treinadas a 48 kHz '
                 'rodam a 48 kHz com reamostragem (+1,4 ms), ou a 44,1 kHz se gravadas assim (mais leves, um pouco '
                 'mais brilhantes). No pedal: CLONE › NAM, com a taxa ao lado do nome.', font=F.small, bg=T['bg'],
                 fg=T['muted'], wraplength=p(1000), justify='left').pack(anchor='w', padx=p(20), pady=(0, p(10)))
        card = W.Card(v, app, T['card'], T['cardline'], radius=10, fit='both')
        card.pack(fill='both', expand=True, padx=p(20))
        cols = (('slot', 'Slot', 60), ('name', 'Nome', 220), ('kind', 'Conteúdo', 230), ('rate', 'Treinada a', 110),
                ('loud', 'Loudness', 100), ('crc', 'CRC dos pesos', 120))
        tv = self.tree = ttk.Treeview(card.body, columns=[c[0] for c in cols], show='headings', style='Wv.Treeview',
                                      selectmode='browse')
        for col, text, width in cols:
            tv.heading(col, text=text, anchor='w')
            tv.column(col, width=p(width), anchor='w', stretch=col == 'name')
        for tag, color in (('empty', T['muted']), ('orig', T['soft']), ('nam', T['text']), ('nam48', T['text']),
                           ('bad', T['red'])):
            tv.tag_configure(tag, foreground=color)
        tv.pack(fill='both', expand=True)
        tv.bind('<Double-1>', lambda e: self.write())
        self.usage = tk.Label(v, text='', font=F.small, bg=T['bg'], fg=T['muted'])
        self.usage.pack(anchor='w', padx=p(20), pady=(p(8), p(16)))
        self._fill()

    # ------------------------------------------------------------------ reading
    def shown(self):
        if self.slots is None and self.app.info:
            self.refresh()

    def _ok(self):
        if not self.app.info:
            self.app.status('Conecte o pedal primeiro')
            return False
        if self.app.info.get('protocol', 1) < 6:
            self.app.status('O firmware do pedal não roda capturas NAM: atualize o waveod', T['red'])
            return False
        return True

    def refresh(self):
        if self._ok():
            self.app.worker.run('Lendo os slots do Clone', lambda p, prog: p.clones(), self._got)

    def _got(self, slots):
        self.slots = slots
        self._fill()
        self.app.status('Slots do Clone lidos')

    def _fill(self):
        tv = self.tree
        tv.delete(*tv.get_children())
        if self.slots is None:
            self.usage.configure(text='Conecte o pedal para ver os slots do Clone.')
            return
        for s in self.slots:
            k = s['kind']
            is_nam = 'sample_rate' in s
            tv.insert('', 'end', iid=str(s['slot']), tags=(KIND_TAGS.get(k, 'orig'),), values=(
                s['slot'] + 1, s['name'], CLONE_KINDS.get(k, '?'),
                '%g kHz' % (s['sample_rate'] / 1000) if is_nam else '—',
                '%.1f dB' % s['loudness'] if is_nam else '—', '%08X' % s['crc'] if is_nam else '—'))
        n = sum(1 for s in self.slots if s['kind'] in (CLONE_NAM, CLONE_NAM_48K))
        r = sum(1 for s in self.slots if s['kind'] == CLONE_NAM_48K)
        self.usage.configure(text='%d de 10 slots com captura NAM%s' % (n, ' (%d com reamostragem)' % r if r else ''))

    # ------------------------------------------------------------------ writing
    def write(self):
        if not self._ok() or self.slots is None:
            return
        sel = self.tree.selection()
        if not sel:
            self.app.status('Selecione o slot que vai receber a captura')
            return
        slot = self.slots[int(sel[0])]
        path = filedialog.askopenfilename(parent=self.app.root, initialdir=self.app.session.get('nam_dir'),
                                          filetypes=[('Captura NAM', '*.nam')])
        if not path:
            return
        self.app.session['nam_dir'] = os.path.dirname(path)
        try:
            m = nam.load(path)
            data, info = nam.capture(path, m=m)
        except (OSError, ValueError) as e:
            self.app.status('Arquivo não serve: %s' % e, T['red'])
            return
        r = WriteDialog(self.app, slot, path, info).show(wait=True)
        if not r:
            return
        name, native = r
        if native:
            data, info = nam.capture(path, native_rate=True, m=m)

        def work(p, prog):
            p.write_clone(slot['slot'], name, data)
            return p.clones()
        self.app.worker.run('Gravando o slot %d do Clone' % (slot['slot'] + 1), work,
                            lambda s: self._done(s, 'Slot %d: %s (%s)' % (slot['slot'] + 1, name, info['runs_at'])))

    def _done(self, slots, what):
        self._got(slots)
        self.app.status(what, T['green_soft'])


class WriteDialog(W.Dialog):
    """the capture's facts, its name on the pedal, the rate, and what the slot loses"""

    def __init__(self, app, slot, path, info):
        super().__init__(app, 'Gravar no slot %d do Clone?' % (slot['slot'] + 1), 540)
        p, F = app.p, app.F
        b = self.body
        rate48 = 47000 < info['sample_rate'] < 49000
        facts = '%s · rede A2-Lite, %d pesos · treinada a %g kHz · loudness %s' % (
            os.path.basename(path), info['weights'], info['sample_rate'] / 1000,
            '%.1f dB' % info['loudness'] if info['loudness'] is not None else 'não informado (-18 dB)')
        tk.Label(b, text=facts, font=F.small, bg=T['panel'], fg=T['soft'], wraplength=p(490),
                 justify='left').pack(anchor='w', pady=(0, p(12)))
        tk.Label(b, text='Nome no pedal (até 16 letras)', font=F.small, bg=T['panel'], fg=T['muted']).pack(anchor='w')
        self.name = tk.StringVar(value=nam.default_name(path))
        W.Field(b, app, self.name, '', search=False, height=36).pack(fill='x', pady=(p(4), p(12)))
        self.native = tk.BooleanVar(value=False)
        if rate48:
            W.Switch(b, app, 'Rodar a 44,1 kHz, sem reamostragem', self.native).pack(anchor='w')
            tk.Label(b, text='Desligado: roda a 48 kHz como foi treinada (+1,4 ms, um pouco mais de DSP). Ligado: '
                     'mais leve, o som fica um pouco mais brilhante.', font=F.small, bg=T['panel'], fg=T['muted'],
                     wraplength=p(490), justify='left').pack(anchor='w', pady=(p(4), p(12)))
        lost = 'O slot está vazio.' if slot['kind'] == CLONE_EMPTY else \
            'Isto substitui o que o slot tem agora: %s (%s). Não dá para desfazer pelo pedal; guarde uma cópia ' \
            'antes se for uma captura sua.' % (slot['name'], CLONE_KINDS.get(slot['kind'], '?'))
        tk.Label(b, text=lost, font=F.body, bg=T['panel'], fg=T['text'] if slot['kind'] == CLONE_EMPTY else T['amber'],
                 wraplength=p(490), justify='left').pack(anchor='w')
        self.buttons([('Cancelar', 'ghost', self.close),
                      ('Gravar', 'primary' if slot['kind'] == CLONE_EMPTY else 'danger', self._ok)])

    def _ok(self):
        name = self.name.get().strip()
        if not name:
            return
        self.close((name[:nam.NAME_MAX], self.native.get()))
