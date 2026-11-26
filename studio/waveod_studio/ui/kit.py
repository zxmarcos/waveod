"""The visual kit of Matribox Studio (matribox/studio/fwstudio.py, MIT licence): theme, fonts and widgets drawn
on canvases (anti-aliased with aa.py), kept here so waveod Studio stands alone. Only the widgets; the firmware
composer stays in Matribox Studio.
"""
import ctypes, functools, glob, math, os, subprocess, sys
import tkinter as tk
from tkinter import ttk, font as tkfont

from waveod_studio.ui import aa

FONT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'fonts')   # IBM Plex (OFL)


def mix(c1, c2, t):
    """c1 moved towards c2 by t (t = 1 - opacity of c1 over c2)"""
    a = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(c2[i:i + 2], 16) for i in (1, 3, 5)]
    return '#%02x%02x%02x' % tuple(round(x + (y - x) * t) for x, y in zip(a, b))


@functools.lru_cache(maxsize=None)
def oklch(L, c, h):
    """OKLCH -> sRGB hex (the design's colour space)"""
    hr = math.radians(h)
    A, B = c * math.cos(hr), c * math.sin(hr)
    l_ = (L + 0.3963377774 * A + 0.2158037573 * B) ** 3
    m_ = (L - 0.1055613458 * A - 0.0638541728 * B) ** 3
    s_ = (L - 0.0894841775 * A - 1.2914855480 * B) ** 3
    rgb = (4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_,
           -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_,
           -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_)
    g = lambda x: 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    return '#%02x%02x%02x' % tuple(round(255 * min(1.0, max(0.0, g(v)))) for v in rgb)


T = dict(
    bg='#2a2e33', panel='#2f3338', card='#2c3035', raised='#363a40', hover='#3a3f45', seg_on='#4a4f57',
    input='#272b30', thead='#33373d', tile='#33373d', rowline='#383c42', foot='#26292d', track='#3d4248',
    chain='#272b30', line='#40454b', line2='#484d54', cardline='#42474d', outline='#555a62', focus='#636970',
    wire='#555a62', text='#e7e9ec', bright='#f4f5f7', soft='#c6cbd1', soft2='#c9ced5', muted='#a9afb6',
    faint='#8f959c', dim='#6b7178', accent='#f28b2c', accent_hi='#ff9c42', accent_ink='#1a0f05',
    green='#4fc883', green_soft='#7fd6a0', red='#f07a6a', red_hi='#e5604f', amber='#f2c46b', sel='#42474e',
    base_bar='#7f8fb0', disabled='#4a4f56',
)
T.update(accent_lo=mix(T['accent'], T['panel'], .87), teal=T['green'], teal_lo=mix(T['green'], T['panel'], .88),
         green_lo=mix(T['green'], T['panel'], .88), green_line=mix(T['green'], T['panel'], .62),
         red_lo=mix(T['red'], T['panel'], .88), red_line=mix(T['red'], T['panel'], .62),
         amber_lo=mix(T['amber'], T['panel'], .9))

MENU_HUE = {0: 250, 1: 290, 2: 335, 3: 55, 4: 25, 5: 80, 6: 105, 7: 140, 8: 175, 9: 220, 10: 265, 11: 315}
FIXED_LABELS = {5: 'Cab', 6: 'IR', 11: 'Clone', 12: 'Loop', 13: 'Send', 14: 'Return', 15: 'Vol'}


def menu_color(m, L=.76, c=.13):
    """the menu's hue (design); Loop / Send / Return / Volume are neutral"""
    h = MENU_HUE.get(m)
    return oklch(L, .015, 250) if h is None else oklch(L, c, h)


ICO = dict(search='', add='', remove='', check='', lock='', save='',
           open='', build='', reset='', warn='', info='', folder='',
           chip='', close='', filter='', edit='', library='', all='',
           more='', chev='', refresh='')


def load_fonts():
    """IBM Plex from studio/fonts for this process only (Windows); without it Segoe UI / Cascadia are used"""
    if sys.platform != 'win32':
        return
    for f in glob.glob(os.path.join(FONT_DIR, '*.ttf')):
        try:
            ctypes.windll.gdi32.AddFontResourceExW(f, 0x10, 0)      # FR_PRIVATE
        except Exception:
            pass


