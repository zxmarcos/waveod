"""A pedal in emulation: the waveod firmware running in waveod/tools/emu.py, reached through the USB host of
waveod/tools/check_editor.py. For developing the app without hardware (python -m waveod_studio --emu FW.bin).
WAVEOD_EMU_WVLIB=LIB.bin puts an effect library (waveod/tools/wvfx.py lib) in the emulated flash at start-up."""
import os
import sys


def waveod_root():
    """the waveod tree this app lives in (studio/ is one of its folders)"""
    return os.environ.get('WAVEOD_ROOT') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


def firmware_elf(fw):
    """the ELF the firmware was packed from: the one beside it (tools/buildfw.py writes FW.elf next to FW.bin), else
    build/fw's (which matches only when the .bin was packed from the last firmware build)"""
    beside = os.path.splitext(fw)[0] + '.elf'
    return beside if os.path.exists(beside) else os.path.join(waveod_root(), 'build', 'fw', 'waveod.elf')


class EmuTransport:
    """send() / receive() like waveod_studio.usb.htmidi.Port, over the emulated pedal's USB MIDI endpoints"""
    name = 'emulador'

    def __init__(self, fw, elf=None):
        root = waveod_root()
        sys.path.insert(0, os.path.join(root, 'tools'))
        import check_editor as ce
        from waveod_studio.usb import htmidi
        ce.htmidi = htmidi
        self.ce = ce
        lib = os.environ.get('WAVEOD_EMU_WVLIB')
        flash = {0x2A0000: open(lib, 'rb').read()} if lib else None     # src/fw/flash_map.h FLASH_WV_LIBRARY
        self.w = ce.Waveod(fw, elf or firmware_elf(fw), flash)
        self.stream = bytearray()

    def send(self, sysex):
        self.w.send(sysex)

    def receive(self):
        self.w.b.run_ms(5)
        while True:
            d = self.w.u.transfer_in(0x83)
            if d is None:
                break
            for i in range(0, len(d) - 3, 4):
                self.stream += d[i + 1:i + 1 + self.ce.CIN_LEN[d[i] & 0x0f]]
        out = []
        while 0xF7 in self.stream:
            e = self.stream.index(0xF7)
            s = self.stream.rfind(0xF0, 0, e)
            if s >= 0:
                out.append(bytes(self.stream[s:e + 1]))
            del self.stream[:e + 1]
        return out

    def wait(self, seconds):
        """time passes on the emulated pedal with audio running (protocol.Pedal.measure_latency)"""
        self.w.play(seconds)

    def latency_cable(self, route):
        """the latency test's cable: 7 samples beyond the buffers, as converters would add"""
        self.w.latency_cable(route)

    def close(self):
        pass
