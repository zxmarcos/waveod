"""The Bateria tab: MEUS RITMOS, the drum patterns of waveod's own (protocol 7; waveod/src/model/drum_user.h).

Twenty places on the pedal, each filled from a MIDI file (drums.py converts it: the General MIDI drum notes onto the
13 sounds of the pedal's bank, ticks onto its grid of 1/120 beat, whole bars so a loop stays in time). On the pedal
they are the last style of the BATERIA screen; here they can be played, stopped and emptied. Writing a place
replaces what it held, so it asks first."""
import os
import tkinter as tk
from tkinter import filedialog, ttk

from waveod_studio import drums
from waveod_studio.ui import kit as W

T = W.T


class DrumsView:
    def __init__(self, app, parent):
        self.app, self.state = app, None
        p, F = app.p, app.F
        v = self.frame = tk.Frame(parent, bg=T['bg'])
        bar = tk.Frame(v, bg=T['bg'])
        bar.pack(fill='x', padx=p(20), pady=(p(16), p(10)))
        tk.Label(bar, text='Bateria · meus ritmos', font=F.h2, bg=T['bg'], fg=T['bright']).pack(side='left')
        W.Btn(bar, app, 'Importar MIDI…', None, self.import_midi, 'primary', height=34,
              tip='Converte um arquivo .mid de bateria e grava no lugar selecionado').pack(side='right')
        W.Btn(bar, app, 'Esvaziar', None, self.empty, 'ghost', height=34,
              tip='Apaga do pedal o ritmo do lugar selecionado').pack(side='right', padx=(0, p(8)))
        W.Btn(bar, app, 'Parar', None, lambda: self.play(False), 'ghost', height=34).pack(side='right', padx=(0, p(8)))
        W.Btn(bar, app, 'Tocar', None, lambda: self.play(True), 'ghost', height=34,
              tip='Toca o ritmo selecionado na bateria do pedal').pack(side='right', padx=(0, p(8)))
        W.Btn(bar, app, 'Ler de novo', None, self.refresh, 'ghost', height=34).pack(side='right', padx=(0, p(8)))
        tk.Label(v, text='Ritmos importados de arquivos MIDI. No pedal: BATERIA › estilo MEUS RITMOS. As notas de '
                 'bateria General MIDI viram os 13 sons do pedal (não há tons: essas notas ficam de fora). Um ritmo em '
                 'loop recomeça no fim de um compasso inteiro; "uma vez" para no fim, bom para uma música inteira. '
                 'O tempo do arquivo vira o da bateria, a não ser que ela siga o tempo dos efeitos.', font=F.small,
                 bg=T['bg'], fg=T['muted'], wraplength=p(1000), justify='left').pack(anchor='w', padx=p(20),
                                                                                     pady=(0, p(10)))
        card = W.Card(v, app, T['card'], T['cardline'], radius=10, fit='both')
        card.pack(fill='both', expand=True, padx=p(20))
        cols = (('place', 'Lugar', 70), ('name', 'Nome', 240), ('mode', 'Toca', 110), ('bpm', 'Tempo do MIDI', 120),
                ('events', 'Eventos', 110), ('state', '', 120))
        tv = self.tree = ttk.Treeview(card.body, columns=[c[0] for c in cols], show='headings', style='Wv.Treeview',
                                      selectmode='browse')
        for col, text, width in cols:
            tv.heading(col, text=text, anchor='w')
            tv.column(col, width=p(width), anchor='w', stretch=col == 'name')
        tv.tag_configure('empty', foreground=T['muted'])
        tv.tag_configure('used', foreground=T['text'])
        tv.tag_configure('playing', foreground=T['green_soft'])
        tv.pack(fill='both', expand=True)
        tv.bind('<Double-1>', lambda e: self.import_midi())
        self.usage = tk.Label(v, text='', font=F.small, bg=T['bg'], fg=T['muted'])
        self.usage.pack(anchor='w', padx=p(20), pady=(p(8), p(16)))
        self._fill()

    # ------------------------------------------------------------------ reading
    def shown(self):
        if self.state is None and self.app.info:
            self.refresh()

    def _ok(self):
        if not self.app.info:
            self.app.status('Conecte o pedal primeiro')
            return False
        if self.app.info.get('protocol', 1) < 7:
            self.app.status('O firmware do pedal não tem MEUS RITMOS: atualize o waveod', T['red'])
            return False
        return True

    def refresh(self):
        if self._ok():
            self.app.worker.run('Lendo os ritmos', lambda p, prog: p.drums(), self._got)

    def _got(self, state):
        keep = self.tree.selection()
        self.state = state
        self._fill()
        if keep:
            self.tree.selection_set(keep)
        self.app.status('Ritmos lidos')

    def _fill(self):
        tv = self.tree
        tv.delete(*tv.get_children())
        st = self.state
        if st is None:
            self.usage.configure(text='Conecte o pedal para ver os ritmos.')
            return
        for i, s in enumerate(st['slots']):
            playing = st['playing_place'] == i
            state = ('tocando' if st['playing'] else 'escolhido') if playing else ''
            if s is None:
                tv.insert('', 'end', iid=str(i), tags=('empty',), values=('M%d' % (i + 1), '—', '', '', '', state))
            else:
                tv.insert('', 'end', iid=str(i), tags=('playing' if playing else 'used',), values=(
                    'M%d' % (i + 1), s['name'], 'uma vez' if s['once'] else 'em loop',
                    '%d BPM' % s['bpm'] if s['bpm'] else '—', s['events'], state))
        n = sum(1 for s in st['slots'] if s)
        self.usage.configure(text='%d de %d lugares usados · até %d eventos por ritmo · %d sons no banco do pedal' % (
            n, st['places'], st['max_events'], st['sounds']))

    def _selected(self, need_used=False):
        sel = self.tree.selection()
        if not sel or self.state is None:
            self.app.status('Selecione um lugar')
            return None
        i = int(sel[0])
        if need_used and self.state['slots'][i] is None:
            self.app.status('O lugar M%d está vazio' % (i + 1))
            return None
        return i

    # ------------------------------------------------------------------ changes
    def import_midi(self):
        if not self._ok() or self.state is None:
            return
        i = self._selected()
        if i is None:
            free = [k for k, s in enumerate(self.state['slots']) if s is None]
            if not free:
                return
            i = free[0]
        path = filedialog.askopenfilename(parent=self.app.root, initialdir=self.app.session.get('midi_dir'),
                                          filetypes=[('Arquivo MIDI', '*.mid *.midi'), ('Todos', '*.*')])
        if not path:
            return
        self.app.session['midi_dir'] = os.path.dirname(path)
        try:
            data = open(path, 'rb').read()
            preview = drums.convert(data, name=drums.name_from_path(path))
        except (OSError, drums.MidiError) as e:
            self.app.status('Arquivo não serve: %s' % e, T['red'])
            return
        r = ImportDialog(self.app, i, self.state['slots'][i], path, preview).show(wait=True)
        if not r:
            return
        name, kit, once = r
        try:
            pat = drums.convert(data, kit=kit, once=once, name=name)
        except drums.MidiError as e:
            self.app.status('Arquivo não serve: %s' % e, T['red'])
            return
        rec = drums.record(pat, name)

        def work(p, prog):
            p.drum_put(i, rec)
            return p.drums()
        self.app.worker.run('Gravando o ritmo M%d' % (i + 1), work,
                            lambda s: self._done(s, 'M%d: %s (%d compassos, %d eventos)' % (
                                i + 1, pat['name'], pat['bars'], len(pat['events']))))

    def empty(self):
        if not self._ok():
            return
        i = self._selected(need_used=True)
        if i is None:
            return
        if not W.ask(self.app, 'Esvaziar M%d?' % (i + 1), 'Apaga do pedal o ritmo "%s". Para tê-lo de volta é preciso '
                     'importar o MIDI de novo.' % self.state['slots'][i]['name'], 'Esvaziar', 'danger'):
            return

        def work(p, prog):
            p.drum_put(i, b'')
            return p.drums()
        self.app.worker.run('Esvaziando M%d' % (i + 1), work, lambda s: self._done(s, 'M%d vazio' % (i + 1)))

    def play(self, on):
        if not self._ok():
            return
        i = self._selected(need_used=True) if on else None
        if on and i is None:
            return

        def work(p, prog):
            if on:
                p.drum_play(i, True)
            else:
                p.drum_play(self.state['playing_place'] if self.state else None, False)
            return p.drums()
        self.app.worker.run('Bateria', work, lambda s: self._done(s, 'Tocando M%d' % (i + 1) if on else 'Bateria parada'))

    def _done(self, state, what):
        self._got(state)
        self.app.status(what, T['green_soft'])