class Fonts:
    """sizes in pixels at 96 dpi, scaled (the design's CSS px)"""
    def __init__(self, root, S):
        fam = set(tkfont.families(root))
        pick = lambda *names: next((f for f in names if f in fam), None)
        sans = pick('IBM Plex Sans', 'Segoe UI Variable Text', 'Segoe UI', 'Helvetica') or 'TkDefaultFont'
        med = pick('IBM Plex Sans Medm', 'IBM Plex Sans Medium', 'Segoe UI Variable Text Semibold') or sans
        semi = pick('IBM Plex Sans SmBld', 'IBM Plex Sans SemiBold', 'Segoe UI Semibold') or sans
        mono = pick('IBM Plex Mono', 'Cascadia Mono', 'Consolas', 'Courier New') or 'TkFixedFont'
        mono_m = pick('IBM Plex Mono Medium', 'Cascadia Mono SemiBold') or mono
        self.icons = pick('Segoe Fluent Icons', 'Segoe MDL2 Assets')
        f = lambda family, px: tkfont.Font(root, family=family, size=-max(1, round(px * S)))
        self.body, self.medium, self.semi = f(sans, 13.5), f(med, 13.5), f(semi, 13.5)
        self.small, self.small_m = f(sans, 12.5), f(med, 12.5)
        self.xs = f(sans, 12)
        self.tiny = f(semi, 12)              # uppercase section labels
        self.th = f(semi, 11.5)              # table headings
        self.badge = f(semi, 10.5)
        self.h3, self.cap = f(semi, 15), f(semi, 14)
        self.h2, self.h_name, self.h_block, self.h1 = f(semi, 19), f(semi, 21), f(semi, 22), f(semi, 26)
        self.mono, self.mono_xs, self.mono_s = f(mono, 12.5), f(mono, 11.5), f(mono_m, 11)
        self.mono_m, self.mono_b = f(mono_m, 13), f(pick('IBM Plex Mono SemiBold') or mono_m, 15)
        self.icon = f(self.icons or sans, 13)
        self.icon_big = f(self.icons or sans, 17)
        self.icon_s = f(self.icons or sans, 10)


def rrect(c, x1, y1, x2, y2, r, fill=None, outline=None, width=1, bg=None, **kw):
    """anti-aliased rounded rectangle (aa.py); bg = colour under it (default: the canvas background)"""
    return aa.rrect(c, x1, y1, x2, y2, r, fill=fill, outline=outline, width=width, bg=bg, **kw)


def fit_text(font, text, width):
    """text cut with an ellipsis to fit width pixels"""
    if font.measure(text) <= width:
        return text
    lo, hi = 0, len(text)
    while lo < hi:
        m = (lo + hi + 1) // 2
        if font.measure(text[:m] + '…') <= width:
            lo = m
        else:
            hi = m - 1
    return text[:lo].rstrip() + '…' if lo else ''


def draw_switch(c, S, on, color, bg, x=0, y=0):
    """the design's 34 x 20 toggle: track + white knob"""
    p = lambda v: round(v * S)
    w, h = p(34), p(20)
    track = (color or T['accent']) if on else T['outline']
    r = (h - 2 * p(2)) / 2
    kx = x + w - p(2) - r if on else x + p(2) + r
    aa.draw(c, x, y, x + w, y + h, [('rrect', (x, y, x + w, y + h, h / 2), track),
                                    ('circle', (kx, y + h / 2, r), T['bright'])], bg)


def lw_of(S):
    return 1 if S < 1.75 else 2


# ------------------------------------------------------------------------------------------------ widgets
class Btn(tk.Canvas):
    """flat button on a canvas (anti-aliased corners). kind: primary, ghost (outlined), subtle, danger, teal,
    seg / seg_on (inside a segmented control)"""
    STYLES = {   # fill, border, ink, hover fill, hover ink
        'primary': ('accent', None, 'accent_ink', 'accent_hi', 'accent_ink'),
        'ghost': (None, 'outline', 'soft2', 'hover', 'text'),
        'subtle': (None, None, 'muted', 'hover', 'text'),
        'danger': (None, 'red_line', 'red', 'red_lo', 'red'),
        'teal': (None, 'green_line', 'green_soft', 'green_lo', 'green_soft'),
        'seg': (None, None, 'muted', None, 'text'),
        'seg_on': ('seg_on', None, 'text', 'seg_on', 'text'),
    }

    def __init__(self, parent, app, text='', icon=None, command=None, kind='ghost', height=34, padx=14,
                 width=None, tip=None, font=None, radius=None):
        self.app, self.text, self.icon, self.command, self.kind = app, text, icon, command, kind
        self.font, self.radius = font, radius
        self.enabled, self.hover, self.down = True, False, False
        self.pbg = parent.cget('bg')
        S = app.S
        self.h = round(height * S)
        self.padx = round(padx * S)
        super().__init__(parent, height=self.h, highlightthickness=0, bd=0, bg=self.pbg, cursor='hand2')
        self.fixed_w = round(width * S) if width else None
        self._size()
        for ev, fn in (('<Enter>', self._enter), ('<Leave>', self._leave), ('<ButtonPress-1>', self._press),
                       ('<ButtonRelease-1>', self._release)):
            self.bind(ev, fn)
        if tip:
            Tooltip(self, app, tip)

    def _font(self):
        return self.font or (self.app.F.semi if self.kind == 'primary' else self.app.F.medium)

    def _size(self):
        f, fi = self._font(), self.app.F.icon
        w = self.padx * 2
        if self.text:
            w += f.measure(self.text)
        if self.icon and self.app.F.icons:
            w += fi.measure(self.icon) + (round(8 * self.app.S) if self.text else 0)
        self.w = self.fixed_w or w
        self.configure(width=self.w)
        self.draw()

    def config_btn(self, text=None, kind=None, icon=None, enabled=None):
        if text is not None: self.text = text
        if kind is not None: self.kind = kind
        if icon is not None: self.icon = icon
        if enabled is not None:
            self.enabled = enabled
            self.configure(cursor='hand2' if enabled else 'arrow')
        self._size()

    def draw(self):
        self.delete('all')
        fill, border, ink, hfill, hink = self.STYLES[self.kind]
        c, b, ink = T.get(fill), T.get(border), T[ink]
        if self.hover and self.enabled:
            c, ink = T.get(hfill) or c, T[hink]
        if self.down and self.enabled and c:
            c = mix(c, '#000000', .1)
        if not self.enabled:
            ink = T['faint'] if self.kind != 'subtle' else T['dim']
            b = T['line2'] if b else None
            if self.kind == 'primary':
                c = T['disabled']
            elif self.kind == 'seg_on':
                c = T['raised']
        r = round((self.radius if self.radius is not None else 6 if self.kind.startswith('seg') else 8) * self.app.S)
        if c or b:
            rrect(self, 0, 0, self.w, self.h, r, fill=c, outline=b, width=lw_of(self.app.S), bg=self.pbg)
        parts = []
        if self.icon and self.app.F.icons:
            parts.append((self.icon, self.app.F.icon))
        if self.text:
            parts.append((self.text, self._font()))
        gap = round(8 * self.app.S) if len(parts) == 2 else 0
        total = sum(f.measure(t) for t, f in parts) + gap
        x0 = (self.w - total) / 2
        for t, f in parts:
            self.create_text(x0, self.h / 2, text=t, font=f, fill=ink, anchor='w')
            x0 += f.measure(t) + gap

    def _enter(self, e): self.hover = True; self.draw()

    def _leave(self, e): self.hover = self.down = False; self.draw()

    def _press(self, e): self.down = True; self.draw()

    def _release(self, e):
        was = self.down
        self.down = False; self.draw()
        if was and self.enabled and self.command and 0 <= e.x < self.w and 0 <= e.y < self.h:
            self.command()


