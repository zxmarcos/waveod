"""Flash dumps as the new interface makes them (webapp.dump_flash / pick_dump / cancel_task) against a fake pedal
that serves a made-up 8 MB image: no USB, no emulator. python tests/test_flash_web.py"""
import os
import struct
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from waveod_studio import flashmap as FM, i18n, protocol as P, webapp    # noqa: E402

i18n.set_language('pt-BR')                          # the messages checked below are the Portuguese ones

failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


def image():
    """a flash: a bootloader, an app image (its pairs at 0x20010) with an older firmware's bytes after it, an empty
    waveod library area, a Clone slot"""
    d = bytearray(b'\xff' * FM.FLASH)
    d[0:0x8000] = bytes(range(256)) * 0x80
    struct.pack_into('<III', d, 0x20010, 0x20030, 0x10000, 0)
    d[0x20030:0x30030] = b'\x11' * 0x10000
    d[0x100000:0x101000] = b'\x22' * 0x1000                     # left by another firmware
    d[0x40A000:0x40C000] = b'\x33' * 0x2000
    return bytes(d)


class FakePedal:
    def __init__(self, data, slow=0.0):
        self.data, self.slow, self.name = data, slow, 'fake'

    def dump(self, start, length, progress=None, cancel=None):   # protocol.Pedal.dump, chunk by chunk
        out = bytearray()
        for off in range(start, start + length, P.CHUNK):
            if cancel and cancel():
                raise P.Cancelled('cancelado')
            out += self.data[off:off + min(P.CHUNK, start + length - off)]
            if progress:
                progress(len(out), length)
            time.sleep(self.slow)
        return bytes(out)

    def close(self):
        pass


def main():
    webapp.Api._watch = lambda self: None                      # no port search
    api = webapp.Api()
    data = image()
    api._pedal = FakePedal(data)
    tmp = tempfile.mkdtemp(prefix='wvdump_')
    where = {}
    api._ask_path = lambda save, types, kind, name='', many=False: where.get('path')

    where['path'] = os.path.join(tmp, 'boot.bin')
    r = api.dump_flash('boot')
    check(r['size'] == FM.BOOTLOADER and open(where['path'], 'rb').read() == data[:FM.BOOTLOADER] and r['summary'] is None,
          'bootloader: %s, %d KB, no summary' % (r['file'], r['size'] // 1024))

    where['path'] = os.path.join(tmp, 'full.bin')
    r = api.dump_flash('all')
    s = {a['name']: a for a in r['summary']}
    check(open(where['path'], 'rb').read() == data and len(r['summary']) == len(FM.AREAS), 'whole flash written, %d areas'
          % len(r['summary']))
    app = s['App (seção 0)']
    check('030030' in app['detail'] and '4 KB de outro firmware' in app['detail'], 'app: %s' % app['detail'])
    check(s['Biblioteca do waveod']['used'] == 0 and not s['Biblioteca do waveod']['detail'], 'an empty waveod library area')
    check(s['Capturas Clone']['used'] == 0x2000 and s['Capturas Clone']['last'] == 0x40BFFF, 'Clone area: 8 KB up to 40BFFF')
    check(all(a['note'] for a in r['summary']), 'every area says what it is for')

    where['path'] = os.path.join(tmp, 'full.bin')
    check(api.pick_dump()['summary'] == r['summary'], 'a saved dump opened: the same summary')
    where['path'] = os.path.join(tmp, 'boot.bin')
    try:
        api.pick_dump()
        check(False, 'a dump that is not 8 MB is refused')
    except RuntimeError as e:
        check('8 MB' in str(e), 'a dump that is not 8 MB is refused: %s' % e)
    where['path'] = None
    check(api.dump_flash('all') is None, 'no file named: nothing read')

    # cancelled half way: no file, no progress left
    api._pedal = FakePedal(data, slow=0.002)
    where['path'] = os.path.join(tmp, 'cancel.bin')
    err, seen = [], []
    t = threading.Thread(target=lambda: err.append(_try(api)))
    t.start()
    while not (api.progress() or {}).get('done'):
        time.sleep(0.005)
    seen.append(api.progress())
    api.cancel_task()
    t.join(20)
    p = seen[0]
    check(p['unit'] == 'KB' and p['total'] == 8192 and p['task'] == 'dump' and p['cancel'],
          'progress: %s, %d of %d KB, cancellable' % (p['label'], p['done'], p['total']))
    check(err and 'cancelada' in str(err[0]) and not os.path.exists(where['path']), 'cancelled: %s; no file' % err)
    check(api.progress() is None, 'no progress left behind')
    print('%d failed' % failed)
    return 1 if failed else 0


def _try(api):
    try:
        api.dump_flash('all')
        return 'not cancelled'
    except RuntimeError as e:
        return e


if __name__ == '__main__':
    sys.exit(main())
