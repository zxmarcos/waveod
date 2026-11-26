"""The Efeitos tab: waveod's effect library on the pedal (protocol 5; waveod/docs/blocks.md). Lists the effect
modules installed, installs module files (.elf, built by waveod/tools/wvfx.py build) and removes them. The
pedal lists them after the original's effects and its built-in ones; a preset that uses a removed one keeps it
(shown as AUSENTE, the signal passes) until it is installed again."""
import os
import tkinter as tk
from tkinter import filedialog, ttk

from waveod_studio import wvlib
from waveod_studio.ui import kit as W

T = W.T


class EffectsView:
    def __init__(self, app, parent):
        self.app, self.lib, self.cap = app, None, 0
        p, F = app.p, app.F
        v = self.frame = tk.Frame(parent, bg=T['bg'])
        bar = tk.Frame(v, bg=T['bg'])
        bar.pack(fill='x', padx=p(20), pady=(p(16), p(10)))
        tk.Label(bar, text='Efeitos', font=F.h2, bg=T['bg'], fg=T['bright']).pack(side='left')
        W.Btn(bar, app, 'Remover', None, self.remove, 'ghost', height=34,
              tip='Tira do pedal os módulos selecionados').pack(side='right')
        W.Btn(bar, app, 'Instalar módulos…', None, self.install, 'primary', height=34,
              tip='Grava no pedal módulos de efeito (.elf) feitos com tools/wvfx.py').pack(side='right', padx=(0, p(8)))
        W.Btn(bar, app, 'Ler de novo', None, self.refresh, 'ghost', height=34).pack(side='right', padx=(0, p(8)))
        tk.Label(v, text='Efeitos do waveod instalados como módulos. No pedal eles aparecem em cada categoria depois '
                 'dos efeitos originais. Um preset que usa um módulo removido guarda o bloco (AUSENTE, o som passa '
                 'direto) até ele ser instalado de novo.', font=F.small, bg=T['bg'], fg=T['muted'],
                 wraplength=p(1000), justify='left').pack(anchor='w', padx=p(20), pady=(0, p(10)))
        card = W.Card(v, app, T['card'], T['cardline'], radius=10, fit='both')
        card.pack(fill='both', expand=True, padx=p(20))
        cols = (('menu', 'Categoria', 90), ('name', 'Nome', 240), ('cat', 'Tipo', 140), ('params', 'Parâmetros', 110),
                ('mem', 'Memória', 170), ('size', 'Módulo', 90), ('id', 'Id', 120))
        tv = self.tree = ttk.Treeview(card.body, columns=[c[0] for c in cols], show='headings', style='Wv.Treeview',
                                      selectmode='extended')
        for col, text, width in cols:
            tv.heading(col, text=text, anchor='w')
            tv.column(col, width=p(width), anchor='w', stretch=col == 'name')
        tv.pack(fill='both', expand=True)
        self.usage = tk.Label(v, text='', font=F.small, bg=T['bg'], fg=T['muted'])
        self.usage.pack(anchor='w', padx=p(20), pady=(p(8), p(16)))
        self._fill()

    # ------------------------------------------------------------------ reading
    def shown(self):
        """the tab came into view: read the library the first time"""
        if self.lib is None and self.app.info:
            self.refresh()

    def _ok(self):
        if not self.app.info:
            self.app.status('Conecte o pedal primeiro')
            return False
        if self.app.info.get('protocol', 1) < 5:
            self.app.status('O firmware do pedal não tem biblioteca de efeitos: atualize o waveod', T['red'])
            return False
        return True

    def refresh(self):
        if self._ok():
            self.app.worker.run('Lendo os efeitos', lambda p, prog: (p.library(prog), p.library_info()), self._got)

    def _got(self, r):
        self.lib, info = r
        self.cap = info['capacity']
        self._fill()
        self.app.status('%s de efeito no pedal' % modules(len(self.lib)))

    def _fill(self):
        tv = self.tree
        tv.delete(*tv.get_children())
        if self.lib is None:
            self.usage.configure(text='Conecte o pedal para ver os efeitos instalados.')
            return
        for m in sorted(self.lib, key=lambda m: (m.menu, m.name)):
            mem = '%s estado%s' % (kb(m.state), ' + %s buffer' % kb(m.buffer) if m.buffer else '')
            tv.insert('', 'end', iid=str(m.id), values=(wvlib.MENU_TAGS[m.menu], m.name, m.category or '—',
                                                        len(m.params), mem, kb(len(m.elf)), '%08X' % m.id))
        used = len(self.lib.pack())
        self.usage.configure(text='%s · %s de %s na flash' % (modules(len(self.lib)), kb(used), kb(self.cap)) if
                             self.lib else 'Nenhum módulo instalado · %s livres na flash' % kb(self.cap))

    # ------------------------------------------------------------------ changes
    def install(self):
        if not self._ok():
            return
        paths = filedialog.askopenfilenames(parent=self.app.root, initialdir=self.app.session.get('fx_dir'),
                                            filetypes=[('Módulo de efeito do waveod', '*.elf')])
        if not paths:
            return
        self.app.session['fx_dir'] = os.path.dirname(paths[0])
        try:
            mods = [wvlib.describe(open(path, 'rb').read()) for path in paths]
        except (OSError, wvlib.LibraryError) as e:
            self.app.status('Módulo inválido: %s' % e, T['red'])
            return
        names = ', '.join('%s (%s)' % (m.name, wvlib.MENU_TAGS[m.menu]) for m in mods)
        if not W.ask(self.app, 'Instalar no pedal?', 'Grava %s na flash do pedal. Um módulo com o mesmo id é '
                     'substituído. O som some por instantes enquanto grava.' % names, 'Instalar', 'primary'):
            return

        def work(p, prog):
            lib = p.library()
            for m in mods:
                lib.add(m.elf, m.id, m.name)
            p.write_library(lib.pack(), lambda d, t: prog(d // 1024, t // 1024))
            return p.library(), p.library_info()
        self.app.worker.run('Gravando os efeitos (KB)', work, lambda r: self._done(r, '%s instalado' % names))

    def remove(self):
        if not self._ok():
            return
        ids = [int(i) for i in self.tree.selection()]
        if not ids:
            self.app.status('Selecione os módulos a remover')
            return
        names = ', '.join(self.lib.find(i).name for i in ids if self.lib.find(i))
        if not W.ask(self.app, 'Remover do pedal?', 'Tira %s do pedal. Os presets que usam esses efeitos guardam os '
                     'blocos (AUSENTE) até eles serem instalados de novo.' % names, 'Remover', 'danger'):
            return

        def work(p, prog):
            lib = p.library()
            for i in ids:
                lib.remove(i)
            p.write_library(lib.pack(), lambda d, t: prog(d // 1024, t // 1024))
            return p.library(), p.library_info()
        self.app.worker.run('Gravando os efeitos (KB)', work, lambda r: self._done(r, '%s removido' % names))

    def _done(self, r, what):
        self._got(r)
        self.app.status(what, T['green_soft'])


def kb(n):
    return '%d B' % n if n < 1024 else '%.1f KB' % (n / 1024) if n < 100 * 1024 else '%d KB' % (n // 1024)


def modules(n):
    return '1 módulo' if n == 1 else '%d módulos' % n