class Tooltip:
    def __init__(self, w, app, text):
        self.w, self.app, self.text, self.tip, self.job = w, app, text, None, None
        w.bind('<Enter>', self._sched, add='+')
        w.bind('<Leave>', self._hide, add='+')
        w.bind('<ButtonPress>', self._hide, add='+')

    def _sched(self, e):
        self.job = self.w.after(550, self._show)

    def _show(self):
        self.job = None
        if self.tip or not self.text:
            return
        t = self.text() if callable(self.text) else self.text
        if not t:
            return
        self.tip = tk.Toplevel(self.w)
        self.tip.wm_overrideredirect(True)
        self.tip.configure(bg=T['line2'])
        tk.Label(self.tip, text=t, font=self.app.F.small, bg=T['raised'], fg=T['text'], justify='left',
                 padx=10, pady=6, wraplength=round(360 * self.app.S)).pack(padx=1, pady=1)
        x = self.w.winfo_pointerx() + 8 if callable(self.text) else self.w.winfo_rootx()
        y = self.w.winfo_rooty() + self.w.winfo_height() + 6
        self.tip.wm_geometry('+%d+%d' % (x, y))

    def _hide(self, e=None):
        if self.job:
            self.w.after_cancel(self.job); self.job = None
        if self.tip:
            self.tip.destroy(); self.tip = None


