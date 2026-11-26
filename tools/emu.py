"""Run a waveod firmware in emulation (Unicorn, Cortex-M7) the way the pedal would.

    python tools/emu.py FIRMWARE.bin [--elf build/fw/waveod.elf] [--out DIR] [COMMAND ...]

The MCU section is loaded like the bootloader does; the flash is built from the firmware's own sections
(effects 0x480000, IRs 0x700000, factory presets 0x440000, an erased record store). Peripherals are plain
registers with a few behaviours: DWT / GPT count, SysTick is applied between instruction chunks (the
millisecond counter is advanced the way its interrupt would), the LCD controller switches to NEXT_BUF at
once, the button shift register returns the scripted buttons, the touch controller (GT911) answers on its
bit-banged I2C bus, and the audio DMA interrupt is called synchronously with a test signal.

Commands (same as the simulator): press:B hold:B turn:E:N tap:X,Y long:X,Y drag:X0,Y0:X1,Y1 wait:MS
shot:NAME audio:BLOCKS, play:MS[:HZ] (audio blocks interleaved with the main loop), loop:L|R|SEND|off[:EXTRA]
(a cable from an output to an input instead of the test tone, EXTRA samples beyond the 128 of the buffers), cpu:US (the time every audio
block measures from then on, to exercise the overload guard), plus reboot (power cycle keeping the record store), flash[:FILE] (erase / program
summary; FILE gets the 512 KB record store) and leds (the footswitch LEDs from the shift register's outputs).
A fault in the main context is delivered to the firmware's vector like the core would (exception frame, EXC_RETURN);
SysTick runs the firmware's handler every millisecond; the RTWDOG counts at 32.768 kHz and resets the chip when it
expires. A reset requested through AIRCR or by the RTWDOG restarts the firmware at once, keeping the flash and the
SNVS general purpose registers (a warm reset). The FlexSPI model executes the firmware's erase and program
commands on the emulated flash and reports a fault for any write outside the record store.
Needs the 'unicorn' package (PYTHONPATH).
"""
import argparse
import collections
import math
import os
import struct
import sys
import zlib

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_ERR_INSN_INVALID, \
    UC_ERR_EXCEPTION
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, \
    UC_ARM_REG_R2, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_MAX
from unicorn import arm_const

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLASH = 0x60000000
CHUNK = 300000                      # instructions between SysTick ticks (~0.5 ms at 600 MHz)
RETURN_STUB = 0x0007fff0            # end of ITCM, never used by the image
BUTTONS = {'MENU': 31, 'EXIT': 24, 'EFX': 25, 'SAVE': 26, 'DRUM': 27, 'GLOBAL': 28, 'CTRL': 29, 'EXP': 30,
           'A': 21, 'B': 20, 'C': 23, 'D': 22, 'UP': 17, 'DOWN': 16}
ENCODERS = {'MENU': 0, 'K1': 1, 'K2': 2, 'K3': 3}
EXT_SWITCHES = {'FS1': 16, 'FS2': 22}       # GPIO1 pins of the EXP 2 jack's switches, 0 = pressed
POT_CHANNELS = {'MASTER': 13, 'EXP1': 15, 'EXP2': 11}   # ADC_ETC result slots (board/qme200/controls.c)
GPIO1, GPIO2, GPIO3 = 0x401b8000, 0x401bc000, 0x401c0000
TOUCH_SCL, TOUCH_SDA, TOUCH_INT = 4, 5, 3          # GPIO3 pins
LCDIF_CUR, LCDIF_NEXT = 0x402b8040, 0x402b8050
FLEXSPI = 0x402a8000
FLEXSPI_INTR, FLEXSPI_IPCR0, FLEXSPI_IPCR1, FLEXSPI_IPCMD = FLEXSPI + 0x14, FLEXSPI + 0xa0, FLEXSPI + 0xa4, FLEXSPI + 0xb0
WRITABLE = 0x780000                 # the record store (the snapshot "reboot" keeps)
WRITE_AREAS = ((0x753000, 0x800000), (0x2A0000, 0x440000))   # user IRs + record store; waveod's effect library,
                                                              # its store + Clone captures (src/platform/flash.h)
DMA_INT, DMA_CITER0 = 0x400e8024, 0x400e9016
AIRCR, CFSR = 0xe000ed0c, 0xe000ed28
RTWDOG_CS, RTWDOG_CNT, RTWDOG_TOVAL = 0x400bc000, 0x400bc004, 0x400bc008
SRC_SRSR = 0x400f8008
# the temperature monitor (board/qme200/tempmon.c) and its factory calibration: a part fused with 105 C at count
# 0x4A0 and 25 C at 0x5A0 (typical numbers); the die reads Emu.cpu_temp degrees once measuring started
TEMPMON_TS0, TEMPMON_TS0_SET, OCOTP_ANA1 = 0x400d8180, 0x400d8184, 0x401f44e0
ANA1_CAL = 0x5A0 << 20 | 0x4A0 << 8 | 105
WARM_KEPT = (0x400d4100, 0x400d4104, 0x400d4108, 0x400d410c, 0x400a4000, SRC_SRSR)   # SNVS LPGPR0-3, request word
# footswitch LEDs: (red, green, blue) virtual pins of the shift register outputs (board/qme200/leds.c), active low
LED_PINS = {'A': (0x09, 0x0a, 0x0b), 'B': (0x0e, 0x0d, 0x0c), 'C': (0x03, 0x02, 0x01), 'D': (0x04, 0x05, 0x06),
            'BANK-': (0x11, 0x12, 0x13), 'BANK+': (0x16, 0x15, 0x14), 'EXP': (0x1d, 0x1c, None)}
