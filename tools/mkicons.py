"""Generate src/ui/images/icons.c / icons.h: the design's line icons as LVGL A8 images.

    python tools/mkicons.py            (no extra packages)

The icons are the SVG paths of the claude.ai/design project ("Fluxo de Uso 4.3 v2"), drawn in a 24 x 24 box
with round caps and joins. Each is rasterized here at the sizes the screens use: the coverage of every pixel
by the stroke (the distance from its centre to the path, against half the stroke width), stored as an alpha
map. LVGL draws an A8 image in the image's recolor colour, so one image serves every category colour.
"""
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_C = os.path.join(ROOT, 'src', 'ui', 'images', 'icons.c')
OUT_H = os.path.join(ROOT, 'src', 'ui', 'images', 'icons.h')

# effect categories (design names), in the order of enum icon
CATEGORIES = [
    ('DYN', 'M12 3v6M9 6l3 3 3-3M12 21v-6M9 18l3-3 3 3M4 12h16'),
    ('WAH', 'M5 20l3-15h8l3 15zM9.5 10h5'),
    ('DRV', 'M3 12h2l3-6h3v12h3l3-6h4'),
    ('AMP', 'M3 6h18v12H3zM3 10h18M7 14v.5M11 14v.5M15 14v.5'),
    ('CAB', 'M4 4h16v16H4zM8 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0'),
    ('EQ', 'M6 4v16M12 4v16M18 4v16M4 9h4M10 15h4M16 7h4'),
    ('FLT', 'M3 8h7c3 0 4 2 5 5s2 5 6 5'),
    ('MOD', 'M2 12c2-5 4-5 6 0s4 5 6 0 4-5 6 0'),
    ('PIT', 'M8 20V5M4 9l4-4 4 4M16 4v15M12 15l4 4 4-4'),
    ('DLY', 'M3 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0M14 9a3 3 0 0 1 0 6M18 10a2 2 0 0 1 0 4'),
    ('RVB', 'M4 20A16 16 0 0 1 20 4M4 20a10 10 0 0 1 10-10M4 20a4 4 0 0 1 4-4'),
    ('VOL', 'M4 9h4l5-4v14l-5-4H4zM17 9c1.5 1.5 1.5 4.5 0 6'),
    ('SR', 'M4 8h14l-3-3M20 16H6l3 3'),
    ('PLUS', 'M12 7v10M7 12h10'),
]
CATEGORY_SIZES = (15, 20)
CATEGORY_STROKE = 2.2

