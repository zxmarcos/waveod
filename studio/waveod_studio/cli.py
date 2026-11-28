"""Command line: the same operations as the app, for scripts and quick checks."""
import argparse
import sys

from waveod_studio import backup, flashmap, nam
from waveod_studio.i18n import tr
from waveod_studio.protocol import FLASH_SIZE, LAT_ROUTE_NAMES, LAT_ROUTES, PLACES, Pedal
from waveod_studio.wvpreset import preset_label


def num(s):
    return int(s, 0)


def bar(done, total):
    print('\r%d / %d' % (done, total), end='', flush=True)


def main(argv):
    ap = argparse.ArgumentParser(prog='waveod_studio')
    ap.add_argument('--port', help=tr('py.cli_port'))
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('info')
    sub.add_parser('list')
    b = sub.add_parser('backup'); b.add_argument('file')
    r = sub.add_parser('restore'); r.add_argument('file')
    d = sub.add_parser('dump'); d.add_argument('file')
    d.add_argument('--all', action='store_true'); d.add_argument('--start', type=num, default=0)
    d.add_argument('--length', type=num, default=0x20000)
    lt = sub.add_parser('latency', help=tr('py.cli_latency'))
    lt.add_argument('route', nargs='?', default='L', choices=sorted(LAT_ROUTES))
    sub.add_parser('clone', help=tr('py.cli_clone'))
    n = sub.add_parser('nam', help=tr('py.cli_nam'))
    n.add_argument('model'); n.add_argument('--slot', type=int, required=True, help='1..10')
    n.add_argument('--name', help=tr('py.cli_nam_name'))
    n.add_argument('--44k', dest='native', action='store_true', help=tr('py.cli_44k'))
    a = ap.parse_args(argv)
    if a.cmd == 'nam':
        data, info = nam.capture(a.model, a.native)
        print(tr('py.cli_nam_info', file=a.model, weights=info['weights'], rate='%g' % info['sample_rate'],
                 runs=info['runs_at'], bytes=info['bytes']))
    with Pedal(a.port) as p:
        if a.cmd == 'clone':
            for s in p.clones():
                print('%2d %s' % (s['slot'] + 1, s['name']))
        elif a.cmd == 'nam':
            p.write_clone(a.slot - 1, a.name or nam.default_name(a.model), data)
            print(tr('py.cli_slot_written', n=a.slot))
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
            print('\n' + tr('py.cli_restored', n=n))
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
                print(tr('py.cli_lat_result', mean='%.2f' % r['mean'], ms='%.3f' % r['ms'], runs=r['runs'],
                         min='%.2f' % r['min'], max='%.2f' % r['max'], match='%.0f' % (r['match'] * 100),
                         level=', %d dBFS' % r['level_db'] if a.route != 'PRESET' else ''))
                if a.route != 'PRESET':
                    print(tr('py.cli_lat_parts', buffers=r['buffers'], conv='%.2f' % (r['mean'] - r['buffers']),
                             ms='%.3f' % ((r['mean'] - r['buffers']) * 1000.0 / r['rate'])))
            return 0 if r['status'] in (2, 3) else 1
    return 0