class ImportDialog(W.Dialog):
    """what the file gives (tempo, bars, what is left out), its name on the pedal, the kit, loop or once"""

    def __init__(self, app, place, slot, path, pat):
        super().__init__(app, 'Importar para M%d?' % (place + 1), 560)
        p, F = app.p, app.F
        b = self.body
        facts = '%s · %s · %d compasso%s (%.0f s) · %d notas · %d eventos' % (
            os.path.basename(path), '%d BPM' % pat['bpm'] if pat['bpm'] else 'sem tempo no arquivo (fica o da bateria)',
            pat['bars'], '' if pat['bars'] == 1 else 's', drums.length_seconds(pat), pat['hits'], len(pat['events']))
        if pat['channel'] is None:
            facts += ' · sem canal 10: todas as notas lidas como bateria'
        tk.Label(b, text=facts, font=F.small, bg=T['panel'], fg=T['soft'], wraplength=p(510),
                 justify='left').pack(anchor='w', pady=(0, p(8)))
        if pat['dropped']:
            tk.Label(b, text='Sem som no pedal, ficam de fora: %s' % drums.describe_dropped(pat['dropped']),
                     font=F.small, bg=T['panel'], fg=T['amber'], wraplength=p(510), justify='left').pack(
                anchor='w', pady=(0, p(8)))
        if pat['cut']:
            tk.Label(b, text='Longo demais: só os primeiros %d compassos cabem (%d ficam de fora).' % (
                pat['bars'], pat['cut']), font=F.small, bg=T['panel'], fg=T['amber'], wraplength=p(510),
                justify='left').pack(anchor='w', pady=(0, p(8)))
        tk.Label(b, text='Nome no pedal (até 16 letras)', font=F.small, bg=T['panel'], fg=T['muted']).pack(
            anchor='w', pady=(p(4), 0))
        self.name = tk.StringVar(value=pat['name'])
        W.Field(b, app, self.name, '', search=False, height=36).pack(fill='x', pady=(p(4), p(12)))
        tk.Label(b, text='Bumbo e caixa', font=F.small, bg=T['panel'], fg=T['muted']).pack(anchor='w')
        self.kit = 'acustico'
        self.kits = W.SegBar(b, app, [(k, drums.KIT_NAMES[k]) for k in drums.KITS], self.kit, self._kit, height=34)
        self.kits.pack(anchor='w', pady=(p(4), p(12)))
        self.once = tk.BooleanVar(value=False)
        W.Switch(b, app, 'Tocar uma vez e parar no fim (música inteira)', self.once).pack(anchor='w')
        tk.Label(b, text='Desligado: volta ao começo no fim, em loop.', font=F.small, bg=T['panel'], fg=T['muted'],
                 wraplength=p(510), justify='left').pack(anchor='w', pady=(p(4), p(12)))
        lost = 'O lugar está vazio.' if slot is None else 'Isto substitui "%s", que está neste lugar.' % slot['name']
        tk.Label(b, text=lost, font=F.body, bg=T['panel'], fg=T['text'] if slot is None else T['amber'],
                 wraplength=p(510), justify='left').pack(anchor='w')
        self.buttons([('Cancelar', 'ghost', self.close),
                      ('Gravar', 'primary' if slot is None else 'danger', self._ok)])

    def _kit(self, key):
        self.kit = key
        self.kits.set(active=key)

    def _ok(self):
        name = drums.clean_name(self.name.get())
        if not name:
            return
        self.close((name, self.kit, self.once.get()))
