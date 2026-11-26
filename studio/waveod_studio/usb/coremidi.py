"""The pedal's USB-MIDI ports on macOS (CoreMIDI through ctypes, no packages): list_ports() and Port, the same as
htmidi's winmm ones (htmidi picks this module on macOS).

Port opens a CoreMIDI client with an output port to the pedal's destination and an input port connected to its
source. CoreMIDI calls the read callback on its own thread with packet lists; the bytes go into a buffer that
receive() cuts into complete SysEx messages, as the Windows port does with its input buffers. send() uses
MIDISendSysex and waits for it to finish.
"""
import ctypes
import platform
import threading
import time

from .htmidi import PORT_HINTS, _pick          # noqa: F401  (PORT_HINTS: htmidi's, re-exported)

_UTF8 = 0x08000100                              # kCFStringEncodingUTF8
_ARM = platform.machine().startswith('arm')     # MIDIPacketNext aligns packets to 4 bytes on Arm

_cm = _cf = None


def _libs():
    global _cm, _cf
    if _cm is None:
        cm = ctypes.CDLL('/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI')
        cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
        u32, i32, vp = ctypes.c_uint32, ctypes.c_int32, ctypes.c_void_p
        cm.MIDIGetNumberOfSources.restype = ctypes.c_ulong
        cm.MIDIGetNumberOfDestinations.restype = ctypes.c_ulong
        cm.MIDIGetSource.argtypes = cm.MIDIGetDestination.argtypes = [ctypes.c_ulong]
        cm.MIDIGetSource.restype = cm.MIDIGetDestination.restype = u32
        cm.MIDIObjectGetStringProperty.argtypes = [u32, vp, ctypes.POINTER(vp)]
        cm.MIDIObjectGetStringProperty.restype = i32
        cm.MIDIClientCreate.argtypes = [vp, vp, vp, ctypes.POINTER(u32)]
        cm.MIDIClientCreate.restype = i32
        cm.MIDIOutputPortCreate.argtypes = [u32, vp, ctypes.POINTER(u32)]
        cm.MIDIOutputPortCreate.restype = i32
        cm.MIDIInputPortCreate.argtypes = [u32, vp, vp, vp, ctypes.POINTER(u32)]
        cm.MIDIInputPortCreate.restype = i32
        cm.MIDIPortConnectSource.argtypes = [u32, u32, vp]
        cm.MIDIPortConnectSource.restype = i32
        cm.MIDIPortDisconnectSource.argtypes = [u32, u32]
        cm.MIDIPortDispose.argtypes = [u32]
        cm.MIDIClientDispose.argtypes = [u32]
        cm.MIDISendSysex.argtypes = [ctypes.c_void_p]
        cm.MIDISendSysex.restype = i32
        cf.CFStringCreateWithCString.argtypes = [vp, ctypes.c_char_p, u32]
        cf.CFStringCreateWithCString.restype = vp
        cf.CFStringGetCString.argtypes = [vp, ctypes.c_char_p, ctypes.c_long, u32]
        cf.CFStringGetCString.restype = ctypes.c_bool
        cf.CFRelease.argtypes = [vp]
        _cm, _cf = cm, cf
    return _cm, _cf


def _cfstr(text):
    _, cf = _libs()
    return cf.CFStringCreateWithCString(None, text.encode('utf-8'), _UTF8)


def _name(obj):
    cm, cf = _libs()
    prop = ctypes.c_void_p.in_dll(cm, 'kMIDIPropertyDisplayName')
    s = ctypes.c_void_p()
    if cm.MIDIObjectGetStringProperty(obj, prop.value, ctypes.byref(s)) or not s.value:
        return '?'
    buf = ctypes.create_string_buffer(256)
    ok = cf.CFStringGetCString(s, buf, len(buf), _UTF8)
    cf.CFRelease(s)
    return buf.value.decode('utf-8', 'replace') if ok else '?'


def list_ports():
    """(input names, output names): the MIDI sources and destinations, by their display names"""
    cm, _ = _libs()
    ins = [_name(cm.MIDIGetSource(i)) for i in range(cm.MIDIGetNumberOfSources())]
    outs = [_name(cm.MIDIGetDestination(i)) for i in range(cm.MIDIGetNumberOfDestinations())]
    return ins, outs