# other icons: name, path, size, stroke
SINGLE = [
    ('power', 'M12 4v8M7.5 7.5a6 6 0 1 0 9 0', 14, 2.6),
    ('trash', 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6', 15, 2.2),
    ('next_12', 'M9 5l7 7-7 7', 12, 2.6),
    ('up_12', 'M5 15l7-7 7 7', 12, 2.6),
    ('prev_16', 'M15 5l-7 7 7 7', 16, 2.6),
    ('next_16', 'M9 5l7 7-7 7', 16, 2.6),
    # top bar: the tabs (the home screen's house is ours, the design has no PRESET tab), save, back
    ('tab_preset', 'M4 11l8-7 8 7M6 9.5V20h12V9.5M10 20v-5h4v5', 15, 2.2),
    ('tab_chain', 'M2 12h3M19 12h3M5 8h5v8H5zM14 8h5v8h-5zM10 12h4', 15, 2.2),
    ('tab_stomp', 'M6 3h12v18H6zM9 16a3 3 0 1 0 6 0a3 3 0 1 0-6 0M9 7h6', 15, 2.2),
    ('tab_scene', 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5', 15, 2.2),
    ('tab_tuner', 'M8 3v7a4 4 0 0 0 8 0V3M12 14v7', 15, 2.2),
    ('save', 'M5 4h11l3 3v13H5zM8 4v5h7V4M8 20v-6h8v6', 15, 2.2),
    ('back', 'M15 5l-7 7 7 7', 14, 2.5),
    # ours: a NAM capture that runs through the 44.1 / 48 kHz resampler (a wave over a two-way arrow)
    ('resample', 'M3 8.5c2.25-4.5 4.5-4.5 6.75 0s4.5 4.5 6.75 0 3.75-4.5 4.5-3M5 17h14M8 14l-3 3 3 3M16 14l3 3-3 3',
     14, 2.2),
    # ours: the GLOBAL screen's cards
    ('g_io', 'M3 8h12M11 4l4 4-4 4M21 16H9M13 12l-4 4 4 4', 22, 2.0),
    ('g_tuner', 'M8 3v7a4 4 0 0 0 8 0V3M12 14v7', 22, 2.0),
    ('g_usb', 'M12 3v13M9.5 5.5L12 3l2.5 2.5M12 13l-5-3V7.5M12 11.5l5-3V7M5.5 5.5h3v2.5h-3zM17 4.5a1.5 1.5 0 1 1 0 3'
     'a1.5 1.5 0 1 1 0-3M12 16a2.5 2.5 0 1 1 0 5a2.5 2.5 0 1 1 0-5', 22, 1.9),
    ('g_exp', 'M3 20h18v-3H3zM5 15L19 8.5M5 17v-2M17.5 11.5l1.5 2.5', 22, 2.0),
    ('g_eq', 'M6 4v16M12 4v16M18 4v16M4 9h4M10 15h4M16 7h4', 22, 2.0),
    ('g_fs', 'M4 20h16v-6H4zM9 14v-3h6v3M12 11V7M8.5 5h7', 22, 2.0),
    ('g_midi', 'M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M7.5 12h.5M16 12h.5M9 8.5h.5M14.5 8.5h.5M12 7v.5M10 17h4', 22, 2.0),
    ('g_presets', 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5', 22, 2.0),
    ('g_controls', 'M4 6h9M17 6h3M15 4v4M4 12h3M11 12h9M9 10v4M4 18h11M19 18h1M17 16v4', 22, 2.0),
    ('g_display', 'M3 5h18v12H3zM8 21h8M12 17v4', 22, 2.0),
    ('g_latency', 'M12 9v4.5l2.5 1.5M9 2.5h6M12 2.5V5M12 5a8 8 0 1 0 0 16a8 8 0 1 0 0-16', 22, 2.0),
    ('g_system', 'M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M12 11v6M12 7.5v.5', 22, 2.0),
    # ours: the top bar (menu, the footswitch mode, save, back) and the mode chooser
    ('menu', 'M4 6.5h16M4 12h16M4 17.5h16', 20, 2.4),
    ('mode_preset', 'M4 11l8-7 8 7M6 9.5V20h12V9.5M10 20v-5h4v5', 20, 2.2),
    ('mode_stomp', 'M6 3h12v18H6zM9 16a3 3 0 1 0 6 0a3 3 0 1 0-6 0M9 7h6', 20, 2.2),
    ('mode_scene', 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5', 20, 2.2),
    ('mode_preset_28', 'M4 11l8-7 8 7M6 9.5V20h12V9.5M10 20v-5h4v5', 28, 2.0),
    ('mode_stomp_28', 'M6 3h12v18H6zM9 16a3 3 0 1 0 6 0a3 3 0 1 0-6 0M9 7h6', 28, 2.0),
    ('mode_scene_28', 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5', 28, 2.0),
    ('save_20', 'M5 4h11l3 3v13H5zM8 4v5h7V4M8 20v-6h8v6', 20, 2.2),
    ('back_18', 'M15 5l-7 7 7 7', 18, 2.6),
    # ours: the menu's cards
    ('m_chain', 'M2 12h3M19 12h3M5 8h5v8H5zM14 8h5v8h-5zM10 12h4', 22, 2.0),
    ('m_looper', 'M17 2l3 3-3 3M4 11V9a4 4 0 0 1 4-4h12M7 22l-3-3 3-3M20 13v2a4 4 0 0 1-4 4H4', 22, 2.0),
    ('m_drum', 'M4 9a8 3 0 1 0 16 0a8 3 0 1 0-16 0M4 9v7a8 3 0 0 0 16 0V9M8 2l4 7M16 2l-4 7', 22, 2.0),
    ('m_global', 'M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1'
     'M16.6 16.6l2.1 2.1M5.3 18.7l2.1-2.1M16.6 7.4l2.1-2.1', 22, 2.0),
    # ours: play / stop (drum machine, looper)
    ('play_28', 'M8 4.5v15l12-7.5z', 28, 2.2),
    ('stop_28', 'M6 6h12v12H6z', 28, 2.2),
]

NUM = r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?'


def tokens(d):
    for m in re.finditer(r'([MmLlHhVvCcSsQqTtAaZz])|(' + NUM + ')', d):
        yield m.group(1) or float(m.group(2))


def arc_points(x0, y0, rx, ry, phi, large, sweep, x1, y1):
    """SVG endpoint arc -> points (F.6.5 of the SVG spec)"""
    if rx == 0 or ry == 0:
        return [(x1, y1)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x0 + x1) / 2
    cy = sp * cxp + cp * cyp + (y0 + y1) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(4, int(abs(dt) / (math.pi / 24)))
    out = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return out


def bezier(p0, p1, p2, p3, n=16):
    out = []
    for i in range(1, n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def polylines(d):
    """path data -> list of polylines (lists of points)"""
    toks = list(tokens(d))
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    last_c = None                       # last cubic control point (for S)
    lines, cur = [], []

    def num():
        nonlocal i
        v = toks[i]
        i += 1
        return v

    while i < len(toks):
        if isinstance(toks[i], str):
            cmd = toks[i]
            i += 1
            if cmd in 'Zz':
                if cur:
                    cur.append((sx, sy))
                x, y = sx, sy
                last_c = None
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            if len(cur) > 0:
                lines.append(cur)
            cur = [(nx, ny)]
            x, y, sx, sy = nx, ny, nx, ny
            cmd = 'l' if rel else 'L'       # further pairs are lines
            last_c = None
        elif c == 'L':
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            cur.append((nx, ny))
            x, y = nx, ny
            last_c = None
        elif c == 'H':
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
            last_c = None
        elif c == 'V':
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
            last_c = None
        elif c in 'CS':
            if c == 'C':
                x1, y1 = num(), num()
                if rel:
                    x1, y1 = x + x1, y + y1
            else:
                x1, y1 = (2 * x - last_c[0], 2 * y - last_c[1]) if last_c else (x, y)
            x2, y2, nx, ny = num(), num(), num(), num()
            if rel:
                x2, y2, nx, ny = x + x2, y + y2, x + nx, y + ny
            cur.extend(bezier((x, y), (x1, y1), (x2, y2), (nx, ny)))
            last_c = (x2, y2)
            x, y = nx, ny
        elif c == 'A':
            rx, ry, phi, large, sweep, nx, ny = num(), num(), num(), num(), num(), num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            cur.extend(arc_points(x, y, rx, ry, phi, int(large), int(sweep), nx, ny))
            x, y = nx, ny
            last_c = None
        else:
            raise ValueError('path command ' + cmd)
    if cur:
        lines.append(cur)
    return lines


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    qx, qy = ax + t * dx - px, ay + t * dy - py
    return math.sqrt(qx * qx + qy * qy)


def rasterize(d, size, stroke):
    k = size / 24.0
    segs = []
    for line in polylines(d):
        pts = [(px * k, py * k) for px, py in line]
        if len(pts) == 1:
            pts.append(pts[0])
        segs.extend(zip(pts, pts[1:]))
    half = stroke * k / 2
    out = bytearray(size * size)
    for py in range(size):
        for px in range(size):
            cx, cy = px + 0.5, py + 0.5
            dmin = min(seg_dist(cx, cy, a[0], a[1], b[0], b[1]) for a, b in segs)
            a = max(0.0, min(1.0, half - dmin + 0.5))
            out[py * size + px] = int(round(a * 255))
    return bytes(out)


def emit(c, name, data, size):
    c.append('static LV_ATTRIBUTE_LARGE_CONST const uint8_t %s_data[] = {' % name)
    for r in range(0, len(data), 24):
        c.append('    ' + ', '.join('0x%02x' % b for b in data[r:r + 24]) + ',')
    c.append('};')
    c.append('const lv_image_dsc_t %s = {' % name)
    c.append('    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = %d, .h = %d, .stride = %d },'
             % (size, size, size))
    c.append('    .data_size = sizeof %s_data,' % name)
    c.append('    .data = %s_data,' % name)
    c.append('};')
    c.append('')


def main():
    c = ['/* Generated by tools/mkicons.py; do not edit. The design\'s line icons as A8 alpha maps. */',
         '#include "ui/images/icons.h"', '']
    h = ['/* Generated by tools/mkicons.py; do not edit. */', '#pragma once', '', '#include "lvgl.h"', '',
         '/* effect categories of the design (ui/icons.c maps the library menus onto them) */',
         'enum icon { ' + ', '.join('ICON_' + n for n, _ in CATEGORIES) + ', ICON_COUNT };', '']
    for size in CATEGORY_SIZES:
        names = []
        for n, d in CATEGORIES:
            name = 'icon_%s_%d' % (n.lower(), size)
            emit(c, name, rasterize(d, size, CATEGORY_STROKE), size)
            names.append(name)
        c.append('const lv_image_dsc_t *const icons_%d[ICON_COUNT] = { %s };' %
                 (size, ', '.join('&' + n for n in names)))
        c.append('')
        h.append('extern const lv_image_dsc_t *const icons_%d[ICON_COUNT];      /* %d x %d */' % (size, size, size))
    h.append('')
    for n, d, size, stroke in SINGLE:
        name = 'icon_' + n
        emit(c, name, rasterize(d, size, stroke), size)
        h.append('extern const lv_image_dsc_t %s;%s/* %d x %d */' % (name, ' ' * max(1, 22 - len(name)), size, size))
    with open(OUT_C, 'w', newline='\n') as f:
        f.write('\n'.join(c) + '\n')
    with open(OUT_H, 'w', newline='\n') as f:
        f.write('\n'.join(h) + '\n')
    print('wrote', OUT_C, OUT_H)


if __name__ == '__main__':
    main()
