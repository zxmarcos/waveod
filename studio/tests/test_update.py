"""The new interface's firmware update (webapp.Api.update_firmware) against a simulated bootloader: the pedal in
its app is asked to reboot into the bootloader, the four sections arrive intact, a cancel stops it, and the
connection looks for the pedal again afterwards. No pedal needed.

    python tests/test_update.py [FW.bin]
"""
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))

from waveod_studio import i18n, webapp              # noqa: E402
from waveod_studio.usb import qmeupdate as U        # noqa: E402

i18n.set_language('pt-BR')                          # the messages checked below are the Portuguese ones

FW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'out', 'QME-200_waveod_grid.bin')
failed = 0


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


class Boot:
    """the bootloader's side of the editor's sequence (after matribox-studio's tests/test_qmeupdate.py FakeBoot)"""
    PKT = 0xB4

    def __init__(self):
        self.sections, self.loc, self.buf, self.size, self.expect, self.jumped, self.armed = {}, None, None, 0, 0, False, False

    def handle(self, m):
        cmd = m[3]
        if cmd == 0x01:
            r = bytearray(0x27)
            r[0:4] = b'\xf0\x7e\x48\x01'
            r[4:11] = b'QME-200'
            r[0x14:0x1A] = b'V1.0.0'
            r[0x24], r[0x25] = self.PKT >> 4, self.PKT & 15
            r[0x26] = 0xF7
            return [bytes(r)]
        if cmd == 0x0B:
            self.armed = True
            return []
        if cmd == 0x7F:
            self.loc = m[4]
            return [U.MSG_ACK]
        if cmd == 0x02:
            nib = m[4:-1]
            self.size = int(''.join('%x' % x for x in nib[0:8]), 16)
            self.buf, self.expect = bytearray(), 0
            return [U.MSG_ACK]
        if cmd == 0x04:
            chunk = m[5:-3]
            self.buf += bytes(chunk[i] << 4 | chunk[i + 1] for i in range(0, len(chunk), 2))
            self.expect += 1
            return [U.MSG_ACK]
        if cmd == 0x05:
            self.sections[self.loc] = bytes(self.buf[:self.size])
            return [U.MSG_ACK]
        if cmd == 0x07:
            return [bytes([0xF0, 0x7E, 0x48, 0x07]) + U.nibbles(bytes(range(16))) + b'\xf7']
        if cmd == 0x09:
            self.jumped = True
            return []
        return []


class World:
    mode = 'app'                                    # 'app': waveod running; 'boot': the bootloader
    boot = None
    jumps = 0
    opened = []


class AppPort:
    """the pedal running waveod: silent to the bootloader's query, a jump request reboots it"""
    name = 'Matribox II Pro'

    def __init__(self):
        self.rx = []

    def send(self, m):
        # the editor's packets carry the command nibble by nibble: 0x0500000E is a jump into the bootloader
        if World.mode == 'app' and bytes(m[:5]) == b'\xf0\x21\x25\x4d\x50' and \
                U.CMD_SYS_JUMP.to_bytes(4, 'little') in self._unnibble(m):
            World.jumps += 1
            World.mode = 'boot'

    @staticmethod
    def _unnibble(m):
        body = bytes(m[13:-1])
        return bytes(body[i] << 4 | body[i + 1] & 15 for i in range(0, len(body) - 1, 2))

    def receive(self):
        return []

    def close(self):
        pass


class BootPort:
    name = 'Matribox II Pro'

    def __init__(self):
        self.rx = []

    def send(self, m):
        m = bytes(m)
        if m[:3] == b'\xf0\x7e\x48':
            self.rx += World.boot.handle(m)

    def receive(self):
        out, self.rx = self.rx, []
        return out

    def close(self):
        pass


def fake_port(want=None):
    p = AppPort() if World.mode == 'app' else BootPort()
    World.opened.append(type(p).__name__)
    return p


