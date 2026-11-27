"""The new interface's connection (webapp.Api): waiting for the pedal, telling why it cannot connect, noticing it
went away and finding it again, with fake MIDI ports (no pedal needed).

    python tests/test_link.py
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))

from waveod_studio import i18n, protocol as P, webapp     # noqa: E402
from waveod_studio.usb import htmidi                      # noqa: E402

i18n.set_language('pt-BR')                          # the messages checked below are the Portuguese ones

failed = 0
NAME = 'Matribox II Pro'


def check(cond, what):
    global failed
    print('%s  %s' % ('ok  ' if cond else 'FAIL', what))
    failed += not cond


class World:
    """what the fake Windows and the fake pedal do: plugged, port free, firmware answering, its protocol"""
    plugged = False
    busy = False
    answers = True
    protocol = 8
    opened = 0
    closed = 0


class FakePedal:
    def __init__(self, port=None):
        if not World.plugged:
            raise RuntimeError('MIDI port not found')
        if World.busy:
            raise htmidi.PortError('midiOutOpen failed (4): is the PC editor still open?')
        World.opened += 1
        self.name = NAME

    def __enter__(self):
        return self

    def close(self):
        World.closed += 1

    def info(self):
        if not World.answers or not World.plugged:
            raise P.NotWaveod('o pedal não respondeu')
        return dict(protocol=World.protocol, version='test', presets=240, active=0, modified=False)

    def presets(self):
        if not World.plugged:
            raise P.NotWaveod('o pedal não respondeu')
        return [dict(index=0, name='A')]


def ports():
    return ([NAME], [NAME]) if World.plugged else ([], ['Microsoft GS Wavetable Synth'])


webapp.htmidi.list_ports = ports
webapp.P.Pedal = FakePedal
webapp.SEARCH_S = webapp.RETRY_S = webapp.WATCH_S = 0.05


def wait_phase(api, phase, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        s = api.status()
        if s['phase'] == phase:
            return s
        time.sleep(0.02)
    return api.status()


def main():
    api = webapp.Api()
    s = wait_phase(api, 'searching')
    check(s['phase'] == 'searching', 'nothing plugged in: searching (%s)' % s['message'])
    a = s['attempts']
    time.sleep(0.3)
    check(api.status()['attempts'] > a, 'keeps looking by itself: attempt %d' % api.status()['attempts'])
    try:
        api.presets()
        check(False, 'a call with no pedal is refused')
    except RuntimeError as e:
        check('desconectado' in str(e), 'a call with no pedal is refused at once: %s' % e)

    World.plugged, World.busy = True, True
    s = wait_phase(api, 'busy')
    check(s['phase'] == 'busy' and s['name'] == NAME, 'plugged, port held by another program: busy')
    World.busy, World.answers = False, False
    s = wait_phase(api, 'silent')
    check(s['phase'] == 'silent' and World.closed >= 1, 'port free, no answer: silent, the port closed again')
    World.answers, World.protocol = True, 6
    s = wait_phase(api, 'old')
    check(s['phase'] == 'old' and s['protocol'] == 6, 'answers with protocol 6: old (%s)' % s['message'])
    World.protocol = 8
    s = wait_phase(api, 'connected')
    check(s['phase'] == 'connected' and s['connections'] == 1 and s['version'] == 'test', 'protocol 8: connected')
    check(api.presets() == [dict(index=0, name='A')], 'calls go through')

    World.plugged = False                           # unplugged while idle: the port check notices
    s = wait_phase(api, 'searching')
    check(s['phase'] == 'searching' and 'desconectado' in s['message'], 'unplugged: searching again (%s)' % s['message'])
    World.plugged = True
    s = wait_phase(api, 'connected')
    check(s['phase'] == 'connected' and s['connections'] == 2, 'plugged back: connected, a new connection (2)')

    webapp.WATCH_S = 30                             # unplugged in the middle of a call, before the port check runs
    time.sleep(0.1)
    World.plugged = False
    try:
        api.presets()
        check(False, 'a call to an unplugged pedal fails')
    except RuntimeError as e:
        check('desconectado' in str(e), 'a call to an unplugged pedal: %s' % e)
    check(api.status()['phase'] == 'searching', 'and the search starts at once')
    webapp.WATCH_S = 0.05
    World.plugged = True
    api.retry()
    s = wait_phase(api, 'connected')
    check(s['connections'] == 3, 'retry(): found again at once')
    api._close()
    print('%d failed' % failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
