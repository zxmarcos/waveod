"""The pedal's flash chip: JEDEC id and status registers 1-3 as the firmware read them at start-up (protocol 13,
0x1F00001E), decoded.

    python tools/flashstatus.py                     the pedal over USB (close the editor / Studio first)
    python tools/flashstatus.py --emu FW.bin FW.elf the firmware in the emulator
    python tools/flashstatus.py --lock-test         a WAVEOD_LOCK_TEST build (preset fw-locktest), which locked the
                                                    top 4 KB (0x7FF000) at start-up until power-off, checks the chip
                                                    refuses to erase or program it; then the status again

Without --lock-test it only reads: the firmware read the registers (05h, 35h, 15h) at start-up, before the lock,
and again after it, and this asks for what it read. The bits are a Winbond W25Q64's (JEDEC EF4017); for another chip only the
raw values are shown.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'studio'))

KNOWN = {0xEF4017: 'Winbond W25Q64 (8 MB)'}

# (register, bit, name, meaning when set)
BITS = (
    (0, 0, 'BUSY', 'a write / erase runs'),
    (0, 1, 'WEL', 'write enabled'),
    (0, 2, 'BP0', 'block protect'),
    (0, 3, 'BP1', 'block protect'),
    (0, 4, 'BP2', 'block protect'),
    (0, 5, 'TB', 'protection from the bottom'),
    (0, 6, 'SEC', '4 KB sector protection'),
    (0, 7, 'SRP', 'status register protect (with /WP)'),
    (1, 0, 'SRL', 'status registers locked until power-off'),
    (1, 1, 'QE', 'quad mode (IO2/IO3 as data)'),
    (1, 3, 'LB1', 'security register 1 locked FOR EVER'),
    (1, 4, 'LB2', 'security register 2 locked FOR EVER'),
    (1, 5, 'LB3', 'security register 3 locked FOR EVER'),
    (1, 6, 'CMP', 'complement protection'),
    (1, 7, 'SUS', 'erase / program suspended'),
    (2, 2, 'WPS', 'individual block locks in use'),
    (2, 5, 'DRV0', 'output drive'),
    (2, 6, 'DRV1', 'output drive'),
)


def protected(sr1, sr2):
    """what BP/TB/SEC/CMP protect on a 64 Mbit Winbond part (datasheet table, CMP = 0 only)"""
    bp, tb, sec, cmp_ = (sr1 >> 2) & 7, sr1 >> 5 & 1, sr1 >> 6 & 1, sr2 >> 6 & 1
    if cmp_:
        return 'CMP=1: complement table (not decoded)'
    if bp == 0:
        return 'nothing'
    if bp == 7:
        return 'the whole chip'
    size = (0x1000 << min(bp - 1, 3)) if sec else (0x20000 << (bp - 1))
    lo, hi = (0, size - 1) if tb else (0x800000 - size, 0x7FFFFF)
    return '%#08x-%#08x (%d KB)' % (lo, hi, size // 1024)


def registers(title, sr, known):
    """one set of status registers, raw and decoded"""
    print(title)
    for i, v in enumerate(sr):
        print('   status %d  ' % (i + 1), 'not read' if v is None else '%02X  (%s)' % (v, format(v, '08b')))
    if known and None not in sr:
        names = ['%s (%s)' % (name, meaning) for reg, bit, name, meaning in BITS if sr[reg] >> bit & 1]
        print('   set bits: ', ', '.join(names) or 'none')
        print('   protected:', protected(sr[0], sr[1]))


def show(st):
    from waveod_studio.protocol import FLASH_LOCKS
    jedec = st['jedec']
    known = jedec in KNOWN
    print('JEDEC id   ', '%06X' % jedec if jedec else 'not read', KNOWN.get(jedec, '(unknown chip)') if jedec else '')
    registers('as the chip came up (start-up, before the lock):', st['sr'], known)
    if st['sr_now'] != st['sr']:
        registers('as last read (after the lock):', st['sr_now'], known)
    print('lock:       ', FLASH_LOCKS[st['lock']] if st['lock'] < len(FLASH_LOCKS) else st['lock'])
    print('\n' + st['diag'])

def lock_test(p):
    """run the test, print what happened; True when the chip refused everything"""
    from waveod_studio.protocol import FLASH_LOCKS
    r = p.lock_test()
    if r['reads']:
        print('status read now (8 x):', ' '.join('%02X/%02X/%02X' % x for x in r['reads']))
    print('lock (start-up): %s' % (FLASH_LOCKS[r['state']] if 0 <= r['state'] < len(FLASH_LOCKS) else r['state']))
    if r['state'] != 1:
        return False
    print('erase:       flash_erase_sector -> %d' % r['erase'])
    if r['spot'] is None:
        print('program:     not tried (no 16 erased bytes in the sector)')
    else:
        print('program:     16 zero bytes at +%#x -> %d (-1 = the read-back differs: refused)' % (r['spot'], r['program']))
    if r['changed_at'] is None:
        print('sector:      unchanged: the chip refused the erase and the program')
        return True
    print('sector:      CHANGED at +%#x: the protection did not hold; put back: %s' %
          (r['changed_at'], {1: 'yes', -1: 'FAILED', 0: 'not needed'}[r['restored']]))
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emu', nargs=2, metavar=('FW', 'ELF'), help='the firmware in the emulator')
    ap.add_argument('--lock-test', action='store_true', help='a WAVEOD_LOCK_TEST build: test its locked top 4 KB')
    a = ap.parse_args()
    from waveod_studio.protocol import Pedal
    if a.emu:
        from waveod_studio.emulated import EmuTransport
        t = EmuTransport(*a.emu)
        p = Pedal(transport=t, timeout=20)
        if a.lock_test:
            ok = lock_test(p)
            print()
        show(p.flash_status())
        log = [e for e in t.w.b.flash_log if e[0] in ('sr', 'sr refused', 'refused')]
        if log:
            print('\nemulated chip:', *log, sep='\n  ')
        faults = t.w.b.faults
        if faults:
            print('\nemulator faults:', *faults, sep='\n  ')
        if a.lock_test and (not ok or faults):
            sys.exit(1)
        return
    with Pedal() as p:
        info = p.info()
        if info['protocol'] < 13:
            sys.exit('o pedal está no protocolo %d; o estado da flash precisa do 13' % info['protocol'])
        if a.lock_test:
            ok = lock_test(p)
            print()
        show(p.flash_status())
        if a.lock_test and not ok:
            sys.exit(1)


if __name__ == '__main__':
    main()