LED_NAMES = ('off', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan', 'white')


class GT911:
    """the touch controller as an I2C slave (address 0x14, 16-bit register addresses)"""
    ADDR = 0x14

    def __init__(self):
        self.mem = {0x8140: ord('9'), 0x8141: ord('1'), 0x8142: ord('1'), 0x8143: 0,
                    0x8048: 480 & 0xff, 0x8049: 480 >> 8, 0x804a: 272 & 0xff, 0x804b: 272 >> 8, 0x804d: 0x01}
        self.mode, self.nbit, self.shift, self.drive = 'idle', 0, 0, 1
        self.rw = self.nack = self.byte = 0
        self.ptr, self.rx = 0, []
        self.reads = 0                          # reads of the status register

    def report(self, touching, x, y):
        """a new scan result: status = ready | point count, then the first point"""
        self.mem.update({0x814e: 0x80 | (1 if touching else 0), 0x814f: 0, 0x8150: x & 0xff, 0x8151: x >> 8,
                         0x8152: y & 0xff, 0x8153: y >> 8})

    def start(self):
        # nbit -1: the master pulls SCL low once more before the first data bit
        self.mode, self.nbit, self.shift, self.drive, self.rx = 'addr', -1, 0, 1, []

    def stop(self):
        self.mode, self.drive = 'idle', 1

    def scl_rise(self, sda):
        if self.mode in ('addr', 'rx') and self.nbit < 8:
            self.shift = (self.shift << 1 | sda) & 0xff
        elif self.mode == 'tx' and self.nbit == 8:
            self.nack = sda

    def scl_fall(self):
        if self.mode == 'idle':
            return
        self.nbit += 1
        if self.mode in ('addr', 'rx'):
            if self.nbit == 8:
                if self.mode == 'addr':
                    if self.shift >> 1 != self.ADDR:
                        self.mode = 'idle'
                        return
                    self.rw = self.shift & 1
                else:
                    self.rx.append(self.shift)
                    if len(self.rx) == 2:
                        self.ptr = self.rx[0] << 8 | self.rx[1]
                    elif len(self.rx) > 2:
                        self.mem[self.ptr] = self.shift
                        self.ptr += 1
                self.drive = 0                  # ACK
            elif self.nbit == 9:
                self.drive, self.nbit, self.shift = 1, 0, 0
                if self.mode == 'addr':
                    self.mode = 'tx' if self.rw else 'rx'
                    if self.rw:
                        self.load()
        elif self.mode == 'tx':
            if self.nbit < 8:
                self.drive = (self.byte >> (7 - self.nbit)) & 1
            elif self.nbit == 8:
                self.drive = 1                  # the master's ACK / NACK
            elif self.nack:
                self.mode = 'idle'
            else:
                self.load()

    def load(self):
        self.byte = self.mem.get(self.ptr, 0)
        if self.ptr == 0x814e:
            self.reads += 1
        self.ptr += 1
        self.nbit = 0
        self.drive = (self.byte >> 7) & 1


USB1 = 0x402e0000
USB_USBCMD, USB_USBSTS, USB_USBINTR = USB1 + 0x140, USB1 + 0x144, USB1 + 0x148
USB_LISTADDR, USB_PORTSC1 = USB1 + 0x158, USB1 + 0x184
USB_SETUPSTAT, USB_PRIME, USB_FLUSH, USB_STAT, USB_COMPLETE = (USB1 + 0x1ac, USB1 + 0x1b0, USB1 + 0x1b4,
                                                               USB1 + 0x1b8, USB1 + 0x1bc)
USB_IRQ = 113
UART = 0x40184000                   # LPUART1, the TRS MIDI port
UART_STAT, UART_CTRL, UART_DATA, UART_WATER = UART + 0x14, UART + 0x18, UART + 0x1c, UART + 0x2c
UART_IRQ, UART_TIE = 20, 1 << 23
GPT2_OCR1 = 0x401f0010
IRQ_HANDLERS = {USB_IRQ: 'usb_otg1_irq', UART_IRQ: 'lpuart1_irq', 101: 'gpt2_irq'}


class ChipIdea:
    """the USB OTG1 controller in device mode, as far as the driver needs it: queue heads and transfer
    descriptors are read from memory, transfers complete when the host side (below) moves data"""

    def __init__(self, board):
        self.b = board
        self.cursor = {}                        # endpoint bit -> the next descriptor to work on

    @staticmethod
    def bit(ep):
        return (1 << 16 if ep & 0x80 else 1) << (ep & 0x0f)

    def qh(self, ep):
        return self.b.regs.get(USB_LISTADDR, 0) + 64 * ((ep & 0x0f) * 2 + (ep >> 7))

    def write(self, a4, value):
        """register writes with side effects; returns False when the plain store must not happen"""
        regs = self.b.regs
        if a4 in (USB_USBSTS, USB_SETUPSTAT, USB_COMPLETE):          # write 1 to clear
            regs[a4] = regs.get(a4, 0) & ~value
            return False
        if a4 == USB_USBCMD:
            regs[a4] = value & ~2                                       # the reset completes at once
            return False
        if a4 == USB_PRIME:
            for n in range(32):
                if value >> n & 1:
                    ep = (n - 16) | 0x80 if n >= 16 else n
                    self.cursor[1 << n] = struct.unpack('<I', self.b.uc.mem_read(self.qh(ep) + 8, 4))[0]
                    regs[USB_STAT] = regs.get(USB_STAT, 0) | 1 << n
            regs[a4] = 0
            return False
        if a4 == USB_FLUSH:
            regs[USB_STAT] = regs.get(USB_STAT, 0) & ~value
            for n in range(32):
                if value >> n & 1:
                    self.cursor.pop(1 << n, None)
            regs[a4] = 0
            return False
        return True

    def read(self, a4, v):
        if a4 == USB_USBCMD:
            return v                                                    # tripwires never trip
        return v

    def raise_irq(self, sts):
        self.b.regs[USB_USBSTS] = self.b.regs.get(USB_USBSTS, 0) | sts
        self.b.irq(USB_IRQ, 'usb_otg1_irq')

    def next_td(self, ep):
        """the first active descriptor queued on an endpoint (None = nothing primed)"""
        bit = self.bit(ep)
        if not self.b.regs.get(USB_STAT, 0) & bit:
            return None
        td = self.cursor.get(bit, 1)
        mem = self.b.uc
        while not td & 1:
            tok = struct.unpack('<I', mem.mem_read(td + 4, 4))[0]
            if tok & 0x80:
                return td
            td = struct.unpack('<I', mem.mem_read(td, 4))[0]
        # the end of the list: has the driver appended since? (it re-reads the queue head when priming)
        self.b.regs[USB_STAT] &= ~bit
        return None

    def complete(self, ep, td, moved):
        mem = self.b.uc
        tok = struct.unpack('<I', mem.mem_read(td + 4, 4))[0]
        total = (tok >> 16) & 0x7fff
        mem.mem_write(td + 4, struct.pack('<I', (tok & 0x8000) | (total - moved) << 16))
        nxt = struct.unpack('<I', mem.mem_read(td, 4))[0]
        bit = self.bit(ep)
        self.cursor[bit] = nxt
        if nxt & 1:
            self.b.regs[USB_STAT] = self.b.regs.get(USB_STAT, 0) & ~bit
        self.b.regs[USB_COMPLETE] = self.b.regs.get(USB_COMPLETE, 0) | bit
        if tok & 0x8000:
            self.raise_irq(1)

    def td_buffer(self, td):
        return struct.unpack('<I', self.b.uc.mem_read(td + 8, 4))[0]

    # ---------------------------------------------------------------- the host's side
    def bus_reset(self):
        self.raise_irq(0x40)
        self.b.regs[USB_PORTSC1] = 2 << 26                              # high speed
        self.raise_irq(0x04)

    def setup(self, pkt):
        self.b.uc.mem_write(self.qh(0) + 0x28, bytes(pkt))
        self.b.regs[USB_SETUPSTAT] = self.b.regs.get(USB_SETUPSTAT, 0) | 1
        self.raise_irq(1)

    def stalled(self, ep):
        ctrl = self.b.regs.get(USB1 + 0x1c0 + 4 * (ep & 0x0f), 0)
        return bool(ctrl >> (16 if ep & 0x80 else 0) & 1)

    def transfer_in(self, ep, maxlen=None):
        """one IN transfer (a whole descriptor): its bytes, or None when nothing is queued"""
        td = self.next_td(ep | 0x80)
        if td is None:
            return None
        tok = struct.unpack('<I', self.b.uc.mem_read(td + 4, 4))[0]
        n = (tok >> 16) & 0x7fff
        if maxlen is not None:
            n = min(n, maxlen)
        data = bytes(self.b.uc.mem_read(self.td_buffer(td), n)) if n else b''
        self.complete(ep | 0x80, td, n)
        return data

    def transfer_out(self, ep, data):
        """data into the next OUT descriptor; False when none is queued (NAK)"""
        td = self.next_td(ep & 0x7f)
        if td is None:
            return False
        tok = struct.unpack('<I', self.b.uc.mem_read(td + 4, 4))[0]
        n = min(len(data), (tok >> 16) & 0x7fff)
        if n:
            self.b.uc.mem_write(self.td_buffer(td), bytes(data[:n]))
        self.complete(ep & 0x7f, td, n)
        return True

    def control(self, setup, data=b''):
        """a control transfer: the IN data stage's bytes (b'' for OUT), None when the device stalled"""
        regs = self.b.regs
        regs[USB1 + 0x1c0] = regs.get(USB1 + 0x1c0, 0) & ~0x10001       # the SETUP clears the stall
        self.setup(setup)
        wlen = setup[6] | setup[7] << 8
        if setup[0] & 0x80:
            got = b''
            while True:
                if self.stalled(0x80):
                    return None
                part = self.transfer_in(0)
                if part is None:
                    return None
                got += part
                if len(part) % 64 or len(got) >= wlen or not part:
                    break
            if not self.transfer_out(0, b''):
                return None
            return got
        if wlen and not self.transfer_out(0, data):
            return None
        if self.stalled(0x80) or self.transfer_in(0) is None:
            return None
        return b''


class Board:
    def __init__(self, fw_path, elf_path, flash=None):
        """flash: {offset: bytes} written into the flash image before start-up (waveod's library at 0x2A0000)"""
        data = open(fw_path, 'rb').read()
        _, secs = mkfw.read_frmw(data)
        self.syms = mkfw.symbols(open(elf_path, 'rb').read())
        # Unicorn's Cortex-M7 has no double-precision FPU (the RT1062 has one and modules use it), so the
        # A-profile 'max' core runs the Thumb-2 code and the few M-profile system instructions are emulated
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.primask = 1
        for base, size in ((0, 0x80000), (0x20000000, 0x20000), (0x20200000, 0x80000), (0x80000000, 0x2000000),
                           (FLASH, 0x800000)):
            uc.mem_map(base, size)
        for base, size in ((0x40000000, 0x2400000), (0xe0000000, 0x100000)):
            uc.mmio_map(base, size, self.mmio_read, base, self.mmio_write, base)
        mcu = secs[0]
        for at in (0x10, 0x1c):
            fa, size, dst = struct.unpack_from('<3I', mcu, at)
            uc.mem_write(dst, mcu[fa - 0x20000:fa - 0x20000 + size])
        uc.mem_write(FLASH, b'\xff' * 0x800000)
        for s, off in ((1, 0x480000), (2, 0x700000), (3, 0x440000)):
            uc.mem_write(FLASH + off, secs[s])
        for off, blob in (flash or {}).items():
            uc.mem_write(FLASH + off, bytes(blob))
        uc.mem_write(RETURN_STUB, b'\xfe\xe7')
        self.sp, self.reset = struct.unpack_from('<2I', mcu, struct.unpack_from('<I', mcu, 0x10)[0] - 0x20000)
        self.regs, self.poll = {}, collections.Counter()
        self.cyc = self.gpt = 0
        self.cyc_step = 50000                   # cycles an audio block measures (cpu:US sets it)
        self.in_block = 0                       # in an audio block: 1 until its first DWT_CYCCNT read, then 2
        self.loopback = None                    # a cable from an output to an input (loop:OUT:EXTRA)
        self.played = {}                        # what that output played, by sample
        self.buttons = 0xffffffff               # shift register word, 0 = pressed
        self.ext = 0                            # EXT_SWITCHES pressed (GPIO1 bits)
        self.enc = [0, 0, 0, 0]
        self.shift_word, self.shift_bit, self.g1, self.g2 = 0xffffffff, 0, 0, 0
        self.touch = GT911()
        self.t_scl, self.t_sda = 1, 1
        self.wel, self.flash_log, self.last_cmd, self.txfifo = False, [], 0, bytearray()
        # W25Q64 status registers as the pedal has them: nothing protected, QE set, SR3 at its default; flash_vol =
        # the last command was 50h (volatile status write enable). Kept over a warm restart, like the chip.
        self.flash_sr1, self.flash_sr2, self.flash_sr3, self.flash_vol = 0x00, 0x02, 0x60, False
        self.ip_prep = False                    # between the TX FIFO clear and the command start
        self.xip_hook = None                    # the read hook, only while ip_prep (set_prep)
        self.ms_addr = self.sym('ms_count')
        self.pc = self.reset
        self.faults = []
        self.pots = {}                          # POT_CHANNELS name -> 12-bit reading (default: mid travel)
        self.cpu_temp = 45.0                    # the die, degrees C (TEMPMON)
        self.nvic = set()                       # interrupts enabled in the NVIC
        self.pending = {}                       # irq -> handler name, raised while disabled
        self.usb = ChipIdea(self)
        self.uart_rx, self.uart_tx = bytearray(), bytearray()      # the TRS port: to the pedal, from it
        self.ipsr = 0                           # exception being handled (MRS IPSR), set when a fault is delivered
        self.reset_req = None                   # why the chip restarts ('AIRCR', 'RTWDOG'), None = running
        self.emu_ms = 0.0                       # emulated time, also while interrupts are off
        self.wd_cyc, self.wd_ms = 0, 0.0        # RTWDOG: cycle count and time at its last refresh
        self.out_bits, self.shift_out = [], None   # shift register: bits clocked out, the last latched word
        for at in (0x10, 0x1c):
            fa, size, dst = struct.unpack_from('<3I', mcu, at)
            self.hook_system_instructions(mcu[fa - 0x20000:fa - 0x20000 + size], dst)

    def hook_system_instructions(self, image, base):
        """CPSID/CPSIE i, MSR/MRS of M-profile special registers: hooked by address and emulated"""
        for off in range(0, len(image) - 3, 2):
            hw1, hw2 = struct.unpack_from('<HH', image, off)
            if hw1 in (0xb672, 0xb662) or (hw1 & 0xfff0) == 0xf380 and (hw2 & 0xff00) == 0x8800 \
                    or hw1 == 0xf3ef and (hw2 & 0xf000) == 0x8000:
                self.uc.hook_add(UC_HOOK_CODE, self.system_instruction, begin=base + off, end=base + off)

    def system_instruction(self, uc, address, size, data):
        hw1, hw2 = struct.unpack('<HH', uc.mem_read(address, 4))
        if hw1 in (0xb672, 0xb662):
            self.primask = 1 if hw1 == 0xb672 else 0
            uc.reg_write(UC_ARM_REG_PC, (address + 2) | 1)
            return
        if hw1 == 0xf3ef:                       # MRS Rd, sysreg: MSP, IPSR, PRIMASK; the others read 0
            sysm = hw2 & 0xff
            v = uc.reg_read(UC_ARM_REG_SP) if sysm == 8 else self.ipsr if sysm == 5 else self.primask if sysm == 16 else 0
            uc.reg_write(getattr(arm_const, 'UC_ARM_REG_R%d' % ((hw2 >> 8) & 0xf)), v)
        elif (hw2 & 0xff) == 8:                 # MSR MSP, Rn
            uc.reg_write(UC_ARM_REG_SP, uc.reg_read(getattr(arm_const, 'UC_ARM_REG_R%d' % (hw1 & 0xf))))
        elif (hw2 & 0xff) == 16:                # MSR PRIMASK, Rn
            self.primask = uc.reg_read(getattr(arm_const, 'UC_ARM_REG_R%d' % (hw1 & 0xf))) & 1
        uc.reg_write(UC_ARM_REG_PC, (address + 4) | 1)

    def sym(self, name):
        for k, v in self.syms.items():
            if k == name or k.startswith(name + '.'):
                return v[0]
        raise KeyError(name)

    # ---------------------------------------------------------------- peripherals
    def mmio_write(self, uc, offset, size, value, base):
        addr = base + offset
        a4, sh = addr & ~3, (addr & 3) * 8
        mask = ((1 << (8 * size)) - 1) << sh
        if a4 == GPIO3 + 0x18:                  # ISR: write 1 to clear
            self.regs[a4] = self.regs.get(a4, 0) & ~((value << sh) & mask)
            return
        if 0xe000e100 <= a4 < 0xe000e120 or 0xe000e180 <= a4 < 0xe000e1a0:     # NVIC ISER / ICER
            first = 32 * ((a4 & 0x1f) // 4)
            for n in range(32):
                if (value << sh) >> n & 1:
                    (self.nvic.add if a4 < 0xe000e180 else self.nvic.discard)(first + n)
            return
        if 0xe000e200 <= a4 < 0xe000e220:      # NVIC ISPR: pending by software
            first = 32 * ((a4 & 0x1f) // 4)
            for n in range(32):
                if (value << sh) >> n & 1 and first + n in IRQ_HANDLERS:
                    self.pending[first + n] = IRQ_HANDLERS[first + n]
            return
        if a4 == AIRCR and (value >> 16) == 0x05fa and value & 4:     # SYSRESETREQ
            self.reset_req = 'AIRCR'
            uc.emu_stop()
            return
        if a4 == RTWDOG_CNT and value == 0xb480a602:                  # refresh
            self.wd_cyc, self.wd_ms = self.cyc, self.emu_ms
            return
        if a4 == SRC_SRSR:                      # write 1 to clear
            self.regs[a4] = self.regs.get(a4, 0) & ~value
            return
        if a4 == UART_DATA:                     # a byte out of the TRS port (the FIFO empties at once)
            self.uart_tx.append(value & 0xff)
            return
        if a4 == UART_STAT:                     # write 1 to clear: nothing is ever set but the FIFO flags
            return
        if a4 == UART_CTRL and value & UART_TIE:
            self.pending[UART_IRQ] = 'lpuart1_irq'
        if USB1 <= a4 < USB1 + 0x200 and size == 4 and not self.usb.write(a4, value):
            self.poll.clear()
            return
        self.regs[a4] = (self.regs.get(a4, 0) & ~mask) | ((value << sh) & mask)
        self.poll.clear()
        if a4 in (GPIO3, GPIO3 + 4):
            self.touch_bus()
        elif a4 == FLEXSPI_IPCMD and value & 1:
            self.flexspi_command()
        elif a4 == FLEXSPI + 0xbc and value & 1:  # IPTXFCR: clear the TX FIFO
            self.txfifo = bytearray()
            self.set_prep(True)
        elif a4 == FLEXSPI_INTR and value & 0x40:
            self.flexspi_tx()
        if a4 == GPIO2:                         # parallel load of the button register on the rising edge
            v = self.regs[a4]
            if (v >> 23) & 1 and not (self.g2 >> 23) & 1:
                self.shift_word, self.shift_bit = self.buttons, 0
                self.out_bits = []
            if (v >> 24) & 1 and not (self.g2 >> 24) & 1 and len(self.out_bits) >= 32:     # output latch
                bits = self.out_bits[:32]
                self.shift_out = bytes(sum(bits[8 * i + k] << (7 - k) for k in range(8)) for i in range(4))
            self.g2 = v
        elif a4 == GPIO1:
            v = self.regs[a4]
            if v & 1 and not self.g1 & 1:
                self.shift_bit += 1
                self.out_bits.append((v >> 1) & 1)  # data out, first bit = bit 7 of output byte 0
            self.g1 = v

    def touch_lines(self):
        """SCL / SDA levels: an input pin reads the pull-up, SDA is also pulled low by the slave"""
        dr, gdir = self.regs.get(GPIO3, 0), self.regs.get(GPIO3 + 4, 0)
        scl = (dr >> TOUCH_SCL) & 1 if (gdir >> TOUCH_SCL) & 1 else 1
        sda = (dr >> TOUCH_SDA) & 1 if (gdir >> TOUCH_SDA) & 1 else 1
        return scl, sda & self.touch.drive

    def touch_bus(self):
        """follow the master on GPIO3[4] (SCL) / GPIO3[5] (SDA)"""
        scl, sda = self.touch_lines()
        if scl != self.t_scl:
            if scl:
                self.touch.scl_rise(sda)
            else:
                self.touch.scl_fall()
        elif scl and sda != self.t_sda:
            if sda:
                self.touch.stop()
            else:
                self.touch.start()
        self.t_scl, self.t_sda = self.touch_lines()

    def touch_report(self, touching, x=0, y=0):
        """a GT911 scan report and its INT pulse (latched in the GPIO3 edge status)"""
        self.touch.report(touching, x, y)
        self.regs[GPIO3 + 0x18] = self.regs.get(GPIO3 + 0x18, 0) | 1 << TOUCH_INT

    def set_prep(self, on):
        """watch the memory-mapped reads only while an IP command is being set up: a read hook on the flash all
        the time trips a Unicorn 2.1 bug (a hooked conditional load inside an IT block, such as ldrhls.w from
        the flash, leaves the IT state behind and the next conditional branch faults as an invalid instruction)
        """
        self.ip_prep = on
        if on and self.xip_hook is None:
            self.xip_hook = self.uc.hook_add(UC_HOOK_MEM_READ, self.xip_read, begin=FLASH, end=FLASH + 0x800000 - 1)
            self.uc.ctl_flush_tb()
        elif not on and self.xip_hook is not None:
            self.uc.hook_del(self.xip_hook)
            self.xip_hook = None
            self.uc.ctl_flush_tb()

    def xip_read(self, uc, access, addr, size, value, ud):
        """a memory-mapped read while an IP command is being set up starts an AHB prefetch that can clash
        with it (seen on the pedal as wrong program data)"""
        if self.ip_prep:
            self.faults.append('flash read at %#x while an IP command is being prepared' % addr)

    def flexspi_command(self):
        """an IP command: the instruction byte comes from the first LUT entry of the sequence"""
        self.set_prep(False)
        addr = self.regs.get(FLEXSPI_IPCR0, 0) & 0xffffff
        ipcr1 = self.regs.get(FLEXSPI_IPCR1, 0)
        cmd = self.regs.get(FLEXSPI + 0x200 + 16 * ((ipcr1 >> 16) & 0x1f), 0) & 0xff
        self.last_cmd = cmd
        vol, self.flash_vol = self.flash_vol, cmd == 0x50
        if cmd in (0x01, 0x31, 0x11):
            self.flash_sr_write(cmd, vol)
        elif cmd == 0x50:
            pass                                # the next status register write is a volatile one
        elif cmd == 0x06:
            self.wel = True
        elif cmd in (0x20, 0x02):
            if not self.wel or not any(lo <= addr < hi for lo, hi in WRITE_AREAS):
                self.faults.append('flash command %#x at %#x (write enable %s)' % (cmd, addr, self.wel))
                return
            self.wel = False
            if self.flash_protected(addr):      # the chip ignores it without an error
                self.flash_log.append(('refused', cmd, addr))
                return
            if cmd == 0x20:
                self.uc.mem_write(FLASH + (addr & ~0xfff), b'\xff' * 0x1000)
                self.flash_log.append(('erase', addr & ~0xfff))
            else:                               # page program: the data is already in the TX FIFO
                size = ipcr1 & 0xffff
                if (addr & 0xff) + size > 0x100 or size > len(self.txfifo):
                    self.faults.append('program %#x+%d: crosses a page or the FIFO holds %d bytes' %
                                       (addr, size, len(self.txfifo)))
                    return
                old = self.uc.mem_read(FLASH + addr, size)
                self.uc.mem_write(FLASH + addr, bytes(a & b for a, b in zip(old, self.txfifo[:size])))
                self.flash_log.append(('program', addr, size))

    def flash_sr_write(self, cmd, volatile):
        """a W25Q64 status register write: only volatile ones (after 50h) are expected; SRL ignores them all"""
        value = self.txfifo[0] if self.txfifo else 0xff
        reg = {0x01: 1, 0x31: 2, 0x11: 3}[cmd]
        if not volatile:
            self.faults.append('non-volatile flash status register %d write %#04x (write enable %s)' %
                               (reg, value, self.wel))
            self.wel = False
            return
        if self.flash_sr2 & 1:                  # SRL: locked until the power goes
            self.flash_log.append(('sr refused', reg, value))
            return
        if reg == 1:
            if value & 0x80:
                self.faults.append('flash SRP set (%#04x)' % value)
            self.flash_sr1 = value & 0xfc
        elif reg == 2:
            if not value & 2 or value & 0x78:
                self.faults.append('flash status 2 %#04x: QE off, LB (one-time) or CMP set' % value)
            self.flash_sr2 = value & 0x7b
        else:
            self.flash_sr3 = value
        self.flash_log.append(('sr', reg, value))

    def flash_protected(self, addr):
        """whether BP0-2 / TB / SEC (CMP = 0) cover the address, the W25Q64's table"""
        bp, tb, sec = self.flash_sr1 >> 2 & 7, self.flash_sr1 >> 5 & 1, self.flash_sr1 >> 6 & 1
        if bp == 0:
            return False
        if bp == 7:
            return True
        size = (0x1000 << min(bp - 1, 3)) if sec else (0x20000 << (bp - 1))
        return addr < size if tb else addr >= 0x800000 - size

    def flexspi_tx(self):
        """the firmware pushed one watermark's worth into the TX FIFO (TFDR registers)"""
        wm = (((self.regs.get(FLEXSPI + 0xbc, 0) >> 2) & 0x3f) + 1) * 8
        for w in range(min(wm, 128) // 4):
            self.txfifo += struct.pack('<I', self.regs.get(FLEXSPI + 0x180 + 4 * w, 0))
        if len(self.txfifo) > 128:
            self.faults.append('TX FIFO overflow (%d bytes)' % len(self.txfifo))
    def mmio_read(self, uc, offset, size, base):
        addr = base + offset
        if 0x403c8000 <= addr < 0x403d8000 and (addr & 0x3fff) in (0x0e, 0x10):   # ENCn UPOS / LPOS
            pos = self.enc[(addr - 0x403c8000) >> 14] & 0xffffffff
            return (pos >> 16) & 0xffff if (addr & 0x3fff) == 0x0e else pos & 0xffff
        a4 = addr & ~3
        v = self.regs.get(a4, 0)
        if a4 == 0xe0001004:                    # DWT_CYCCNT
            # in an audio block: the first read (the engine's t0) is followed by cyc_step, the next ones by a
            # cycle each, so the block measures cyc_step however often it reads the counter (the slots' profile,
            # the engine's sections); elsewhere a fixed step per read, which keeps busy waits short
            if self.in_block:
                v = self.cyc
                self.cyc = (self.cyc + (self.cyc_step if self.in_block == 1 else 1)) & 0xffffffff
                self.in_block = 2
            else:
                self.cyc = (self.cyc + 50000) & 0xffffffff
                v = self.cyc
        elif a4 == 0x401ec024:                  # GPT1 CNT at 24 MHz: 1/25 of the cycles -> 600 MHz
            self.gpt = (self.gpt + 2000) & 0xffffffff
            v = self.gpt
        elif a4 == GPIO1:
            bit = (self.shift_word >> (31 - self.shift_bit)) & 1 if self.shift_bit < 32 else 1
            v = (v & ~4) | (bit << 2)
            for pin in EXT_SWITCHES.values():   # the jack's switches idle high (pulled up)
                v = v & ~(1 << pin) if self.ext >> pin & 1 else v | 1 << pin
        elif a4 == GPIO3 + 8:                   # PSR: the touch bus lines, INT idle high
            scl, sda = self.touch_lines()
            v = (v & ~(1 << TOUCH_SCL | 1 << TOUCH_SDA)) | scl << TOUCH_SCL | sda << TOUCH_SDA | 1 << TOUCH_INT
        elif a4 == GPIO3 + 0x18:
            pass
        elif a4 == RTWDOG_CS:                   # unlocked and reconfigured at once
            v |= 0xc00
        elif a4 == RTWDOG_CNT:                  # 32.768 kHz since the last refresh, /256 with PRES
            cs = self.regs.get(RTWDOG_CS, 0)
            v = 0
            if cs & 0x80:
                v = ((self.cyc - self.wd_cyc) & 0xffffffff) * 32768 // 600_000_000
                v = (v >> 8 if cs & 0x1000 else v) & 0xffff
        elif a4 == SRC_SRSR or 0x400d4100 <= a4 < 0x400d4110:
            pass                                # plain registers, never polled
        elif a4 == FLEXSPI_INTR:                # command done, RX data ready, TX FIFO has room
            v = 0x61 if len(self.txfifo) == 0 else 0x21
        elif a4 == FLEXSPI:                     # MCR0: the software reset completes at once
            v &= ~1
        elif a4 == FLEXSPI + 0xe0:              # STS0: idle
            v = 3
        elif a4 == FLEXSPI + 0xf0:              # IPRXFSTS: one 64-bit entry filled
            v = 1
        elif a4 == FLEXSPI + 0x100:             # RFDR: JEDEC id, status 2 / 3, or status 1 (never busy, WEL as set)
            v = {0x9f: 0x1740ef, 0x35: self.flash_sr2, 0x15: self.flash_sr3}.get(
                self.last_cmd, self.flash_sr1 | (2 if self.wel else 0))
        elif a4 == LCDIF_CUR:
            v = self.regs.get(LCDIF_NEXT, v)
        elif USB1 <= a4 < USB1 + 0x200:
            v = self.usb.read(a4, v)
        elif a4 == UART_STAT:                   # TDRE, TC, RDRF while bytes wait
            v = 0xc00000 | (0x200000 if self.uart_rx else 0)
        elif a4 == UART_WATER:                  # RXCOUNT (the FIFO holds 4), TXCOUNT 0
            v = min(len(self.uart_rx), 4) << 24
        elif a4 == UART_DATA:
            v = self.uart_rx.pop(0) if self.uart_rx else 0
        elif a4 == OCOTP_ANA1:
            v = ANA1_CAL
        elif a4 == TEMPMON_TS0:                 # TEMP_CNT for cpu_temp, FINISHED once MEASURE_TEMP was set
            hot, hot_n, room_n = ANA1_CAL & 0xff, ANA1_CAL >> 8 & 0xfff, ANA1_CAL >> 20 & 0xfff
            n = round(hot_n + (hot - self.cpu_temp) * (room_n - hot_n) / (hot - 25)) & 0xfff
            done = 4 if (self.regs.get(TEMPMON_TS0_SET, 0) | v) & 2 else 0
            v = (v & ~0xfff04) | n << 8 | done
        elif a4 == 0x403b0004:                  # ADC_ETC DONE0/1
            v = 3
        elif 0x403b0028 <= a4 < 0x403b0060 and (a4 - 0x403b0010) % 0x28 >= 0x18:
            v = 2048 | 2048 << 16               # potentiometers at mid travel, unless set in self.pots
            for name, ch in POT_CHANNELS.items():
                t, k = ch // 8, (ch % 8) // 2
                if a4 == 0x403b0028 + 0x28 * t + 4 * k and name in self.pots:
                    sh = 16 if ch & 1 else 0
                    v = (v & ~(0xffff << sh)) | (self.pots[name] & 0xfff) << sh

        else:                                   # a register polled over and over: let the loop finish
            k = (uc.reg_read(UC_ARM_REG_PC), a4)
            self.poll[k] += 1
            if self.poll[k] > 20:
                v = 0xffffffff if self.poll[k] & 1 else 0
        return (v >> ((addr & 3) * 8)) & ((1 << 8 * size) - 1)

    # ---------------------------------------------------------------- execution
    def irq(self, n, handler):
        """an interrupt request: the handler runs now if the NVIC and PRIMASK let it, else when they do"""
        if n in self.nvic and not self.primask:
            self.pending.pop(n, None)
            self.call(self.sym(handler))
        else:
            self.pending[n] = handler

    def deliver(self):
        for n, handler in list(self.pending.items()):
            if n in self.nvic and not self.primask:
                del self.pending[n]
                self.call(self.sym(handler))
                if n == UART_IRQ and (self.regs.get(UART_CTRL, 0) & UART_TIE or self.uart_rx):
                    self.pending[n] = handler       # still something to send or take

    def uart_in(self, data):
        """bytes arriving on the TRS input"""
        self.uart_rx += bytes(data)
        self.pending[UART_IRQ] = 'lpuart1_irq'

    def deliver_fault(self, ex):
        """a fault of the main context, taken like the core does: exception frame on the MSP, EXC_RETURN in LR,
        the handler from the vector table (UsageFault for an undefined instruction, BusFault otherwise)"""
        uc = self.uc
        if self.ipsr:                           # a fault inside a fault handler: the core would lock up
            return False
        pc = uc.reg_read(UC_ARM_REG_PC)
        usage = ex.errno in (UC_ERR_INSN_INVALID, UC_ERR_EXCEPTION)
        n = 6 if usage else 5
        self.regs[CFSR] = 1 << 16 if usage else 0x8200      # UNDEFINSTR / PRECISERR + BFARVALID
        sp = (uc.reg_read(UC_ARM_REG_SP) - 32) & ~7
        regs = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, arm_const.UC_ARM_REG_R3,
                                         arm_const.UC_ARM_REG_R12, UC_ARM_REG_LR)]
        uc.mem_write(sp, struct.pack('<8I', *regs, pc, 0x01000000))
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, 0xfffffff9)
        self.ipsr = n
        self.pc = struct.unpack('<I', uc.mem_read(4 * n, 4))[0] & ~1
        print('fault: %s at pc=%#x, delivered as exception %d' % (ex, pc, n))
        return True

    def run_ms(self, ms):
        """run the main context for `ms` emulated milliseconds (until a reset is requested)"""
        uc = self.uc
        if self.pc == self.reset:
            uc.reg_write(UC_ARM_REG_SP, self.sp)
        for _ in range(int(ms * 2)):
            if self.reset_req:
                return True
            self.deliver()
            try:
                uc.emu_start(self.pc | 1, 0xffffffff, count=CHUNK)
                self.pc = uc.reg_read(UC_ARM_REG_PC)
                if self.reset_req:
                    return True
            except UcError as ex:
                if self.reset_req:
                    return True
                if not self.deliver_fault(ex):
                    self.faults.append('%s at pc=%#x' % (ex, uc.reg_read(UC_ARM_REG_PC)))
                    return False
            self.emu_ms += 0.5
            cs = self.regs.get(RTWDOG_CS, 0)
            if cs & 0x80:                       # the RTWDOG counts emulated time, interrupts or not
                tov = self.regs.get(RTWDOG_TOVAL, 0xffff) & 0xffff
                if self.emu_ms - self.wd_ms > tov * (256 if cs & 0x1000 else 1) * 1000 / 32768:
                    self.regs[SRC_SRSR] = self.regs.get(SRC_SRSR, 0) | 0x80
                    self.reset_req = 'RTWDOG'
                    return True
            if self.regs.get(0xe000e010, 0) & 1 and not self.primask:
                if not hasattr(self, 'half'):
                    self.half = 0
                self.half ^= 1
                if self.half:                   # the firmware's SysTick handler, with an exception frame
                    self.call(self.sym('systick_handler'), frame=True)
        return True

    def call(self, fn, *args, budget=50_000_000, count_instructions=False, frame=False):
        """call a function synchronously (an interrupt handler, for instance) and restore the context;
        frame: an exception frame of the interrupted code at the handler's stack pointer (PC, xPSR = IPSR)"""
        uc = self.uc
        ctx = uc.context_save()
        for reg, val in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), args):
            uc.reg_write(reg, val)
        sp = uc.reg_read(UC_ARM_REG_SP)          # an exception frame below the interrupted code's stack
        sp = (sp - 0x200) & ~7 if 0x20000400 < sp <= 0x20008000 else 0x20007000
        if frame:
            uc.mem_write(sp, struct.pack('<8I', 0, 0, 0, 0, 0, 0, self.pc, self.ipsr))
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RETURN_STUB | 1)
        n = [0]
        h = uc.hook_add(UC_HOOK_CODE, lambda *a: n.__setitem__(0, n[0] + 1)) if count_instructions else None
        try:
            uc.emu_start(fn | 1, RETURN_STUB, count=budget)
        except UcError as ex:
            self.faults.append('%s at pc=%#x in call %#x' % (ex, uc.reg_read(UC_ARM_REG_PC), fn))
        if h is not None:
            uc.hook_del(h)
        uc.context_restore(ctx)
        return n[0]

    def audio_block(self, phase, freq=220.0, level=0.25, count=False):
        """one DMA interrupt: fill the half that just completed with a sine on the guitar input"""
        uc = self.uc
        rx = self.sym('rx_buf')
        half = phase & 1
        frames = []
        lb = self.loopback
        for i in range(64):
            if lb:                              # the output word played 128 (the buffers) + extra samples ago
                x = self.played.get(phase * 64 + i - 128 - lb['extra'], 0) * lb['gain']
                v = max(-2 ** 31, min(2 ** 31 - 1, int(x)))
                f = [0] * 6
                f[lb['a']], f[lb['b']] = v, max(-2 ** 31, min(2 ** 31 - 1, -v))
                frames += f
                continue
            x = level * math.sin(2 * math.pi * freq * (phase * 64 + i) / 44100)
            v = int(x * 980586112)              # the engine's full scale (differential: R - L)
            frames += [-v, 0, 0, v, 0, 0]
        uc.mem_write(rx + half * 64 * 6 * 4, struct.pack('<384i', *frames))
        self.regs[DMA_INT] = 1
        self.regs[DMA_CITER0 & ~3] = (self.regs.get(DMA_CITER0 & ~3, 0) & 0xffff) | ((700 if half else 100) << 16)
        self.in_block = 1
        try:
            n = self.call(self.sym('dma0_dma16_irq'), count_instructions=count)
        finally:
            self.in_block = 0
        tx = self.sym('tx_buf')
        out = struct.unpack('<256i', uc.mem_read(tx + half * 64 * 4 * 4, 1024))
        if lb:
            for i in range(64):
                self.played[phase * 64 + i] = out[4 * i + lb['out']]
                self.played.pop(phase * 64 + i - 8192, None)
        return n, out

    def set_loopback(self, route, extra=0, gain=0.5):
        """route L, R (main outputs into the guitar input) or SEND (FX send into FX return); None = no cable"""
        self.played = {}
        if route is None:
            self.loopback = None
            return
        out = {'L': 0, 'SEND': 1, 'R': 2}[route]
        a, b = (4, 1) if route == 'SEND' else (3, 0)
        self.loopback = dict(out=out, a=a, b=b, extra=int(extra), gain=gain)

    def screen(self):
        fb = self.regs.get(LCDIF_NEXT, 0x81f00000) or 0x81f00000
        px = self.uc.mem_read(fb, 480 * 272 * 4)
        rgb = bytearray(480 * 272 * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = px[2::4], px[1::4], px[0::4]
        return bytes(rgb)


def png(path, rgb, w=480, h=272):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def warm_reset(b, fw, elf, boot_ms):
    """the chip restarted (AIRCR or RTWDOG): the firmware boots again on the same flash; the SNVS general
    purpose registers, the bootloader's request word and the reset status survive"""
    print('reset: %s at %.0f ms' % (b.reset_req, b.emu_ms))
    areas = [(lo, bytes(b.uc.mem_read(FLASH + lo, hi - lo))) for lo, hi in WRITE_AREAS]
    kept = {a: b.regs[a] for a in WARM_KEPT if a in b.regs}
    log = b.flash_log
    nb = Board(fw, elf)
    for lo, data in areas:
        nb.uc.mem_write(FLASH + lo, data)
    nb.regs.update(kept)
    nb.flash_log = log
    print('restart: %s' % ('ok' if nb.run_ms(boot_ms) else 'FAULT ' + '; '.join(nb.faults)))
    return nb


def leds_text(b):
    if b.shift_out is None:
        return 'nothing latched'
    word = b.shift_out
    out = []
    for name, pins in LED_PINS.items():
        colour = sum(1 << c for c, pin in enumerate(pins) if pin is not None and not (word[pin >> 3] >> (pin & 7)) & 1)
        out.append('%s=%s' % (name, LED_NAMES[colour]))
    return ' '.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fw')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'emu'))
    ap.add_argument('--boot-ms', type=int, default=400)
    ap.add_argument('commands', nargs='*')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    b = Board(a.fw, a.elf)
    ok = b.run_ms(a.boot_ms)
    print('boot: %s' % ('ok' if ok else 'FAULT ' + '; '.join(b.faults)))
    phase = 0
    for cmd in a.commands:
        while b.reset_req:
            b = warm_reset(b, a.fw, a.elf, a.boot_ms)
        p = cmd.split(':')
        if p[0] == 'leds':
            b.run_ms(20)
            print('leds: ' + leds_text(b))
        elif p[0] in ('press', 'hold'):                # press:A+B presses both at once
            names = p[1].split('+')
            mask = sum(1 << BUTTONS[n] for n in names if n in BUTTONS)
            ext = sum(1 << EXT_SWITCHES[n] for n in names if n in EXT_SWITCHES)
            b.buttons &= ~mask
            b.ext |= ext
            b.run_ms(800 if p[0] == 'hold' else 60)
            b.buttons |= mask
            b.ext &= ~ext
            b.run_ms(60)
        elif p[0] in ('tap', 'long', 'drag'):
            pts = [tuple(int(v) for v in q.split(',')) for q in p[1:]]
            if p[0] == 'drag':
                (x0, y0), (x1, y1) = pts
                pts = [(x0, y0)] * 2 + [(x0 + (x1 - x0) * i // 10, y0 + (y1 - y0) * i // 10) for i in range(1, 11)]
                pts += [(x1, y1)] * 4            # a finger stops before it lifts
            else:
                pts = pts * (100 if p[0] == 'long' else 8)
            for x, y in pts:                    # a report every 10 ms while the finger is down
                b.touch_report(True, x, y)
                b.run_ms(10)
            b.touch_report(False, *pts[-1])
            b.run_ms(100)
        elif p[0] == 'reboot':                  # power cycle: same flash, fresh everything else
            areas = [(lo, bytes(b.uc.mem_read(FLASH + lo, hi - lo))) for lo, hi in WRITE_AREAS]
            log = b.flash_log
            b = Board(a.fw, a.elf)
            for lo, data in areas:
                b.uc.mem_write(FLASH + lo, data)
            b.flash_log = log
            print('reboot: %s' % ('ok' if b.run_ms(a.boot_ms) else 'FAULT ' + '; '.join(b.faults)))
        elif p[0] == 'flash':                   # what was written so far, and the store as a file
            erases = sum(1 for e in b.flash_log if e[0] == 'erase')
            programmed = sum(e[2] for e in b.flash_log if e[0] == 'program')
            print('flash: %d sector erases, %d bytes programmed, lowest address %#x' %
                  (erases, programmed, min((e[1] for e in b.flash_log), default=0)))
            if len(p) > 1:
                open(p[1], 'wb').write(bytes(b.uc.mem_read(FLASH + WRITABLE, 0x80000)))
        elif p[0] == 'turn':
            b.enc[ENCODERS[p[1]]] += 4 * int(p[2])
            b.run_ms(120)
        elif p[0] == 'wait':
            b.run_ms(int(p[1]))
        elif p[0] == 'loop':                    # loop:L:7 = a cable from OUT L to the input, 7 samples beyond the
            b.set_loopback(None if p[1] == 'off' else p[1], int(p[2]) if len(p) > 2 else 0)     # buffers; loop:off
        elif p[0] == 'cpu':                     # every audio block from now on measures US microseconds
            b.cyc_step = int(float(p[1]) * 600)
        elif p[0] == 'shot':
            b.run_ms(40)
            path = os.path.join(a.out, p[1] + '.png')
            png(path, b.screen())
            print(path)
        elif p[0] == 'play':                    # real-time-like: one audio block, then 1.45 ms of main loop
            freq = float(p[2]) if len(p) > 2 else 220.0
            for _ in range(int(float(p[1]) / 1.4512)):
                b.audio_block(phase, freq=freq)
                phase += 1
                b.run_ms(1.4512)
        elif p[0] == 'audio':
            n = int(p[1])
            peak, peak_r = 0, 0
            for i in range(n):
                cnt, out = b.audio_block(phase, count=i == n - 1)
                phase += 1
                peak = max(peak, max(abs(x) for x in out[0::4]))
                peak_r = max(peak_r, max(abs(x) for x in out[2::4]))
            print('audio: %d blocks, output peak %.4f of full scale (R %.4f), last block %d instructions' %
                  (n, peak / 2 ** 31, peak_r / 2 ** 31, cnt))
        else:
            sys.exit('unknown command ' + cmd)
        if b.faults:
            print('FAULT: ' + '; '.join(b.faults))
            sys.exit(1)
    while b.reset_req:
        b = warm_reset(b, a.fw, a.elf, a.boot_ms)


if __name__ == '__main__':
    main()