_READ_PROC = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
_DONE_PROC = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class _SysexRequest(ctypes.Structure):           # MIDISysexSendRequest
    _fields_ = [('destination', ctypes.c_uint32), ('data', ctypes.c_void_p), ('bytesToSend', ctypes.c_uint32),
                ('complete', ctypes.c_bool), ('reserved', ctypes.c_uint8 * 3), ('completionProc', _DONE_PROC),
                ('completionRefCon', ctypes.c_void_p)]


@_DONE_PROC
def _sent(_request):                             # MIDISendSysex wants a completion proc; complete is polled
    pass


class Port:
    """SysEx in/out on the pedal's USB-MIDI ports (CoreMIDI)"""

    def __init__(self, want=None):
        cm, cf = _libs()
        ins, outs = list_ports()
        self.in_id, self.out_id = _pick(ins, want), _pick(outs, want)
        self.name = outs[self.out_id]
        self.src, self.dst = cm.MIDIGetSource(self.in_id), cm.MIDIGetDestination(self.out_id)
        self.lock = threading.Lock()
        self.stream = bytearray()
        self.partial = bytearray()
        self.client, self.outp, self.inp = ctypes.c_uint32(), ctypes.c_uint32(), ctypes.c_uint32()
        self._read = _READ_PROC(self._on_packets)    # kept: CoreMIDI holds only the pointer
        names = [_cfstr(n) for n in ('waveod Studio', 'waveod out', 'waveod in')]
        try:
            r = cm.MIDIClientCreate(names[0], None, None, ctypes.byref(self.client))
            if r:
                raise RuntimeError('MIDIClientCreate falhou (%d)' % r)
            r = cm.MIDIOutputPortCreate(self.client, names[1], ctypes.byref(self.outp))
            if r:
                raise RuntimeError('MIDIOutputPortCreate falhou (%d)' % r)
            r = cm.MIDIInputPortCreate(self.client, names[2], ctypes.cast(self._read, ctypes.c_void_p), None,
                                       ctypes.byref(self.inp))
            if r:
                raise RuntimeError('MIDIInputPortCreate falhou (%d)' % r)
            r = cm.MIDIPortConnectSource(self.inp, self.src, None)
            if r:
                raise RuntimeError('MIDIPortConnectSource falhou (%d)' % r)
        except Exception:
            if self.client.value:
                cm.MIDIClientDispose(self.client)
            raise
        finally:
            for n in names:
                cf.CFRelease(n)

    def _on_packets(self, pktlist, _ref, _src):
        """CoreMIDI's thread: the bytes of a MIDIPacketList (packed to 4 bytes: u64 time, u16 length, data)"""
        n = ctypes.c_uint32.from_address(pktlist).value
        at = pktlist + 4
        got = bytearray()
        for _ in range(n):
            length = ctypes.c_uint16.from_address(at + 8).value
            got += ctypes.string_at(at + 10, length)
            at += 10 + length
            if _ARM:
                at = (at + 3) & ~3
        with self.lock:
            self.stream += got

    def send(self, sysex):
        cm, _ = _libs()
        data = ctypes.create_string_buffer(bytes(sysex), len(sysex))
        req = _SysexRequest(self.dst, ctypes.cast(data, ctypes.c_void_p), len(sysex), False, (0, 0, 0), _sent, None)
        r = cm.MIDISendSysex(ctypes.byref(req))
        if r:
            raise RuntimeError('MIDISendSysex falhou (%d)' % r)
        t0 = time.time()
        while not req.complete and time.time() - t0 < 2:
            time.sleep(0.001)
        if not req.complete:
            req.complete = True                  # gives up on the rest; CoreMIDI then calls the completion
            raise RuntimeError('a SysEx não foi enviada em 2 s')

    def receive(self):
        """complete SysEx messages received so far"""
        with self.lock:
            self.partial += self.stream
            self.stream.clear()
        out = []
        while 0xF7 in self.partial:
            e = self.partial.index(0xF7)
            s = self.partial.rfind(0xF0, 0, e)
            if s >= 0:
                out.append(bytes(self.partial[s:e + 1]))
            del self.partial[:e + 1]
        return out

    def close(self):
        cm, _ = _libs()
        if self.client.value:
            cm.MIDIPortDisconnectSource(self.inp, self.src)
            cm.MIDIClientDispose(self.client)        # disposes its ports too
            self.client = ctypes.c_uint32()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