webapp.htmidi.Port = fake_port
webapp.htmidi.list_ports = lambda: ([], [])         # the watcher sees no pedal: it never takes the port
U.VERSION_SLEEP = 0.01


def main():
    img = U.Image.load(FW)
    api = webapp.Api()
    info = api.firmware_info(FW)
    check(info['version'] == img.version and len(info['sections']) == 4,
          'firmware_info: %s %s, %s' % (info['file'], info['version'], ', '.join(
              '%s %d KB' % (s['name'], s['size'] // 1024) for s in info['sections'])))
    try:
        api.firmware_info(__file__)
        check(False, 'a file that is not a firmware is refused')
    except RuntimeError as e:
        check('não serve' in str(e), 'a file that is not a firmware is refused: %s' % e)

    # the pedal in its app: asked to reboot into the bootloader, then flashed
    World.boot, World.mode = Boot(), 'app'
    seen, phases = [], set()
    out = []
    t = threading.Thread(target=lambda: out.append(api.update_firmware(FW)))
    t0 = time.time()
    t.start()
    while t.is_alive():
        p, s = api.progress(), api.status()
        if p:
            seen.append(p)
        phases.add(s['phase'])
        time.sleep(0.02)
    r = out[0] if out else None
    check(r and World.jumps == 1 and World.opened[:2] == ['AppPort', 'BootPort'],
          'app first: one jump request, then the bootloader (%s)' % World.opened[:3])
    intact = all(World.boot.sections.get(loc) == img.sections[loc] for loc in U.ORDER)
    check(intact and World.boot.jumped, 'the four sections arrived intact, the pedal told to start (%.1f s)' % (time.time() - t0))
    labels = {p['label'] for p in seen}
    check(any(p['unit'] == 'pacotes' and p['total'] > 0 for p in seen) and any('Gravando: MCU' in x for x in labels),
          'progress in packets, by section: %s' % sorted(labels)[:4])
    check('updating' in phases and api.status()['phase'] == 'searching' and 'firmware novo' in api.status()['message'],
          'connection: updating while it runs, then looking for the pedal again (%s)' % api.status()['message'])
    check(os.path.exists(r['log']) and os.path.getsize(r['log']) > 0, 'a log of the update: %s' % r['log'])

    # in the bootloader already (a failed update, the recovery combo): no jump; a cancel stops it
    World.boot, World.mode, World.jumps = Boot(), 'boot', 0
    err = []

    def go():
        try:
            api.update_firmware(FW)
        except RuntimeError as e:
            err.append(str(e))
    t = threading.Thread(target=go)
    t.start()
    while not (api.progress() or {}).get('total'):
        time.sleep(0.005)
    api.cancel_update()
    t.join(10)
    check(World.jumps == 0 and err and 'cancelado' in err[0], 'from the bootloader, cancelled: %s' % (err or '?'))
    check('modo de atualização' in api.status()['message'], 'after a failure: %s' % api.status()['message'])
    check(api.progress() is None, 'no progress left behind')
    try:
        webapp.Api(emu='x').update_firmware(FW)
        check(False, 'the emulator refuses')
    except RuntimeError as e:
        check('emulador' in str(e), 'the emulator refuses: %s' % e)
    api._close()

    # every file dialog's filter as pywebview reads it (a '-' in the description once made it refuse the dialog)
    try:
        from webview.util import parse_file_type
    except ImportError:
        print('  (pywebview not installed: file filters not checked)')
    else:
        bad, seen = [], 0
        for lang in i18n.STRINGS:                   # every language's labels
            i18n.set_language(lang)
            for kind in webapp.FILES:
                for t in webapp.file_types(kind):
                    seen += 1
                    try:
                        parse_file_type(t)
                    except ValueError:
                        bad.append(t)
        i18n.set_language('pt-BR')
        check(seen and not bad, 'file dialog filters accepted by pywebview (%d, every language)%s' % (
            seen, ': refused %s' % bad if bad else ''))
    print('%d failed' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