class Card(tk.Canvas):
    """rounded panel with a 1 px border; widgets go in .body, inset so they clear the corners.
    fit: 'both' (the body follows the card), 'x' (the card's height follows the body), 'none' (both sizes do)"""
    def __init__(self, parent, app, bg, border=None, radius=12, fit='x', top_only=False, under=None, inset=None):
        self.under = under or parent.cget('bg')
        super().__init__(parent, bg=self.under, highlightthickness=0, bd=0, width=1, height=1)
        self.app, self.fill, self.border, self.fit, self.top_only = app, bg, border, fit, top_only
        self.rp = round(radius * app.S)
        self.lw = lw_of(app.S)
        self.i = inset if inset is not None else max(self.lw, math.ceil(self.rp * .3))
        self.body = tk.Frame(self, bg=bg)
        self.win = self.create_window(self.i, self.i, window=self.body, anchor='nw')
        self.bind('<Configure>', self._draw)
        if fit != 'both':
            self.body.bind('<Configure>', self._on_body, add='+')
            self.after_idle(self._on_body)          # a 1 px canvas does not map the body: size it once

    def _on_body(self, e=None):
        i2 = 2 * self.i
        if not self.winfo_exists():
            return
        if self.fit == 'none':
            self.configure(width=self.body.winfo_reqwidth() + i2)
        self.configure(height=self.body.winfo_reqheight() + i2)

    def set_border(self, color=None, fill=None):
        self.border = color
        if fill and fill != self.fill:
            self.fill = fill
            self.body.configure(bg=fill)
        self._draw()

    def _draw(self, e=None):
        w, h = self.winfo_width(), self.winfo_height()
        if w < 2 or h < 2:
            return
        self.delete('bg')
        i = self.i
        if self.fit in ('x', 'both'):
            self.itemconfigure(self.win, width=max(1, w - 2 * i))
        if self.fit == 'both':
            self.itemconfigure(self.win, height=max(1, h - 2 * i))
        r = max(1, min(self.rp, w // 2, h // 2))
        f, b, lw = self.fill, self.border, self.lw
        bot = 0 if self.top_only else r
        rect = lambda x1, y1, x2, y2, c: self.create_rectangle(x1, y1, x2, y2, fill=c, outline='', tags='bg')
        rect(0, r, w, h - bot, f)
        rect(r, 0, w - r, h, f)
        if b:
            rect(r, 0, w - r, lw, b)
            if not self.top_only:
                rect(r, h - lw, w - r, h, b)
            rect(0, r, lw, h - bot, b)
            rect(w - lw, r, w, h - bot, b)
        R = 4 * r
        corners = [(0, 0, 0, 0), (w - r, 0, w - R, 0)]
        if not self.top_only:
            corners += [(0, h - r, 0, h - R), (w - r, h - r, w - R, h - R)]
        for x, y, rx, ry in corners:
            shapes = [('rrect', (rx, ry, rx + R, ry + R, r), b or f)]
            if b:
                shapes.append(('rrect', (rx + lw, ry + lw, rx + R - lw, ry + R - lw, max(0, r - lw)), f))
            aa.draw(self, x, y, x + r, y + r, shapes, self.under, tags='bg')
        self.tag_lower('bg')


def magnifier(parent, app, bg, size=14):
    p = lambda v: round(v * app.S)
    c = tk.Canvas(parent, width=p(size), height=p(size), bg=bg, highlightthickness=0)
    k, sw = size / 14, max(1.0, 1.5 * app.S) / 2
    aa.draw(c, 0, 0, p(size), p(size), [('circle', (p(6 * k), p(6 * k), p(4.5 * k) + sw), T['faint']),
                                        ('circle', (p(6 * k), p(6 * k), p(4.5 * k) - sw), bg),
                                        ('seg', (p(9.6 * k), p(9.6 * k), p(13 * k), p(13 * k), sw), T['faint'])], bg)
    return c


class Field(Card):
    """text input: rounded border (lighter while focused), optional magnifier, placeholder and a key hint"""
    def __init__(self, parent, app, var, placeholder='', search=True, height=36, bg=None, kbd=None, radius=8,
                 font=None, width=None, under=None):
        bg = bg or T['input']
        super().__init__(parent, app, bg, T['line2'], radius, fit='x', under=under)
        p = lambda v: round(v * app.S)
        F = app.F
        font = font or F.body
        b = self.body
        b.configure(height=p(height) - 2 * self.i)
        b.pack_propagate(False)
        if width:
            self.configure(width=p(width))
        x = p(12) - self.i
        if search:
            magnifier(b, app, bg, 14 if height >= 34 else 13).pack(side='left', padx=(x, 0))
            x = p(8)
        if kbd:
            tk.Label(b, text=kbd, font=F.mono_s, bg=bg, fg=T['faint'], padx=p(6), pady=0, highlightthickness=1,
                     highlightbackground=T['line2']).pack(side='right', padx=(p(8), p(12) - self.i))
        self.var = var
        self.entry = tk.Entry(b, textvariable=var, font=font, bg=bg, fg=T['text'], bd=0, relief='flat',
                              insertbackground=T['text'], highlightthickness=0, disabledbackground=bg,
                              selectbackground=T['seg_on'], selectforeground=T['text'])
        self.entry.pack(side='left', fill='x', expand=True, padx=(x, 0 if kbd else p(12) - self.i))
        self.ph = tk.Label(b, text=placeholder, font=font, bg=bg, fg=T['faint'], cursor='xterm', padx=0, pady=0)
        self.ph.bind('<Button-1>', lambda e: self.entry.focus_set())
        b.bind('<Button-1>', lambda e: self.entry.focus_set())
        var.trace_add('write', lambda *a: self._ph())
        self.entry.bind('<FocusIn>', lambda e: self.set_border(T['focus']), add='+')
        self.entry.bind('<FocusOut>', lambda e: self.set_border(T['line2']), add='+')
        self.after_idle(self._ph)

    def _ph(self):
        try:
            if self.var.get():
                self.ph.place_forget()
            else:
                self.ph.place(in_=self.entry, x=1, rely=.5, anchor='w')
        except tk.TclError:
            pass


class PopupMenu:
    """dark drop-down list: items [(text, callback[, enabled])] and None for a separator"""
    def __init__(self, app, items, x, y, width=None, current=None, above=None):
        self.app = app
        p = lambda v: round(v * app.S)
        F = app.F
        top = self.top = tk.Toplevel(app.root)
        top.withdraw()
        top.overrideredirect(True)
        top.configure(bg=T['line2'])
        fr = tk.Frame(top, bg=T['raised'])
        fr.pack(padx=1, pady=1, fill='both')
        tk.Frame(fr, bg=T['raised'], height=p(4)).pack(fill='x')
        for it in items:
            if it is None:
                tk.Frame(fr, bg=T['line2'], height=1).pack(fill='x', pady=p(4))
                continue
            text, cmd = it[0], it[1]
            enabled = it[2] if len(it) > 2 else True
            row = tk.Frame(fr, bg=T['raised'], cursor='hand2' if enabled else '')
            row.pack(fill='x', padx=p(4))
            mark = tk.Label(row, text='✓' if current is not None and text == current else '', width=2,
                            font=F.small, bg=T['raised'], fg=T['accent'])
            mark.pack(side='left', padx=(p(4), 0))
            lb = tk.Label(row, text=text, font=F.body, bg=T['raised'], fg=T['text'] if enabled else T['dim'],
                          anchor='w', padx=p(4), pady=p(6))
            lb.pack(side='left', fill='x', expand=True, padx=(0, p(14)))
            ws = (row, mark, lb)
            if enabled:
                for w in ws:
                    w.bind('<Enter>', lambda e, ws=ws: [x.configure(bg=T['seg_on']) for x in ws])
                    w.bind('<Leave>', lambda e, ws=ws: [x.configure(bg=T['raised']) for x in ws])
                    w.bind('<ButtonRelease-1>', lambda e, cmd=cmd: self._pick(cmd))
        tk.Frame(fr, bg=T['raised'], height=p(4)).pack(fill='x')
        top.update_idletasks()
        w = max(width or 0, top.winfo_reqwidth())
        h = top.winfo_reqheight()
        sw, sh = app.root.winfo_screenwidth(), app.root.winfo_screenheight()
        if y + h > sh - p(40) and above is not None:
            y = above - h
        x = min(x, sw - w - p(8))
        top.geometry('%dx%d+%d+%d' % (w, h, x, max(0, y)))
        top.deiconify()
        top.lift()
        # inside a modal dialog the dialog's grab sends the focus straight back to it (FocusOut closed the list
        # before any click): the list takes the grab while open, and gives it back on close
        self.prev_grab = app.root.grab_current()
        if self.prev_grab is not None:
            top.grab_set()
            top.bind('<ButtonPress>', self._outside)       # with the grab, clicks elsewhere arrive here
        top.bind('<FocusOut>', lambda e: self.close() if e.widget is top else None)
        top.bind('<Escape>', lambda e: self.close())
        top.after(10, top.focus_force)

    def _outside(self, e):
        t = self.top
        if not (0 <= e.x_root - t.winfo_rootx() < t.winfo_width() and 0 <= e.y_root - t.winfo_rooty() < t.winfo_height()):
            self.close()

    def _pick(self, cmd):
        self.close()
        if cmd:
            cmd()

    def close(self):
        try:
            self.top.destroy()
        except tk.TclError:
            return
        prev, self.prev_grab = self.prev_grab, None
        if prev is not None:
            try:
                prev.grab_set()
                prev.focus_set()
            except tk.TclError:
                pass


class Dropdown(Card):
    """read-only select: the value and a chevron; a click opens the list"""
    def __init__(self, parent, app, values, value, command, width=180, height=36, bg=None, font=None, under=None):
        bg = bg or T['input']
        super().__init__(parent, app, bg, T['line2'], 8 if height >= 30 else 7, fit='x', under=under)
        p = lambda v: round(v * app.S)
        self.values, self.value, self.command = list(values), value, command
        b = self.body
        b.configure(height=p(height) - 2 * self.i, cursor='hand2')
        b.pack_propagate(False)
        if width:
            self.configure(width=p(width))
        F = app.F
        self.lbl = tk.Label(b, text=value, font=font or F.body, bg=bg, fg=T['text'], anchor='w', cursor='hand2')
        self.lbl.pack(side='left', fill='x', expand=True, padx=(p(10) - self.i, 0))
        self.chev = tk.Label(b, text=ICO['chev'] if F.icons else '▾', font=F.icon_s, bg=bg, fg=T['faint'],
                             cursor='hand2')
        self.chev.pack(side='right', padx=(p(6), p(10) - self.i))
        for w in (self, b, self.lbl, self.chev):
            w.bind('<ButtonRelease-1>', lambda e: self.open())
            w.bind('<Enter>', lambda e: self.set_border(T['focus']))
            w.bind('<Leave>', lambda e: self.set_border(T['line2']))

    def open(self):
        y = self.winfo_rooty()
        PopupMenu(self.app, [(v, lambda v=v: self._pick(v)) for v in self.values], self.winfo_rootx(),
                  y + self.winfo_height() + round(4 * self.app.S), width=self.winfo_width(), current=self.value,
                  above=y - round(4 * self.app.S))

    def _pick(self, v):
        self.set(v)
        self.command(v)

    def set(self, v):
        self.value = v
        self.lbl.configure(text=v)

    def set_values(self, values):
        self.values = list(values)


class SegBar(tk.Canvas):
    """segmented control: items [(key, label)] or [(key, label, count, count colour)]; fill: equal widths"""
    def __init__(self, parent, app, items, active, command, height=34, radius=8, inner=5, font=None, pad=12,
                 fill=False, margin=3):
        self.pbg = parent.cget('bg')
        super().__init__(parent, height=round(height * app.S), bg=self.pbg, highlightthickness=0, width=1,
                         cursor='hand2')
        self.app, self.items, self.active, self.command = app, items, active, command
        self.r, self.ir, self.font, self.pad, self.fill, self.m = radius, inner, font or app.F.medium, pad, fill, margin
        self.hover = None
        self.spans = []
        self.bind('<Configure>', lambda e: self.draw())
        self.bind('<Motion>', self._motion)
        self.bind('<Leave>', lambda e: self._set_hover(None))
        self.bind('<ButtonRelease-1>', self._click)
        self._measure()

    def _part(self, it):
        label = it[1]
        count = it[2] if len(it) > 2 else None
        return label, (None if count is None else str(count))

    def _measure(self):
        p = lambda v: round(v * self.app.S)
        F = self.app.F
        self.widths = []
        for it in self.items:
            label, count = self._part(it)
            w = self.font.measure(label) + 2 * p(self.pad)
            if count is not None:
                w += p(6) + F.mono_s.measure(count)
            self.widths.append(w)
        if not self.fill:
            self.configure(width=sum(self.widths) + 2 * p(self.m) + p(2) * (len(self.items) - 1))
        self.draw()

    def set(self, items=None, active=None):
        if items is not None:
            self.items = items
        if active is not None:
            self.active = active
        self._measure()

    def draw(self):
        self.delete('all')
        S = self.app.S
        p = lambda v: round(v * S)
        w, h = self.winfo_width(), self.winfo_height()
        if w < 4:
            return
        F = self.app.F
        rrect(self, 0, 0, w, h, p(self.r), fill=T['raised'], outline=T['line2'], width=lw_of(S), bg=self.pbg)
        m, gap = p(self.m), p(2)
        n = len(self.items)
        widths = [(w - 2 * m - gap * (n - 1)) / n] * n if self.fill else self.widths
        x = m
        self.spans = []
        for it, sw in zip(self.items, widths):
            key = it[0]
            label, count = self._part(it)
            on = key == self.active
            if on:
                rrect(self, x, m, x + sw, h - m, p(self.ir), fill=T['seg_on'], bg=T['raised'])
            ink = T['text'] if on or self.hover == key else T['muted']
            tw = self.font.measure(label) + (p(6) + F.mono_s.measure(count) if count is not None else 0)
            tx = x + (sw - tw) / 2
            self.create_text(tx, h / 2, text=label, font=self.font, fill=ink, anchor='w')
            if count is not None:
                cc = it[3] if len(it) > 3 and it[3] else T['faint']
                self.create_text(tx + self.font.measure(label) + p(6), h / 2 + p(.5), text=count, font=F.mono_s,
                                 fill=cc, anchor='w')
            self.spans.append((x, x + sw, key))
            x += sw + gap

    def _key_at(self, x):
        for x1, x2, key in self.spans:
            if x1 <= x <= x2:
                return key
        return None

    def _set_hover(self, key):
        if key != self.hover:
            self.hover = key
            self.draw()

    def _motion(self, e):
        self._set_hover(self._key_at(e.x))

    def _click(self, e):
        key = self._key_at(e.x)
        if key is not None and 0 <= e.y <= self.winfo_height():
            self.command(key)


class Pill(tk.Canvas):
    """small tinted label (menu codes, states, origins)"""
    def __init__(self, parent, app, text, color, bg=None, icon=None, font=None, tint=.85, radius=5, height=22,
                 padx=8, ink=None):
        self.app = app
        f = font or app.F.badge
        S = app.S
        pad = round(padx * S)
        under = bg or parent.cget('bg')
        w = f.measure(text) + 2 * pad + (app.F.icon_s.measure(icon) + round(5 * S) if icon and app.F.icons else 0)
        h = round(height * S)
        super().__init__(parent, width=w, height=h, bg=under, highlightthickness=0, bd=0)
        lo = mix(color, under, tint)
        rrect(self, 0, 0, w, h, round(radius * S), fill=lo, bg=under)
        x = pad
        if icon and app.F.icons:
            self.create_text(x, h / 2, text=icon, font=app.F.icon_s, fill=ink or color, anchor='w')
            x += app.F.icon_s.measure(icon) + round(5 * S)
        self.create_text(x, h / 2, text=text, font=f, fill=ink or color, anchor='w')


class Switch(tk.Frame):
    """toggle switch + label bound to a BooleanVar"""
    def __init__(self, parent, app, text, var, command=None, color=None):
        bg = parent.cget('bg')
        super().__init__(parent, bg=bg, cursor='hand2')
        S = app.S
        self.var, self.command, self.S, self.color, self.bg = var, command, S, color, bg
        self.c = tk.Canvas(self, width=round(34 * S), height=round(20 * S), bg=bg, highlightthickness=0)
        self.c.pack(side='left')
        self.lbl = tk.Label(self, text=text, font=app.F.body, bg=bg, fg=T['text'])
        self.lbl.pack(side='left', padx=(round(10 * S), 0))
        for w in (self, self.c, self.lbl):
            w.bind('<Button-1>', self._click)
        self.draw()

    def _click(self, e):
        self.var.set(not self.var.get())
        self.draw()
        if self.command:
            self.command()

    def draw(self):
        self.c.delete('all')
        draw_switch(self.c, self.S, self.var.get(), self.color, self.bg)


class ToggleRow(Card):
    """the design's on/off row: label on the left, switch on the right, the whole row clickable"""
    def __init__(self, parent, app, text, on, command, color=None, enabled=True, height=40):
        super().__init__(parent, app, T['tile'], T['line2'], 8, fit='x')
        S = app.S
        p = lambda v: round(v * S)
        self.S, self.on, self.color, self.command, self.enabled = S, on, color, command, enabled
        b = self.body
        b.configure(height=p(height) - 2 * self.i, cursor='hand2' if enabled else '')
        b.pack_propagate(False)
        self.lbl = tk.Label(b, text=text, font=app.F.body, bg=T['tile'], fg=T['text'] if enabled else T['muted'])
        self.lbl.pack(side='left', padx=(p(12) - self.i, 0))
        self.sw = tk.Canvas(b, width=p(34), height=p(20), bg=T['tile'], highlightthickness=0)
        self.sw.pack(side='right', padx=(0, p(12) - self.i))
        for w in (self, b, self.lbl, self.sw):
            w.bind('<ButtonRelease-1>', lambda e: self.command() if self.enabled and self.command else None)
        self._sw()

    def _sw(self):
        self.sw.delete('all')
        draw_switch(self.sw, self.S, self.on, self.color if self.enabled else T['dim'], T['tile'])

    def set(self, text, on):
        self.on = on
        self.lbl.configure(text=text)
        self._sw()


class ChipFlow(tk.Canvas):
    """chips that wrap to the width: kind 'outline' (bordered) or 'fill' (mono, filled); tips: hover texts"""
    def __init__(self, parent, app, chips, kind='outline', tips=None):
        self.pbg = parent.cget('bg')
        super().__init__(parent, height=1, width=1, bg=self.pbg, highlightthickness=0)
        self.app, self.chips, self.kind, self.tips = app, chips, kind, tips
        self.boxes = []
        self.bind('<Configure>', lambda e: self._layout(e.width))
        if tips:
            Tooltip(self, app, self._tip)

    def _layout(self, w):
        self.delete('all')
        S = self.app.S
        p = lambda v: round(v * S)
        F = self.app.F
        f = F.small if self.kind == 'outline' else F.mono_xs
        pad, h, gap = (p(9), p(25), p(6)) if self.kind == 'outline' else (p(8), p(23), p(6))
        x = y = 0
        self.boxes = []
        for k, t in enumerate(self.chips):
            tw = f.measure(t) + 2 * pad
            if x and x + tw > w:
                x, y = 0, y + h + gap
            if self.kind == 'outline':
                rrect(self, x, y, x + tw, y + h, p(6), outline=T['line2'], width=lw_of(S), bg=self.pbg)
            else:
                rrect(self, x, y, x + tw, y + h, p(5), fill=T['hover'], bg=self.pbg)
            self.create_text(x + tw / 2, y + h / 2, text=t, font=f, fill=T['soft2'])
            self.boxes.append((x, y, x + tw, y + h, k))
            x += tw + gap
        nh = (y + h) if self.chips else 1
        if int(self.cget('height')) != nh:
            self.configure(height=nh)

    def _tip(self):
        x = self.winfo_pointerx() - self.winfo_rootx()
        y = self.winfo_pointery() - self.winfo_rooty()
        for x1, y1, x2, y2, k in self.boxes:
            if x1 <= x <= x2 and y1 <= y <= y2:
                return self.tips[k]
        return None


class RowButton(tk.Canvas):
    """list row (origins): rounded hover / active background, dot, text and a right-aligned stat"""
    def __init__(self, parent, app, text, right='', dot=None, active=False, command=None, height=36):
        self.pbg = parent.cget('bg')
        super().__init__(parent, height=round(height * app.S), width=1, bg=self.pbg, highlightthickness=0,
                         cursor='hand2')
        self.app, self.text, self.right, self.dot, self.active, self.command = app, text, right, dot, active, command
        self.hover = False
        self.bind('<Configure>', lambda e: self.draw())
        self.bind('<Enter>', lambda e: self._hov(True))
        self.bind('<Leave>', lambda e: self._hov(False))
        self.bind('<ButtonRelease-1>', lambda e: command() if command else None)

    def _hov(self, v):
        self.hover = v
        self.draw()

    def draw(self):
        self.delete('all')
        S, F = self.app.S, self.app.F
        p = lambda v: round(v * S)
        w, h = self.winfo_width(), self.winfo_height()
        if w < 10:
            return
        fill = T['line'] if self.active else T['hover'] if self.hover else None
        if fill:
            rrect(self, 0, 0, w, h, p(7), fill=fill, bg=self.pbg)
        bg = fill or self.pbg
        x = p(10)
        if self.dot:
            aa.oval(self, x + p(4), h / 2, p(4), fill=self.dot, bg=bg)
            x += p(22)
        rw = F.mono_xs.measure(self.right) + p(8) if self.right else 0
        self.create_text(x, h / 2, text=fit_text(F.body, self.text, w - x - p(10) - rw), font=F.body,
                         fill=T['text'], anchor='w')
        if self.right:
            self.create_text(w - p(10), h / 2, text=self.right, font=F.mono_xs, fill=T['muted'], anchor='e')


class ScrollFrame(tk.Frame):
    def __init__(self, parent, app, bg):
        super().__init__(parent, bg=bg)
        self.canvas = tk.Canvas(self, bg=bg, highlightthickness=0, bd=0)
        self.sb = ttk.Scrollbar(self, orient='vertical', command=self._sb_cmd, style='Dark.Vertical.TScrollbar')
        self.inner = tk.Frame(self.canvas, bg=bg)
        self.win = self.canvas.create_window(0, 0, window=self.inner, anchor='nw')
        self.canvas.configure(yscrollcommand=self.sb.set)
        self.canvas.pack(side='left', fill='both', expand=True)
        self.inner.bind('<Configure>', lambda e: self._update())
        self.canvas.bind('<Configure>', lambda e: (self.canvas.itemconfigure(self.win, width=e.width), self._update()))
        self.hold = None                # scroll offset (px) kept while inner is rebuilt, see keep()
        self.hold_job = None

    def keep(self, ms=400):
        """call before rebuilding inner's children: keeps the current scroll offset while the new widgets size
        themselves (inner passes through smaller heights, which would clamp the view to the top)"""
        self.hold = self.canvas.canvasy(0) if self.sb.winfo_manager() else 0
        if self.hold_job is not None:
            self.after_cancel(self.hold_job)
        self.hold_job = self.after(ms, self._release)

    def _release(self):
        self.hold = self.hold_job = None
        self._update()

    def _sb_cmd(self, *a):
        self.hold = None                # the user scrolls: stop restoring
        self.canvas.yview(*a)

    def _update(self):
        ih, ch = self.inner.winfo_reqheight(), self.canvas.winfo_height()
        self.canvas.configure(scrollregion=(0, 0, self.canvas.winfo_width(), ih))
        need = ih > ch + 1
        if self.hold:
            if ih > 0:
                self.canvas.yview_moveto(self.hold / ih)
            if not need:                # a passing size: keep the scrollbar until the hold ends
                return
        if need and not self.sb.winfo_manager():
            self.sb.pack(side='right', fill='y', before=self.canvas)
        elif not need and self.sb.winfo_manager():
            self.sb.pack_forget()
            self.canvas.yview_moveto(0)

    def _wheel(self, e):
        self.hold = None
        if self.inner.winfo_height() > self.canvas.winfo_height():
            self.canvas.yview_scroll(int(-e.delta / 120) * 3, 'units')


# ------------------------------------------------------------------------------------------------ effects table


class Dialog:
    def __init__(self, app, title, width=560):
        self.app = app
        S = app.S
        self.top = tk.Toplevel(app.root)
        self.top.withdraw()
        self.top.title(title)
        self.top.configure(bg=T['line2'])
        self.top.transient(app.root)
        self.top.resizable(False, False)
        self.frame = tk.Frame(self.top, bg=T['panel'])
        self.frame.pack(fill='both', expand=True, padx=1, pady=1)
        self.w = round(width * S)
        tk.Label(self.frame, text=title, font=app.F.h2, bg=T['panel'], fg=T['bright']).pack(
            anchor='w', padx=round(26 * S), pady=(round(22 * S), round(10 * S)))
        self.body = tk.Frame(self.frame, bg=T['panel'], width=self.w)
        self.body.pack(fill='both', expand=True, padx=round(26 * S))
        self.bar = tk.Frame(self.frame, bg=T['panel'])
        self.bar.pack(fill='x', padx=round(26 * S), pady=(round(18 * S), round(22 * S)))
        self.top.bind('<Escape>', lambda e: self.close())
        self.result = None

    def buttons(self, specs):
        for w in self.bar.winfo_children():
            w.destroy()
        self.btns = []
        for text, kind, cmd in reversed(specs):
            b = Btn(self.bar, self.app, text, None, cmd, kind, height=36, padx=16)
            b.pack(side='right', padx=(round(8 * self.app.S), 0))
            self.btns.append(b)

    def _path_row(self, var, browse, parent=None):
        S, F = self.app.S, self.app.F
        p = lambda v: round(v * S)
        row = tk.Frame(parent or self.body, bg=T['panel'])
        row.pack(fill='x')
        f = Field(row, self.app, var, '', search=False, height=36, font=F.mono)
        f.pack(side='left', fill='x', expand=True)
        Btn(row, self.app, '', ICO['folder'], browse, 'ghost', height=36, padx=12).pack(side='left', padx=(p(8), 0))

    def show(self, wait=False):
        self.top.update_idletasks()
        self.top.update_idletasks()                 # Cards size themselves at idle time
        r = self.app.root
        w = max(self.w + round(54 * self.app.S), self.top.winfo_reqwidth())
        h = self.top.winfo_reqheight()
        x = r.winfo_rootx() + (r.winfo_width() - w) // 2
        y = r.winfo_rooty() + (r.winfo_height() - h) // 3
        self.top.minsize(w, 1)                      # the height keeps following the content
        self.top.geometry('+%d+%d' % (x, max(0, y)))
        self.top.deiconify()
        dark_titlebar(self.top)
        self.top.grab_set()
        self.top.focus_set()
        if wait:
            self.app.root.wait_window(self.top)
        return self.result

    def close(self, result=None):
        self.result = result
        try:
            self.top.grab_release()
        except tk.TclError:
            pass
        self.top.destroy()


def ask(app, title, text, ok, kind='primary'):
    d = Dialog(app, title, 480)
    tk.Label(d.body, text=text, font=app.F.body, bg=T['panel'], fg=T['soft'], wraplength=round(430 * app.S),
             justify='left').pack(anchor='w')
    d.buttons([('Cancelar', 'ghost', d.close), (ok, kind, lambda: d.close(True))])
    return d.show(wait=True)


def reveal(path):
    try:
        if sys.platform == 'win32':
            subprocess.Popen(['explorer', '/select,', os.path.normpath(path)])
        else:
            subprocess.Popen(['xdg-open', os.path.dirname(path)])
    except Exception:
        pass


def dark_titlebar(win):
    if sys.platform != 'win32':
        return
    try:
        win.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(win.winfo_id())
        on = ctypes.c_int(1)
        for attr in (20, 19):
            if ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attr, ctypes.byref(on), 4) == 0:
                break
        bg = T['panel']
        cref = ctypes.c_int(int(bg[5:7], 16) << 16 | int(bg[3:5], 16) << 8 | int(bg[1:3], 16))
        ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, 35, ctypes.byref(cref), 4)
    except Exception:
        pass


