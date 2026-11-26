"""Command line: the same operations as the app, for scripts and quick checks."""
import argparse
import sys

from waveod_studio import backup, flashmap, nam
from waveod_studio.protocol import FLASH_SIZE, LAT_ROUTE_NAMES, LAT_ROUTES, PLACES, Pedal
from waveod_studio.wvpreset import preset_label


def num(s):
    return int(s, 0)


def bar(done, total):
    print('\r%d / %d' % (done, total), end='', flush=True)


def main(argv):
    ap = argparse.ArgumentParser(prog='waveod_studio')
    ap.add_argument('--port', help='porta MIDI (nome ou número)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('info')
    sub.add_parser('list')
    b = sub.add_parser('backup'); b.add_argument('file')
    r = sub.add_parser('restore'); r.add_argument('file')
    d = sub.add_parser('dump'); d.add_argument('file')
    d.add_argument('--all', action='store_true'); d.add_argument('--start', type=num, default=0)
    d.add_argument('--length', type=num, default=0x20000)
    lt = sub.add_parser('latency', help='teste de latência do pedal (cabo da saída na entrada, ou PRESET sem cabo)')
    lt.add_argument('route', nargs='?', default='L', choices=sorted(LAT_ROUTES))
    sub.add_parser('clone', help='nomes dos 10 slots do Clone')
    n = sub.add_parser('nam', help='grava uma captura NAM (.nam A2-Lite) num slot do Clone, no lugar do que ele tem')
    n.add_argument('model'); n.add_argument('--slot', type=int, required=True, help='1..10')
    n.add_argument('--name', help='nome no pedal (16 letras; padrão: o do arquivo)')
    n.add_argument('--44k', dest='native', action='store_true', help='uma captura de 48 kHz roda a 44,1 kHz')
    a = ap.parse_args(argv)
    if a.cmd == 'nam':
        data, info = nam.capture(a.model, a.native)
        print('%s: %d pesos, %g Hz, roda a %s, %d bytes' % (a.model, info['weights'], info['sample_rate'],
                                                           info['runs_at'], info['bytes']))
    with Pedal(a.port) as p:
        if a.cmd == 'clone':
            for i, name in enumerate(p.clone_names()):
                print('%2d %s' % (i + 1, name))
        elif a.cmd == 'nam':
            p.write_clone(a.slot - 1, a.name or nam.default_name(a.model), data)
            print('slot %d gravado' % a.slot)
        elif a.cmd == 'info':
            for k, v in p.info().items():
                print('%-10s %s' % (k, v))
        elif a.cmd == 'list':
            for e in p.presets():
                print('%s %s %-17s %s' % (preset_label(e['index']), '*' if e['active'] else ' ', e['name'],
                                         PLACES.get(e['place'])))
        elif a.cmd == 'backup':
            m = backup.save(p, a.file, bar)
            print('\n%s: %d presets' % (a.file, len(m['presets'])))
        elif a.cmd == 'restore':
            n = backup.restore(p, a.file, bar)
            print('\n%d presets gravados' % n)
        elif a.cmd == 'dump':
            start, length = (0, FLASH_SIZE) if a.all else (a.start, a.length)
            data = p.dump(start, length, bar)
            open(a.file, 'wb').write(data)
            print('\n%s: %d bytes' % (a.file, len(data)))
            if len(data) == FLASH_SIZE:
                for name, lo, hi, n, last in flashmap.summary(data):
                    print('%-32s %06X-%06X %9d  %s' % (name, lo, hi, n, '%06X' % last if last is not None else '-'))
        elif a.cmd == 'latency':
            r = p.measure_latency(LAT_ROUTES[a.route], bar)
            print('\n%s: %s' % (LAT_ROUTE_NAMES[LAT_ROUTES[a.route]], r['status_text']))
            if r['runs']:
                print('%.2f amostras = %.3f ms (%d medidas de %.2f a %.2f, semelhança %.0f%%%s)' % (
                    r['mean'], r['ms'], r['runs'], r['min'], r['max'], r['match'] * 100,
                    ', %d dBFS' % r['level_db'] if a.route != 'PRESET' else ''))
                if a.route != 'PRESET':
                    print('buffers %d amostras + conversores %.2f amostras (%.3f ms)' % (
                        r['buffers'], r['mean'] - r['buffers'], (r['mean'] - r['buffers']) * 1000.0 / r['rate']))
            return 0 if r['status'] in (2, 3) else 1
    return 0
