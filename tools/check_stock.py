"""The blocks ported from the original's own effects (docs/stock_port.md) against the original, bit for bit.

    python tools/check_stock.py STOCK.bin [blocks...] [--seconds S] [--cost] [--fw FW.bin [--elf ELF] [--no-unicorn]]

With --fw (a waveod firmware, tools/mkfw.py): the blocks are also packed into a library and installed over USB in
the emulated firmware as waveod Studio does, and each one's presets (empty, knobs moved, after factory preset 01A)
are played twice, with the original's module in the slot and with the port: the DAC words must be the same.

For each block and each scenario (knobs held, knobs moved between blocks, a stretch in bypass) the same input runs
through three things: the original's module from STOCK.bin's library in Unicorn (matribox effemu, the original's
loader and ABI), the block built as a module (tools/wvfx.py build, run by tools/wvfx_emu.py) and the block built
for the PC (tools/circuits/wvfx_host.c). Pass: all three outputs identical, every sample of both channels. On a
difference: the first differing frame and the error in dB under the original's level.

The input is the guitar DI (out/circuits/di.f32, left and right from different takes; a synthetic pluck without
it). Needs matribox-studio (pip install -e) and unicorn.
"""
import argparse
import math
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'out', 'stock')
FS = 44100
FRAMES = 64
sys.path.insert(0, HERE)
import wvbuild                                                      # noqa: E402

AMP6 = ['Gain', 'Presence', 'Volume', 'Bass', 'Middle', 'Treble']


def amp_scenarios(defaults):
    """knob sets for the amps (Gain, Presence, Volume, Bass, Middle, Treble[, Gain 2]), plus one that moves every
    knob and bypasses a stretch; a 7th knob (Gain 2) at its default, or as given"""
    d = list(defaults)
    extra = d[6:]
    s = [('defaults', d, [], None, 1.0)]
    s.append(('tone 0, gain 20, volume 50', [20.0, 0.0, 50.0, 0.0, 0.0, 0.0] + extra, [], None, 1.0))
    s.append(('all 100', [100.0] * len(d), [], None, 1.0))
    s.append(('gain 100, tone 20/80/30', [100.0, 70.0, 60.0, 20.0, 80.0, 30.0] + extra, [], None, 1.0))
    s.append(('gain 10, presence 100', [10.0, 100.0, 40.0, 50.0, 50.0, 50.0] + extra, [], None, 1.0))
    s.append(('presence 1.5 (taper point 3)', [60.0, 1.5, 50.0, 50.0, 50.0, 50.0] + extra, [], None, 1.0))
    s.append(('defaults, input +12 dB', d, [], None, 4.0))
    if extra:
        s.append(('gain 2 only (gain 1 at 0)', [0.0] + d[1:6] + [80.0], [], None, 1.0))
    moves = [(40, {0: 90.0}), (60, {5: 10.0}), (80, {3: 90.0, 4: 20.0}), (100, {1: 0.0}), (120, {2: 80.0}),
             (200, {0: 5.0, 5: 100.0}), (260, {4: 100.0})]
    if extra:
        moves += [(70, {6: 100.0}), (230, {6: 10.0})]
        moves.sort(key=lambda m: m[0])
    s.append(('knobs moving, bypass 150-180', d, moves, (150, 180), 1.0))
    return s


def passive_scenarios(defaults, middle=10.0):
    """amp_scenarios for an amp whose tone stack passes nothing with Bass, Middle and Treble all at 0 (as the circuit):
    that set with Middle at `middle`"""
    s = amp_scenarios(defaults)
    t = s[1][1][:]
    t[4] = middle
    s[1] = ('tone 0 but middle %d, gain 20' % middle, t, [], None, 1.0)
    return s


def input_scenarios(defaults):
    """passive_scenarios for an amp with an Input knob before Gain (Input, Gain, Presence, Volume, Bass, Middle,
    Treble): Input in the Gain 2 knob's place; its Gain section passes nothing at 0, so 'Input only' has Gain 5"""
    def back(k):
        return [k[6]] + k[:6]
    out = []
    for tag, knobs, moves, bypass, gain in passive_scenarios(list(defaults[1:]) + [defaults[0]]):
        if tag.startswith('gain 2 only'):
            tag, knobs = 'input 80, gain 5', [5.0] + knobs[1:]
        moves = [(b, {(0 if i == 6 else i + 1): v for i, v in m.items()}) for b, m in moves]
        out.append((tag, back(knobs), moves, bypass, gain))
    return out


def no_presence_scenarios(defaults):
    """passive_scenarios for an amp without Presence (Gain, Volume, Bass, Middle, Treble); Middle 30 in the tone-0
    set, which is near silence at 10"""
    out = []
    for tag, knobs, moves, bypass, gain in passive_scenarios([defaults[0], 50.0] + list(defaults[1:]), 30.0):
        moves = [(b, {(i - 1 if i > 1 else i): v for i, v in m.items() if i != 1}) for b, m in moves]
        out.append((tag.replace(', presence 100', ', volume 40'), knobs[:1] + knobs[2:], [m for m in moves if m[1]],
                    bypass, gain))
    return out


def knob_scenarios(defaults, keep=()):
    """for any knob set: the defaults, all 0, all 100, alternating 100 / 0 and 0 / 100, +12 dB in, and every knob
    moved in turn with a bypassed stretch; the knobs in `keep` (levels that silence the block at 0) stay at their
    defaults in the fixed sets"""
    d, n = list(defaults), len(defaults)

    def held(k):
        return [d[i] if i in keep else v for i, v in enumerate(k)]
    s = [('defaults', d, [], None, 1.0), ('all 0', held([0.0] * n), [], None, 1.0),
         ('all 100', held([100.0] * n), [], None, 1.0),
         ('100 / 0 alternating', held([100.0 if i % 2 == 0 else 0.0 for i in range(n)]), [], None, 1.0),
         ('0 / 100 alternating', held([0.0 if i % 2 == 0 else 100.0 for i in range(n)]), [], None, 1.0),
         ('defaults, input +12 dB', d, [], None, 4.0)]
    moves = [(40 + 30 * i, {i: (90.0 if d[i] < 50.0 else 10.0)}) for i in range(n)]
    moves.append((40 + 30 * n, {i: 100.0 for i in range(n)}))
    s.append(('knobs moving, bypass 150-180', d, moves, (150, 180), 1.0))
    return s


def mode_scenarios(defaults, word, keep=(), values=(1.0,)):
    """knob_scenarios plus the defaults, all 100 and the knobs moving with the Mode knob `word` at each of `values`
    (the drives that tell Mode 1, 2 from other non-zero values)"""
    s = knob_scenarios(defaults, keep)
    base = list(s[:1]) + [s[2], s[-1]]
    for mv in values:
        for tag, knobs, moves, bypass, gain in base:
            knobs = [mv if i == word else v for i, v in enumerate(knobs)]
            moves = [(b, {i: (mv if i == word else v) for i, v in m.items()}) for b, m in moves]
            s.append((tag + ', mode %g' % mv, knobs, moves, bypass, gain))
    return s


def sync_scenarios(defaults, sync, rate, keep=()):
    """knob_scenarios plus Sync on (knob `sync` at 1) with Rate (knob `rate`) at tempo divisions 0, 3, 4, 10 and at a
    value between them (one beat)"""
    s = knob_scenarios(defaults, keep)
    for r in (0.0, 3.0, 4.0, 10.0, 2.5):
        k = list(defaults)
        k[sync], k[rate] = 1.0, r
        s.append(('sync on, rate %g' % r, k, [], None, 1.0))
    return s


def dual_sync_scenarios(defaults, first, second, keep=()):
    """sync_scenarios for two LFOs (first, second = (sync knob, rate knob)), plus both synced together"""
    s = sync_scenarios(defaults, first[0], first[1], keep)
    for tag, k, moves, bypass, gain in s[7:]:
        k = list(k)
        k[second[0]], k[second[1]] = 1.0, 10.0 - k[first[1]]
        s.append((tag.replace('sync on', 'both synced') + ', 2nd rate %g' % k[second[1]], k, moves, bypass, gain))
    return s


def clamped(scenarios, word, lo, hi):
    """scenarios with knob `word` held inside [lo, hi] (a knob the original reads a table with, unchecked)"""
    def c(i, v):
        return min(max(v, lo), hi) if i == word else v
    return [(tag, [c(i, v) for i, v in enumerate(knobs)], [(b, {i: c(i, v) for i, v in m.items()}) for b, m in moves],
             bypass, gain) for tag, knobs, moves, bypass, gain in scenarios]


def delay_scenarios(defaults, sync, time, trail, keep=()):
    """knob_scenarios for a delay (Trail off in the fixed sets), plus Sync on at divisions 0, 3, 10 and between,
    the knobs moving with Trail on (its bypassed stretch rings on) and Trail off (the lines cleared) and a time
    sweep"""
    d = list(defaults)
    s = []
    for tag, knobs, moves, bypass, gain in knob_scenarios(d, keep):
        knobs = list(knobs)
        knobs[trail] = 0.0
        moves = [(b, {i: v for i, v in m.items() if i != trail}) for b, m in moves]
        s.append((tag, knobs, [m for m in moves if m[1]], bypass, gain))
    for r in (0.0, 3.0, 10.0, 2.5):
        k = list(d)
        k[sync], k[time], k[trail] = 1.0, r, 0.0
        s.append(('sync on, time %g' % r, k, [], None, 1.0))
    for tr in (1.0, 0.0):
        k = list(d)
        k[trail] = tr
        s.append(('trail %g, bypass 100-200' % tr, k, [(60, {time: 120.0})], (100, 200), 1.0))
    k = list(d)
    k[trail] = 0.0
    s.append(('time sweep 900 -> 30 ms', k, [(10, {time: 900.0}), (300, {time: 30.0}), (700, {time: 2500.0})], None,
              1.0))
    return s


def reverb_scenarios(defaults, trail, keep=()):
    """knob_scenarios for a reverb (Trail off in the fixed sets), plus the knobs moving with Trail on (the bypassed
    stretch rings on) and Trail off (the lines cleared)"""
    d = list(defaults)
    s = []
    for tag, knobs, moves, bypass, gain in knob_scenarios(d, keep):
        knobs = list(knobs)
        knobs[trail] = 0.0
        moves = [(b, {i: v for i, v in m.items() if i != trail}) for b, m in moves]
        s.append((tag, knobs, [m for m in moves if m[1]], bypass, gain))
    for tr in (1.0, 0.0):
        k = list(d)
        k[trail] = tr
        s.append(('trail %g, bypass 100-200' % tr, k, [(60, {i: 80.0 for i in range(trail) if i not in keep})],
                  (100, 200), 1.0))
    return s

def gate_scenarios(defaults, thr, levels=(0.0, 20.0, 40.0, 60.0)):
    """knob_scenarios for a gate with the threshold knob `thr` held (a high threshold silences the DI), plus the
    defaults at each threshold of `levels` and the threshold moving through them"""
    d = list(defaults)
    s = knob_scenarios(d, keep=(thr,))
    for v in levels:
        k = list(d)
        k[thr] = v
        s.append(('threshold %g' % v, k, [], None, 1.0))
    s.append(('threshold moving', d, [(40 + 60 * i, {thr: v}) for i, v in enumerate(levels)], (200, 230), 1.0))
    return s

def onoff_scenarios(defaults, knobs, switches):
    """for blocks with on/off switches that only take 0 and 1: the defaults, the `knobs` at 0, 50 and 100 with every
    combination of the switches, +12 dB in, and the knobs and switches moving with a bypassed stretch"""
    d = list(defaults)
    s = [('defaults', d, [], None, 1.0), ('defaults, input +12 dB', d, [], None, 4.0)]
    for v in (0.0, 50.0, 100.0):
        for c in range(1 << len(switches)):
            k = list(d)
            for i in knobs:
                k[i] = v
            for j, w in enumerate(switches):
                k[w] = float((c >> j) & 1)
            s.append(('knobs %g, switches %s' % (v, ''.join(str(int(k[w])) for w in switches)), k, [], None, 1.0))
    moves = [(40 + 30 * i, {w: 1.0 - d[w]}) for i, w in enumerate(switches)]
    moves += [(40 + 30 * (len(switches) + i), {k: (90.0 if d[k] < 50.0 else 10.0)}) for i, k in enumerate(knobs)]
    s.append(('knobs moving, bypass 150-180', d, moves, (150, 180), 1.0))
    return s

def harmony_scenarios(d):
    """for the Harmony D (Mix, Key, Mode, Interval 1, Interval 2, (unused), Smooth): the defaults, Mix below and
    above 0.5, each Mode with its own key and intervals, Smooth on, the intervals' extremes, +12 dB in, and the knobs
    moving with a bypassed stretch"""
    s = [('defaults', d, [], None, 1.0), ('defaults, input +12 dB', d, [], None, 4.0),
         ('mix 20', [20.0] + d[1:], [], None, 1.0), ('mix 100', [100.0] + d[1:], [], None, 1.0)]
    for mode in range(8):
        k = [70.0, float((3 * mode) % 12), float(mode), float((5 + mode) % 14), float((11 + 2 * mode) % 14), 0.0, 0.0]
        s.append(('mode %d' % mode, k, [], None, 1.0))
    s.append(('smooth on', d[:6] + [1.0], [], None, 1.0))
    s.append(('smooth on, mode 2', [60.0, 4.0, 2.0, 3.0, 12.0, 0.0, 1.0], [], None, 1.0))
    s.append(('intervals 0 / 13', [80.0, 7.0, 0.0, 0.0, 13.0, 0.0, 0.0], [], None, 1.0))
    s.append(('knobs moving, bypass 150-180', d, [(40, {0: 90.0}), (70, {1: 5.0}), (100, {2: 2.0}), (130, {3: 2.0}),
                                                  (200, {6: 1.0}), (230, {0: 30.0, 4: 6.0})], (150, 180), 1.0))
    return s


def eq_scenarios(defaults, bands=5):
    """knob_scenarios for a band EQ (the bands -50 .. 50, Volume held), plus each band alone at +50 and -50 and a
    mixed setting"""
    s = knob_scenarios(defaults, keep=(bands,))
    for w in range(bands):
        s = clamped(s, w, -50.0, 50.0)
    for w in range(bands):
        for v in (50.0, -50.0):
            k = list(defaults)
            k[w] = v
            s.append(('band %d at %+g' % (w, v), k, [], None, 1.0))
    s.append(('mixed bands', [37.0, -21.0, 8.0, -44.0, 29.0][:bands] + list(defaults[bands:]), [], None, 1.0))
    return s


def slapback_scenarios(defaults):
    """delay scenarios for the Slapback (Mix, Time, Feedback, Trail with Trail on knob word 4): 5 words, word 3 a
    copy of word 4 (the PC host maps its arguments by parameter order)"""
    d = list(defaults[:3]) + [0.0, 0.0]
    out = []
    for tag, knobs, moves, bypass, gain in knob_scenarios(d[:3]):
        out.append((tag, list(knobs) + [0.0, 0.0], moves, bypass, gain))
    for tr in (1.0, 0.0):
        out.append(('trail %g, bypass 100-200' % tr, d[:3] + [tr, tr], [(60, {1: 40.0})], (100, 200), 1.0))
    out.append(('time sweep 300 -> 20 ms', d, [(10, {1: 300.0}), (300, {1: 20.0}), (700, {1: 210.0})], None, 1.0))
    return out


def switch_scenarios(defaults, switches):
    """knob_scenarios for amps whose knobs start Gain, (Tone Cut / Presence), Volume (the Voks, the Bog SV): Gain and Volume held in the fixed sets
    (0 silences them), the switches (Bright, Char) 0 / 1, each switch on at the defaults, and turned on and off while
    the knobs move"""
    def sw(k):
        return {i: (1.0 if (v > 0.0 and i in switches) else (0.0 if i in switches else v)) for i, v in k.items()}
    out = []
    for tag, knobs, moves, bypass, gain in knob_scenarios(defaults, keep=(0, 2)):
        knobs = [sw({i: v})[i] for i, v in enumerate(knobs)]
        moves = [(b, sw(m)) for b, m in moves]
        if bypass:
            moves = sorted(moves + [(b, {i: 1.0}) for b, i in zip((60, 200), switches)] +
                           [(b, {i: 0.0}) for b, i in zip((110, 260), switches)], key=lambda m: m[0])
        out.append((tag, knobs, moves, bypass, gain))
    for i in switches:
        d = list(defaults)
        d[i] = 1.0
        out.insert(1, ('defaults, switch %d on' % i, d, [], None, 1.0))
    d = list(defaults)
    out.insert(1, ('gain 100, knob 1 at 0, volume 80', [100.0, 0.0, 80.0] + d[3:], [], None, 1.0))
    out.insert(2, ('gain 5, knob 1 at 100, volume 100', [5.0, 100.0, 100.0] + d[3:], [], None, 1.0))
    return out


def bright_scenarios(defaults):
    """no_presence_scenarios for an amp with a Bright switch after its five knobs (Gain, Volume, Bass, Middle,
    Treble, Bright): Bright off there, on at the defaults and at Gain 100, and switched on and off while the knobs
    move"""
    out = []
    for tag, knobs, moves, bypass, gain in no_presence_scenarios(defaults[:5]):
        if bypass:
            moves = sorted(moves + [(70, {5: 1.0}), (230, {5: 0.0})], key=lambda m: m[0])
        out.append((tag, knobs + [0.0], moves, bypass, gain))
    out.insert(1, ('defaults, bright on', list(defaults[:5]) + [1.0], [], None, 1.0))
    out.insert(2, ('gain 100, bright on', [100.0] + list(defaults[1:5]) + [1.0], [], None, 1.0))
    return out


def supero_scenarios(defaults, gains, volume):
    """knob_scenarios for the Supero 2 amps (their Gains and Volume held in the fixed sets: 0 silences them), plus
    the Gains low (below 0.0591, where the output level changes) at the defaults and +12 dB in"""
    out = knob_scenarios(defaults, keep=gains[:1] + (volume,))
    low = list(defaults)
    for i in gains:
        low[i] = 8.0
    out.insert(1, ('gains 8 (low level)', low, [], None, 1.0))
    out.insert(2, ('gains 8, input +12 dB', low, [], None, 4.0))
    return out


def bassvt_scenarios(defaults):
    """for the A BassVT (Gain, Bass, Middle, Midrange 0-4, Treble, Volume): each Midrange, the extremes, +12 dB in,
    and the knobs and Midrange moving with a bypassed stretch"""
    d = list(defaults)
    s = [('midrange %d' % r, d[:3] + [float(r)] + d[4:], [], None, 1.0) for r in range(5)]
    s.append(('all 100, midrange 4', [100.0, 100.0, 100.0, 4.0, 100.0, 100.0], [], None, 1.0))
    s.append(('all 5, midrange 0', [5.0, 5.0, 5.0, 0.0, 5.0, 5.0], [], None, 1.0))
    s.append(('defaults, input +12 dB', d, [], None, 4.0))
    moves = [(40, {0: 90.0}), (60, {1: 10.0}), (80, {2: 90.0, 3: 3.0}), (100, {4: 10.0}), (120, {5: 80.0}),
             (160, {3: 0.0, 1: 90.0}), (200, {0: 5.0, 4: 100.0}), (240, {3: 2.0}), (260, {2: 10.0})]
    s.append(('knobs moving, bypass 150-180', d, moves, (150, 180), 1.0))
    return s

def halen_scenarios(defaults):
    """amp_scenarios for the Halen 51, whose knob words are Gain, Volume, Bass, Middle, Treble, (unused), Presence:
    each set given as those 7 words, word 5 a copy of Presence (the PC host takes the knobs in parameter order)"""
    d = list(defaults)
    amp6 = [d[0], d[5], d[1], d[2], d[3], d[4]]
    word = {0: 0, 1: 6, 2: 1, 3: 2, 4: 3, 5: 4}
    out = []
    for tag, k, moves, bypass, gain in amp_scenarios(amp6):
        w = [k[0], k[2], k[3], k[4], k[5], k[1], k[1]]
        out.append((tag, w, [(b, {word[i]: v for i, v in m.items()}) for b, m in moves], bypass, gain))
    return out

# block -> (source under src/blocks, descriptor, the original's id, scenario maker)
BLOCKS = {
    'brit45': ('amps/brit45.c', 'brit45_block', 0x0700002A, amp_scenarios),
    'brit45p': ('amps/brit45p.c', 'brit45p_block', 0x0700002B, amp_scenarios),
    'brit45jp': ('amps/brit45jp.c', 'brit45jp_block', 0x0700002C, amp_scenarios),
    'brit50': ('amps/brit50.c', 'brit50_block', 0x0700002D, amp_scenarios),
    'brit50p': ('amps/brit50p.c', 'brit50p_block', 0x0700002E, amp_scenarios),
    'brit50jp': ('amps/brit50jp.c', 'brit50jp_block', 0x0700002F, amp_scenarios),
    'britslp': ('amps/britslp.c', 'britslp_block', 0x07000030, amp_scenarios),
    'brit800': ('amps/brit800.c', 'brit800_block', 0x07000035, amp_scenarios),
    'brit900': ('amps/brit900.c', 'brit900_block', 0x0700004E, amp_scenarios),
    'calif_iic1': ('amps/calif_iic1.c', 'calif_iic1_block', 0x07000039, amp_scenarios),
    'calif_iic2': ('amps/calif_iic2.c', 'calif_iic2_block', 0x0700003A, amp_scenarios),
    'calif_iic3': ('amps/calif_iic3.c', 'calif_iic3_block', 0x0700003B, amp_scenarios),
    'calif_ivld1': ('amps/calif_ivld1.c', 'calif_ivld1_block', 0x07000055, amp_scenarios),
    'calif_ivld2': ('amps/calif_ivld2.c', 'calif_ivld2_block', 0x07000056, amp_scenarios),
    'calif_ivld3': ('amps/calif_ivld3.c', 'calif_ivld3_block', 0x07000057, amp_scenarios),
    'calif_dualv': ('amps/calif_dualv.c', 'calif_dualv_block', 0x07000068, amp_scenarios),
    'calif_dualm': ('amps/calif_dualm.c', 'calif_dualm_block', 0x07000069, amp_scenarios),
    'calif_starcl': ('amps/calif_starcl.c', 'calif_starcl_block', 0x07000019, passive_scenarios),
    'calif_starod': ('amps/calif_starod.c', 'calif_starod_block', 0x0700004A, input_scenarios),
    'calif_bass': ('amps/calif_bass.c', 'calif_bass_block', 0x07000077, no_presence_scenarios),
    'bman_n': ('amps/bman_n.c', 'bman_n_block', 0x07000003, amp_scenarios),
    'bman_bri': ('amps/bman_bri.c', 'bman_bri_block', 0x07000024, amp_scenarios),
    'bog_svcl': ('amps/bog_svcl.c', 'bog_svcl_block', 0x0700001A, lambda d: switch_scenarios(d, (5,))),
    'bog_svod': ('amps/bog_svod.c', 'bog_svod_block', 0x0700003D, passive_scenarios),
    'bog_xtblue': ('amps/bog_xtblue.c', 'bog_xtblue_block', 0x07000043, passive_scenarios),
    'bog_xtred': ('amps/bog_xtred.c', 'bog_xtred_block', 0x0700006E, passive_scenarios),
    'dark_double': ('amps/dark_double.c', 'dark_double_block', 0x07000004, bright_scenarios),
    'dark_deluxe': ('amps/dark_deluxe.c', 'dark_deluxe_block', 0x07000005, lambda d: knob_scenarios(d, keep=(0, 1))),
    'doctor_cl': ('amps/doctor_cl.c', 'doctor_cl_block', 0x0700001B, passive_scenarios),
    'doctor_od': ('amps/doctor_od.c', 'doctor_od_block', 0x07000049, passive_scenarios),
    'dizzy_vh': ('amps/dizzy_vh.c', 'dizzy_vh_block', 0x07000065, amp_scenarios),
    'dizzy_vhp': ('amps/dizzy_vhp.c', 'dizzy_vhp_block', 0x0700006A, amp_scenarios),
    'dizzy_vhs': ('amps/dizzy_vhs.c', 'dizzy_vhs_block', 0x07000066, amp_scenarios),
    'dizzy_vhps': ('amps/dizzy_vhps.c', 'dizzy_vhps_block', 0x0700006B, amp_scenarios),
    'dragon_cl': ('amps/dragon_cl.c', 'dragon_cl_block', 0x0700001F, no_presence_scenarios),
    'dragon_clb': ('amps/dragon_clb.c', 'dragon_clb_block', 0x0700007B, no_presence_scenarios),
    'dragon_od': ('amps/dragon_od.c', 'dragon_od_block', 0x0700007C, no_presence_scenarios),
    'sol_cl': ('amps/sol_cl.c', 'sol_cl_block', 0x07000023, amp_scenarios),
    'sol_od': ('amps/sol_od.c', 'sol_od_block', 0x07000047, amp_scenarios),
    'sol_ld': ('amps/sol_ld.c', 'sol_ld_block', 0x07000059, amp_scenarios),
    'eng_120': ('amps/eng_120.c', 'eng_120_block', 0x0700005F, amp_scenarios),
    'eng_120p': ('amps/eng_120p.c', 'eng_120p_block', 0x07000060, amp_scenarios),
    'flyman_1': ('amps/flyman_1.c', 'flyman_1_block', 0x07000040, amp_scenarios),
    'flyman_2': ('amps/flyman_2.c', 'flyman_2_block', 0x07000041, amp_scenarios),
    'flyman_p1': ('amps/flyman_p1.c', 'flyman_p1_block', 0x0700005D, amp_scenarios),
    'flyman_p2': ('amps/flyman_p2.c', 'flyman_p2_block', 0x0700005E, amp_scenarios),
    'jazz_120': ('amps/jazz_120.c', 'jazz_120_block', 0x07000014, lambda d: switch_scenarios(d, (4,))),
    'tanger_r100': ('amps/tanger_r100.c', 'tanger_r100_block', 0x07000053, no_presence_scenarios),
    'twd_deluxe': ('amps/twd_deluxe.c', 'twd_deluxe_block', 0x07000001, lambda d: knob_scenarios(d, keep=(0, 2))),
    'a_bassft': ('amps/a_bassft.c', 'a_bassft_block', 0x08000075, lambda d: knob_scenarios(d, keep=(0,))),
    'f_2bass': ('amps/f_2bass.c', 'f_2bass_block', 0x08000076, lambda d: onoff_scenarios(d, (2, 3, 4), (1,)) + knob_scenarios(d, keep=(0,))[1:3]),
    'ac_preamp': ('amps/ac_preamp.c', 'ac_preamp_block', 0x0800007A, lambda d: knob_scenarios(d, keep=(0,))),
    'ac_preamp_2': ('amps/ac_preamp_2.c', 'ac_preamp_2_block', 0x0800007B, lambda d: knob_scenarios(d, keep=(0,))),
    'a_bassvt': ('amps/a_bassvt.c', 'a_bassvt_block', 0x07000073, bassvt_scenarios),
    'halen_51': ('amps/halen_51.c', 'halen_51_block', 0x0700005A, halen_scenarios),
    'superb_cl': ('amps/superb_cl.c', 'superb_cl_block', 0x07000015, amp_scenarios),
    'superb_od': ('amps/superb_od.c', 'superb_od_block', 0x07000048, amp_scenarios),
    'supero_cl': ('amps/supero_cl.c', 'supero_cl_block', 0x0700000F, lambda d: supero_scenarios(d, (0,), 2)),
    'supero_od': ('amps/supero_od.c', 'supero_od_block', 0x07000028, lambda d: supero_scenarios(d, (0, 2), 4)),
    'voks_bass': ('amps/voks_bass.c', 'voks_bass_block', 0x07000075, lambda d: knob_scenarios(d, keep=(0,))),
    'voks_30n': ('amps/voks_30n.c', 'voks_30n_block', 0x07000011, lambda d: switch_scenarios(d, (3,))),
    'voks_15tb': ('amps/voks_15tb.c', 'voks_15tb_block', 0x07000010, lambda d: switch_scenarios(d, ())),
    'voks_30tb': ('amps/voks_30tb.c', 'voks_30tb_block', 0x07000027, lambda d: switch_scenarios(d, (5,))),
    # DRIVE
    'solar_fuzz': ('drives/solar_fuzz.c', 'solar_fuzz_block', 0x03000026, lambda d: knob_scenarios(d, keep=(0, 1))),
    'red_fuzz': ('drives/red_fuzz.c', 'red_fuzz_block', 0x03000024, lambda d: knob_scenarios(d, keep=(0, 1))),
    'butter_od': ('drives/butter_od.c', 'butter_od_block', 0x03000002, lambda d: knob_scenarios(d, keep=(1,))),
    'skreamer': ('drives/skreamer.c', 'skreamer_block', 0x03000000, lambda d: knob_scenarios(d, keep=(2,))),
    'skreamer9': ('drives/skreamer9.c', 'skreamer9_block', 0x03000001, lambda d: knob_scenarios(d, keep=(2,))),
    'gerden_od': ('drives/gerden_od.c', 'gerden_od_block', 0x03000010, lambda d: knob_scenarios(d, keep=(2,))),
    'dist_plus': ('drives/dist_plus.c', 'dist_plus_block', 0x03000029, lambda d: knob_scenarios(d, keep=(1,))),
    'shark': ('drives/shark.c', 'shark_block', 0x03000030, lambda d: knob_scenarios(d, keep=(2,))),
    'blues_od': ('drives/blues_od.c', 'blues_od_block', 0x03000009, lambda d: knob_scenarios(d, keep=(2,))),
    'breaker_od': ('drives/breaker_od.c', 'breaker_od_block', 0x0300000E, lambda d: knob_scenarios(d, keep=(2,))),
    'warm_od': ('drives/warm_od.c', 'warm_od_block', 0x03000004, lambda d: knob_scenarios(d, keep=(2,))),
    'super_od': ('drives/super_od.c', 'super_od_block', 0x03000006, lambda d: knob_scenarios(d, keep=(2,))),
    'full_od': ('drives/full_od.c', 'full_od_block', 0x0300000A, lambda d: mode_scenarios(d, 3, keep=(2,))),
    'timmy_od': ('drives/timmy_od.c', 'timmy_od_block', 0x0300001E, lambda d: mode_scenarios(d, 4, keep=(1,))),
    'dark_mouse': ('drives/dark_mouse.c', 'dark_mouse_block', 0x0300002B, lambda d: knob_scenarios(d, keep=(2,))),
    'plexi_dist': ('drives/plexi_dist.c', 'plexi_dist_block', 0x0300002D, lambda d: knob_scenarios(d, keep=(1,))),
    'bass_od': ('drives/bass_od.c', 'bass_od_block', 0x0300003F,
                lambda d: mode_scenarios(d, 3, keep=(2, 3), values=(1.0, 2.0))),
    'bass_dist': ('drives/bass_dist.c', 'bass_dist_block', 0x03000040, lambda d: knob_scenarios(d, keep=(2,))),
    'master_dist': ('drives/master_dist.c', 'master_dist_block', 0x0300002E, lambda d: knob_scenarios(d, keep=(1,))),
    'sardar_dist': ('drives/sardar_dist.c', 'sardar_dist_block', 0x03000052, lambda d: knob_scenarios(d, keep=(1,))),
    'jp_dist': ('drives/jp_dist.c', 'jp_dist_block', 0x0300002A, lambda d: knob_scenarios(d, keep=(2,))),
    'strive': ('drives/strive.c', 'strive_block', 0x03000032, lambda d: mode_scenarios(d, 3, keep=(2,), values=(1.0, 2.0))),
    'fuzz_cream': ('drives/fuzz_cream.c', 'fuzz_cream_block', 0x03000022, lambda d: knob_scenarios(d, keep=(2,))),
    'mod_phaser': ('mods/mod_phaser.c', 'mod_phaser_block', 0x04000019, lambda d: sync_scenarios(d, 1, 0)),
    'mod_bbd_phaser': ('mods/mod_bbd_phaser.c', 'mod_bbd_phaser_block', 0x0400001A, lambda d: sync_scenarios(d, 2, 1)),
    'mod_phaser_st': ('mods/mod_phaser_st.c', 'mod_phaser_st_block', 0x0400001B, lambda d: sync_scenarios(d, 2, 1)),
    'mod_pan_phaser': ('mods/mod_pan_phaser.c', 'mod_pan_phaser_block', 0x0400001E, lambda d: dual_sync_scenarios(d, (4, 1), (5, 3))),
    'mod_vibe': ('mods/mod_vibe.c', 'mod_vibe_block', 0x0400001F, lambda d: sync_scenarios(d, 2, 1)),
    'mod_u_vibe': ('mods/mod_u_vibe.c', 'mod_u_vibe_block', 0x04000020, lambda d: sync_scenarios(d, 4, 1, keep=(2,))),
    'mod_vibrato': ('mods/mod_vibrato.c', 'mod_vibrato_block', 0x04000017, lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'mod_flanger': ('mods/mod_flanger.c', 'mod_flanger_block', 0x04000011, lambda d: sync_scenarios(d, 4, 1)),
    'mod_flanger_n': ('mods/mod_flanger_n.c', 'mod_flanger_n_block', 0x04000013, lambda d: sync_scenarios(d, 4, 1)),
    'mod_bass_jet': ('mods/mod_bass_jet.c', 'mod_bass_jet_block', 0x04000012, lambda d: sync_scenarios(d, 4, 1)),
    'mod_trem_jet': ('mods/mod_trem_jet.c', 'mod_trem_jet_block', 0x04000014,
                     lambda d: dual_sync_scenarios(d, (5, 1), (6, 4))),
    'mod_b_chorus': ('mods/mod_b_chorus.c', 'mod_b_chorus_block', 0x04000008, lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'mod_e_chorus': ('mods/mod_e_chorus.c', 'mod_e_chorus_block', 0x04000001, lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'mod_d_chorus': ('mods/mod_d_chorus.c', 'mod_d_chorus_block', 0x04000002,
                     lambda d: mode_scenarios(d, 0, values=(1.0, 2.0, 3.0))),
    'mod_m_chorus': ('mods/mod_m_chorus.c', 'mod_m_chorus_block', 0x0400000F, lambda d: sync_scenarios(d, 6, 1)),
    'mod_detune': ('mods/mod_detune.c', 'mod_detune_block', 0x01000029,
                   lambda d: clamped(knob_scenarios(d, keep=(2,)), 0, -50.0, 50.0)),
    'mod_lofi_bit': ('mods/mod_lofi_bit.c', 'mod_lofi_bit_block', 0x0100002E, lambda d: knob_scenarios(d)),
    'mod_bbd_roto': ('mods/mod_bbd_roto.c', 'mod_bbd_roto_block', 0x04000015, lambda d: sync_scenarios(d, 2, 1)),
    'mod_ce_roto': ('mods/mod_ce_roto.c', 'mod_ce_roto_block', 0x04000016, lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'dly_warm': ('delays/dly_warm.c', 'dly_warm_block', 0x0B000001, lambda d: delay_scenarios(d, 3, 1, 4)),
    'dly_tube': ('delays/dly_tube.c', 'dly_tube_block', 0x0B00000B, lambda d: delay_scenarios(d, 3, 1, 4)),
    'dly_eko_d': ('delays/dly_eko_d.c', 'dly_eko_d_block', 0x0B000003,
                  lambda d: delay_scenarios(d, 6, 2, 8) + [('both synced, A 3, B 7', d[:5] + [7.0, 1.0, 1.0, 0.0],
                                                            [], None, 1.0)]),
    'dly_slapback': ('delays/dly_slapback.c', 'dly_slapback_block', 0x0B000005, lambda d: slapback_scenarios(d)),
    'dly_ping_pong': ('delays/dly_ping_pong.c', 'dly_ping_pong_block', 0x0B000004, lambda d: delay_scenarios(d, 3, 2, 4)),
    'dly_999_echo': ('delays/dly_999_echo.c', 'dly_999_echo_block', 0x0B000012, lambda d: delay_scenarios(d, 3, 1, 4)),
    'dly_reverse': ('delays/dly_reverse.c', 'dly_reverse_block', 0x0B000028, lambda d: delay_scenarios(d, 4, 1, 5, keep=(3,))),
    'dly_sweep': ('delays/dly_sweep.c', 'dly_sweep_block', 0x0B000006,
                  lambda d: delay_scenarios(d, 6, 2, 7) + [('sweep synced, rate 3', d[:4] + [3.0, 1.0, 0.0, 0.0],
                                                            [], None, 1.0)]),
    'dly_mag': ('delays/dly_mag.c', 'dly_mag_block', 0x0B000002, lambda d: delay_scenarios(d, 3, 1, 4)),
    'dly_rack': ('delays/dly_rack.c', 'dly_rack_block', 0x0B000014, lambda d: delay_scenarios(d, 5, 2, 6)),
    'dly_bbd': ('delays/dly_bbd.c', 'dly_bbd_block', 0x0B00001D, lambda d: delay_scenarios(d, 6, 1, 7, keep=(1, 5))),
    'dly_ice': ('delays/dly_ice.c', 'dly_ice_block', 0x0B00002C,
                lambda d: delay_scenarios(d, 9, 1, 10, keep=(1, 8)) + [
                    ('phase %d, cuts %d / %d' % (ph, c, 3 - c), d[:5] + [float(ph), float(c), float(3 - c)] + d[8:],
                     [(200, {6: float(3 - c), 7: float(c)})], None, 1.0) for ph, c in ((1, 1), (3, 2), (0, 3))]),
    'dly_ring': ('delays/dly_ring.c', 'dly_ring_block', 0x0B000009, lambda d: delay_scenarios(d, 6, 2, 7)),
    'dly_multi_tape': ('delays/dly_multi_tape.c', 'dly_multi_tape_block', 0x0B00000C,
                       lambda d: delay_scenarios(d, 5, 1, 6, keep=(4,)) + [
                           ('mode %d' % m, d[:4] + [float(m)] + d[5:], [(300, {4: float(m % 12 + 1)})], None, 1.0)
                           for m in range(1, 13)]),
    'dly_sweet': ('delays/dly_sweet.c', 'dly_sweet_block', 0x0B00000D, lambda d: delay_scenarios(d, 3, 1, 4)),
    'dly_lo_fi': ('delays/dly_lo_fi.c', 'dly_lo_fi_block', 0x0B000026,       # knob word 5 unused
                  lambda d: delay_scenarios(d[:5] + [0.0] + d[5:], 6, 1, 7)),
    'rvb_hall': ('reverbs/rvb_hall.c', 'rvb_hall_block', 0x0C000001, lambda d: reverb_scenarios(d, 3)),
    'rvb_room': ('reverbs/rvb_room.c', 'rvb_room_block', 0x0C000000, lambda d: reverb_scenarios(d, 3)),
    'rvb_church': ('reverbs/rvb_church.c', 'rvb_church_block', 0x0C000002, lambda d: reverb_scenarios(d, 3)),
    'rvb_sky': ('reverbs/rvb_sky.c', 'rvb_sky_block', 0x0C000006, lambda d: reverb_scenarios(d, 2)),
    'rvb_sea': ('reverbs/rvb_sea.c', 'rvb_sea_block', 0x0C000007, lambda d: reverb_scenarios(d, 2)),
    'rvb_studio': ('reverbs/rvb_studio.c', 'rvb_studio_block', 0x0C00000B, lambda d: reverb_scenarios(d, 6)),
    'rvb_club': ('reverbs/rvb_club.c', 'rvb_club_block', 0x0C00000C, lambda d: reverb_scenarios(d, 6)),
    'rvb_plate': ('reverbs/rvb_plate.c', 'rvb_plate_block', 0x0C000003, lambda d: reverb_scenarios(d, 3)),
    'rvb_mod_reverb': ('reverbs/rvb_mod_reverb.c', 'rvb_mod_reverb_block', 0x0C000008, lambda d: reverb_scenarios(d, 5)),
    'rvb_shimmer': ('reverbs/rvb_shimmer.c', 'rvb_shimmer_block', 0x0C000009, lambda d: reverb_scenarios(d, 5)),
    'rvb_spring': ('reverbs/rvb_spring.c', 'rvb_spring_block', 0x0C000004, lambda d: reverb_scenarios(d, 3)),
    'rvb_haze': ('reverbs/rvb_haze.c', 'rvb_haze_block', 0x0C000015, lambda d: reverb_scenarios(d, 6)),
    'dyn_gate_1': ('dyn/dyn_gate_1.c', 'dyn_gate_1_block', 0x0000001B, lambda d: gate_scenarios(d, 0)),
    'dyn_gate_2': ('dyn/dyn_gate_2.c', 'dyn_gate_2_block', 0x0000001D, lambda d: gate_scenarios(d, 0)),
    'dyn_gate_3': ('dyn/dyn_gate_3.c', 'dyn_gate_3_block', 0x00000021, lambda d: gate_scenarios(d, 0)),
    'dyn_comp_1': ('dyn/dyn_comp_1.c', 'dyn_comp_1_block', 0x00000000, lambda d: knob_scenarios(d, keep=(1,))),
    'dyn_comp_2': ('dyn/dyn_comp_2.c', 'dyn_comp_2_block', 0x00000001, lambda d: knob_scenarios(d, keep=(2,))),
    'dyn_comp_3': ('dyn/dyn_comp_3.c', 'dyn_comp_3_block', 0x00000003, lambda d: knob_scenarios(d, keep=(2,))),
    'dyn_m_boost': ('dyn/dyn_m_boost.c', 'dyn_m_boost_block', 0x00000014, lambda d: knob_scenarios(d, keep=(0,))),
    'dyn_e_boost': ('dyn/dyn_e_boost.c', 'dyn_e_boost_block', 0x0000001A, lambda d: onoff_scenarios(d, (0,), (1, 2))),
    'dyn_ac_boost': ('dyn/dyn_ac_boost.c', 'dyn_ac_boost_block', 0x0000000A, lambda d: knob_scenarios(d, keep=(1,))),
    'dyn_rc_boost': ('dyn/dyn_rc_boost.c', 'dyn_rc_boost_block', 0x0000000C, lambda d: knob_scenarios(d, keep=(1,))),
    'dyn_bb_boost': ('dyn/dyn_bb_boost.c', 'dyn_bb_boost_block', 0x0000000B, lambda d: knob_scenarios(d, keep=(1,))),
    'dyn_fat_boost': ('dyn/dyn_fat_boost.c', 'dyn_fat_boost_block', 0x00000019,
                      lambda d: onoff_scenarios(d, (0, 1, 2), (3,))),
    'dyn_ac_sim': ('dyn/dyn_ac_sim.c', 'dyn_ac_sim_block', 0x01000001, lambda d: clamped(
        knob_scenarios(d, keep=(2,)), 3, 0.0, 3.0) + [('mode %d' % m, d[:3] + [float(m)], [], None, 1.0)
                                                       for m in range(4)] + [
        ('mode moving', d, [(20, {3: 0.0}), (80, {3: 3.0}), (140, {3: 1.0}), (200, {3: 2.0}), (215, {3: 0.0})], None,
         1.0)]),
    'fx_filter': ('fx/fx_filter.c', 'fx_filter_block', 0x01000019, lambda d: sync_scenarios(d, 5, 4)),
    'fx_octaver': ('fx/fx_octaver.c', 'fx_octaver_block', 0x01000021, lambda d: knob_scenarios(d, keep=(2,)) + [
        ('octaves only', [100.0, 100.0, 0.0], [], None, 1.0)]),
    'fx_dual_melody': ('fx/fx_dual_melody.c', 'fx_dual_melody_block', 0x01000023,     # knob word 3 unused
                       lambda d: clamped(clamped(knob_scenarios(d[:3] + [0.0] + d[3:], keep=(2,)), 0, 0.0, 24.0),
                                         1, -24.0, 0.0) + [
                           ('pitch %g / %g' % (h, l), [h, l, 30.0, 0.0, 80.0, 60.0], [], None, 1.0)
                           for h, l in ((7.0, -5.0), (24.0, -24.0), (1.0, -1.0))]),
    'fx_pitch': ('fx/fx_pitch.c', 'fx_pitch_block', 0x01000024, lambda d: clamped(clamped(
        knob_scenarios(d, keep=(3,)), 0, 0.0, 12.0), 1, -12.0, 0.0) + [
        ('pitch %g / %g, range %g' % (h, l, r), [h, l, 70.0, 30.0, r], [], None, 1.0)
        for h, l, r in ((7.0, -5.0, 0.0), (12.0, -12.0, 100.0), (3.0, -2.0, 37.0))]),
    'fx_tape_mod': ('fx/fx_tape_mod.c', 'fx_tape_mod_block', 0x01000033, lambda d: knob_scenarios(d, keep=(2,))),
    'fx_pitch_s': ('fx/fx_pitch_s.c', 'fx_pitch_s_block', 0x01000055, lambda d: clamped(
        knob_scenarios(d, keep=(3,)), 0, 0.0, 5.0) + [
        ('range %d, position %g' % (r, q), [float(r), q, 100.0, 100.0], [], None, 1.0)
        for r, q in ((2, 100.0), (0, 100.0), (4, 30.0), (5, 75.0), (3, 50.0), (1, 100.0))] + [
        ('position moving', [2.0, 0.0, 100.0, 100.0], [(30, {1: 100.0}), (120, {1: 40.0}), (200, {0: 5.0})], None,
         1.0)]),
    'fx_harmony_d': ('fx/fx_harmony_d.c', 'fx_harmony_d_block', 0x0100004E,       # knob word 5 unused
                     lambda d: harmony_scenarios(d[:5] + [0.0] + d[5:])),
    'eq_guitar_1': ('eq/eq_guitar_1.c', 'eq_guitar_1_block', 0x01000035, lambda d: eq_scenarios(d)),
    'eq_guitar_2': ('eq/eq_guitar_2.c', 'eq_guitar_2_block', 0x01000036, lambda d: eq_scenarios(d)),
    'eq_calif': ('eq/eq_calif.c', 'eq_calif_block', 0x0100003C, lambda d: eq_scenarios(d + [0.0])),
    'eq_bass_1': ('eq/eq_bass_1.c', 'eq_bass_1_block', 0x01000039, lambda d: eq_scenarios(d)),
    'eq_bass_2': ('eq/eq_bass_2.c', 'eq_bass_2_block', 0x0100003A, lambda d: eq_scenarios(d)),
    'clone': ('clone/clone.c', 'clone_block', 0x0F000000, lambda d: knob_scenarios(d, keep=(1,))),
    'vol_volume': ('vol_volume.c', 'vol_volume_block', 0x06000003, lambda d: knob_scenarios(d, keep=(0,))),
    'wah_voks': ('wah/wah_voks.c', 'wah_voks_block', 0x05000001, lambda d: knob_scenarios(d, keep=(2,))),
    'wah_cry': ('wah/wah_cry.c', 'wah_cry_block', 0x05000008, lambda d: knob_scenarios(d, keep=(2,))),
    'wah_rack': ('wah/wah_rack.c', 'wah_rack_block', 0x0500000A, lambda d: onoff_scenarios(d, (0, 1, 3), (4,))),
    'wah_touch': ('wah/wah_touch.c', 'wah_touch_block', 0x0100000F, lambda d: onoff_scenarios(d, (0, 1, 2, 3), (4,))),
    'wah_auto': ('wah/wah_auto.c', 'wah_auto_block', 0x01000015, lambda d: clamped(
        sync_scenarios(d, 6, 1, keep=(2,)), 1, 0.1, 10.0)),
    'wah_bass': ('wah/wah_bass.c', 'wah_bass_block', 0x05000007, lambda d: knob_scenarios(d, keep=(2,))),
    'fx_ring_mod': ('fx/fx_ring_mod.c', 'fx_ring_mod_block', 0x0100002F, lambda d: clamped(
        knob_scenarios(d), 2, -50.0, 50.0) + [('fine -50', [50.0, 30.0, -50.0, 80.0], [], None, 1.0)]),
    'dyn_ac_woody': ('dyn/dyn_ac_woody.c', 'dyn_ac_woody_block', 0x01000000, lambda d: knob_scenarios(d) + [
        ('shape %g' % v, [v], [], None, 1.0) for v in (7.0, 33.0, 61.0, 99.0)]),
    'dly_pure': ('delays/dly_pure.c', 'dly_pure_block', 0x0B000000, lambda d: delay_scenarios(d, 3, 1, 4)),
    'mod_tremolo': ('mods/mod_tremolo.c', 'mod_tremolo_block', 0x04000021, lambda d: sync_scenarios(d, 2, 1)),
    'mod_sine_trem': ('mods/mod_sine_trem.c', 'mod_sine_trem_block', 0x04000026,
                      lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'mod_triangle_trem': ('mods/mod_triangle_trem.c', 'mod_triangle_trem_block', 0x04000027,
                          lambda d: sync_scenarios(d, 3, 1, keep=(2,))),
    'mod_bias_trem': ('mods/mod_bias_trem.c', 'mod_bias_trem_block', 0x04000028,
                      lambda d: sync_scenarios(d, 3, 1, keep=(1, 2))),     # Rate 0 + Depth 100: a full cut
    'master_od': ('drives/master_od.c', 'master_od_block', 0x0300000F, lambda d: knob_scenarios(d, keep=(1,))),
}
failures = 0


def check(cond, what):
    global failures
    print('%-5s %s' % ('ok' if cond else 'FAIL', what))
    failures += 0 if cond else 1


def run(cmd, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode:
        sys.exit('failed: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r.stdout


def signal(seconds):
    n = int(seconds * FS) // FRAMES * FRAMES
    p = os.path.join(ROOT, 'out', 'circuits', 'di.f32')
    if os.path.exists(p):
        x = np.fromfile(p, np.float32)[0::2]
        l, r = x[int(3.5 * FS):int(3.5 * FS) + n], x[int(6.0 * FS):int(6.0 * FS) + n]
        if len(l) == n and len(r) == n:
            return np.stack([l, r], 1).astype(np.float32)
    t = np.arange(n) / FS
    l = sum(0.3 / h * np.exp(-(t % 0.5) * (2 + h)) * np.sin(2 * np.pi * 110.0 * h * t) for h in range(1, 12))
    return np.stack([l, np.roll(l, 3000) * 0.7], 1).astype(np.float32)


def knob_plan(nblocks, knobs, moves):
    """the knob vector of each audio block"""
    k = list(knobs) + [0.0] * (15 - len(knobs))
    plan, at = [], dict(moves)
    for b in range(nblocks):
        for i, v in at.get(b, {}).items():
            k[i] = v
        plan.append(list(k))
    return plan


def run_stock(elf, x, plan, bypass, data=None):
    from matribox.module import effemu as E
    e = E.Emu(elf)
    uc = e.uc
    K, BUF, AUX, EN, BEAT, EXTRA, CTX, X1, X2 = (E.SCRATCH + o for o in
                                                (0x0, 0x1000, 0x2000, 0x3000, 0x3010, 0x3020, 0x3100, 0x4000, 0x5000))
    uc.mem_write(K, struct.pack('<15f', *plan[0]))
    uc.mem_write(BEAT, struct.pack('<I', 500))
    uc.mem_write(EXTRA, struct.pack('<II', X1, X2))
    # Init's 2nd argument: the tempo word, as Process's; for an item's data (a Clone capture) its address
    word = 500
    if data is not None:
        word = E.SCRATCH + 0x80000
        uc.mem_write(word, data)
    uc.mem_write(CTX, struct.pack('<I', word) + b'\0' * 60)
    e.call(e.init, K, CTX)
    out = np.zeros_like(x)
    for b in range(len(plan)):
        blk = x[b * FRAMES:(b + 1) * FRAMES]
        uc.mem_write(K, struct.pack('<15f', *plan[b]))
        uc.mem_write(EN, bytes([0 if bypass and bypass[0] <= b < bypass[1] else 1]))
        uc.mem_write(BUF, blk.tobytes())
        uc.mem_write(AUX, blk.tobytes())
        e.call(e.process, BUF, FRAMES, K, AUX, EN, BEAT, EXTRA)
        out[b * FRAMES:(b + 1) * FRAMES] = np.frombuffer(bytes(uc.mem_read(BUF, 8 * FRAMES)), np.float32).reshape(-1, 2)
    return out


def run_arm(elf, x, plan, bypass):
    import wvfx_emu
    r = wvfx_emu.Runner(elf)
    if r.start(plan[0]):
        sys.exit('init failed')
    out = np.zeros_like(x)
    for b in range(len(plan)):
        blk = x[b * FRAMES:(b + 1) * FRAMES].reshape(-1)
        en = not (bypass and bypass[0] <= b < bypass[1])
        y, _, _ = r.block([float(v) for v in blk], plan[b], enabled=en)
        out[b * FRAMES:(b + 1) * FRAMES] = np.array(y, np.float32).reshape(-1, 2)
    return out


def run_pc(exe, x, knobs, moves, bypass, nparams, tag):
    fi, fo = os.path.join(OUT, tag + '_in.f32'), os.path.join(OUT, tag + '_pc.f32')
    x.tofile(fi)
    env = dict(os.environ)
    env.pop('WVFX_KNOBS_AT', None)
    env.pop('WVFX_BYPASS', None)
    env['WVFX_WORDS'] = ','.join('%.9g' % v for v in knobs)    # the words by index, as the module gets them
    if moves:
        env['WVFX_KNOBS_AT'] = ';'.join('%d:%s' % (b, ','.join('%d=%.9g' % kv for kv in m.items())) for b, m in moves)
    if bypass:
        env['WVFX_BYPASS'] = '%d-%d' % bypass
    run([exe, fi, fo] + ['%.9g' % v for v in knobs[:nparams]], env)
    return np.fromfile(fo, np.float32).reshape(-1, 2)


def cost(stock_elf, elf, x, knobs, warm=4, blocks=8):
    """instructions per 64-frame block (median), the original's module and the port, at `knobs`"""
    from unicorn import UC_HOOK_CODE
    from matribox.module import effemu as E
    import wvfx_emu
    k = list(knobs) + [0.0] * (15 - len(knobs))
    e = E.Emu(stock_elf)
    uc = e.uc
    K, BUF, AUX, EN, BEAT, EXTRA, CTX = (E.SCRATCH + o for o in (0x0, 0x1000, 0x2000, 0x3000, 0x3010, 0x3020, 0x3100))
    uc.mem_write(K, struct.pack('<15f', *k))
    uc.mem_write(EN, b'\x01')
    uc.mem_write(EXTRA, struct.pack('<II', E.SCRATCH + 0x4000, E.SCRATCH + 0x5000))
    uc.mem_write(CTX, struct.pack('<I', 500) + b'\0' * 60)    # Init's 2nd argument: the tempo word, as Process's
    # one counting hook from the start, switched by a flag: a hook added after code ran misses cached blocks
    n, on = [0], [False]

    def tick(*_):
        if on[0]:
            n[0] += 1
    uc.hook_add(UC_HOOK_CODE, tick)
    e.call(e.init, K, CTX)
    counts = []
    for b in range(warm + blocks):
        uc.mem_write(BUF, x[b * FRAMES:(b + 1) * FRAMES].tobytes())
        n[0], on[0] = 0, b >= warm
        e.call(e.process, BUF, FRAMES, K, AUX, EN, BEAT, EXTRA)
        on[0] = False
        if b >= warm:
            counts.append(n[0])
    r = wvfx_emu.Runner(elf)
    r.uc.hook_add(UC_HOOK_CODE, tick)
    r.start(k)
    mine = []
    for b in range(warm + blocks):
        blk = [float(v) for v in x[b * FRAMES:(b + 1) * FRAMES].reshape(-1)]
        n[0], on[0] = 0, b >= warm
        r.block(blk, k)
        on[0] = False
        if b >= warm:
            mine.append(n[0])
    return sorted(counts)[len(counts) // 2], sorted(mine)[len(mine) // 2]


def tone_block(b, phase, t, freq, level, count):
    """emu.Waveod.audio_block with the sine counted from the tone's start (sample t * 64), so that every run gets
    the same input to the bit wherever its blocks fall"""
    import emu
    uc = b.uc
    half = phase & 1
    frames = []
    for i in range(64):
        v = int(level * math.sin(2 * math.pi * freq * (t * 64 + i) / FS) * 980586112)
        frames += [-v, 0, 0, v, 0, 0]
    uc.mem_write(b.sym('rx_buf') + half * 64 * 6 * 4, struct.pack('<384i', *frames))
    b.regs[emu.DMA_INT] = 1
    b.regs[emu.DMA_CITER0 & ~3] = (b.regs.get(emu.DMA_CITER0 & ~3, 0) & 0xffff) | ((700 if half else 100) << 16)
    b.in_block = 1                                   # the block's cycle count as emu.Board.audio_block keeps it
    try:
        n = b.call(b.sym('dma0_dma16_irq'), count_instructions=count)
    finally:
        b.in_block = 0
    out = struct.unpack('<256i', uc.mem_read(b.sym('tx_buf') + half * 64 * 4 * 4, 1024))
    return n, out


def play(cb, w, p, index, blocks=80, freq=196.0, level=0.3):
    """load preset `index` after the empty one, let it settle, then the tone: (left, right, median instructions)"""
    p.load(cb.EMPTY)
    p.load(index)
    b = w.b
    base = ((w.phase + cb.WARM_BLOCKS) // 50 + 1) * 50
    for ph in range(w.phase, base):
        b.audio_block(ph, level=0.0)
        b.run_ms(1.4512)
    left, right, cost = [], [], []
    for i in range(blocks):
        n, out = tone_block(b, base + i, i, freq, level, True)
        cost.append(n)
        left += out[0::4]
        right += out[2::4]
        b.run_ms(1.4512)
    w.phase = base + blocks
    if b.faults:
        sys.exit('FAULT: ' + '; '.join(b.faults))
    return left, right, sorted(cost)[len(cost) // 2]


def firmware(fw, elf_fw, ports):
    """the ports installed over USB in the emulated firmware as waveod Studio does; each preset played with the
    original's module and with the port in its place: the DAC words identical"""
    import types
    import check_blocks as cb
    import check_modfx as cm
    from waveod_studio import wvlib
    lib = os.path.join(OUT, 'stock_lib.bin')
    run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'lib', lib] + [e for _, e, _ in ports])
    img = open(lib, 'rb').read()
    mods = [wvlib.describe(open(e, 'rb').read()) for _, e, _ in ports]
    w = cb.ce.Waveod(fw, elf_fw)
    p = cb.Pedal(transport=cb.Transport(w), timeout=120)
    _, data = p.get(0)
    base = cb.empty(data)
    p.put(cb.EMPTY, base)
    p.write_library(img)
    got = p.library()
    check([m.id for m in got] == [m.id for m in mods] and got.pack() == img,
          'installed over USB: %d modules, %d bytes, read back byte for byte' % (len(got), len(img)))
    play(cb, w, p, cb.EMPTY)    # the session's first play differs from the next ones whatever plays (start-up ramps)
    for (name, _, entry), m in zip(ports, mods):
        stock = types.SimpleNamespace(id=entry.id, name=entry.name, params=[
            {'knob': q['idx'], 'default': q['default']} for q in entry.params])
        for tag, pre, knobs in (('defaults', base, None), ('gain 90, bass 80, treble 20', base, {0: 90, 3: 80, 5: 20}),
                                ('after 01A', data, None)):
            p.put(30, cm.with_block(pre, stock, 4, knobs))
            p.put(31, cm.with_block(pre, m, m.menu, knobs))
            names = ([b.name for b in p.describe(30)[1]][-1:], [b.name for b in p.describe(31)[1]][-1:])
            l0, r0, c0 = play(cb, w, p, 30)
            l1, r1, c1 = play(cb, w, p, 31)
            rms = math.sqrt(sum(float(v) * v for v in l1) / len(l1)) / 2 ** 31
            same = l0 == l1 and r0 == r1
            check(same and rms > 1e-4 and names == ([entry.name], [m.name]),
                  '%-8s %-28s %s; %s %.1f dBFS; %d / %d instructions per block' % (
                      name, tag, 'original = port, every DAC word' if same else 'DIFFERS',
                      names, 20 * math.log10(rms + 1e-30), c0, c1))
    if w.b.faults:
        check(False, 'faults: ' + '; '.join(w.b.faults))


def compare(ref, y):
    if np.array_equal(ref.view(np.uint32), y.view(np.uint32)):
        return True, 'bit for bit'
    bad = np.nonzero(np.any(ref.view(np.uint32) != y.view(np.uint32), axis=1))[0]
    err = np.sqrt(np.mean((ref.astype(np.float64) - y) ** 2))
    lvl = np.sqrt(np.mean(ref.astype(np.float64) ** 2)) or 1.0
    return False, 'DIFFERS from frame %d (block %d), %d frames, error %.1f dB' % (
        bad[0], bad[0] // FRAMES, len(bad), 20 * math.log10(max(err / lvl, 1e-30)))


DATA_BLOCKS = {'clone'}                             # blocks that play an item's data (--data): run once per file


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('stock')
    ap.add_argument('blocks', nargs='*')
    ap.add_argument('--seconds', type=float, default=1.5)
    ap.add_argument('--cost', action='store_true', help='also the instructions per block, the original and the port')
    ap.add_argument('--fw', help='a waveod firmware (tools/mkfw.py): the ports installed over USB in the emulated '
                                 'firmware, each preset played with the original module and with the port')
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'waveod.elf'))
    ap.add_argument('--no-unicorn', action='store_true', help='with --fw: skip the scenarios against the module')
    ap.add_argument('--data', action='append', default=[],
                    help='an item\'s data for the blocks that play one (a Clone capture, 0x2000 bytes); repeatable')
    a = ap.parse_args()
    from matribox.firmware import fwtool
    lib = fwtool.Firmware(open(a.stock, 'rb').read()).library
    by_id = {e.id: e for e in lib.entries}
    os.makedirs(OUT, exist_ok=True)
    x = signal(a.seconds)
    nblocks = len(x) // FRAMES
    ports = []
    for name in a.blocks or list(BLOCKS):
        src, desc, eid, scen = BLOCKS[name]
        entry = by_id[eid]
        path = os.path.join(ROOT, 'src', 'blocks', src)
        elf = os.path.join(OUT, name + '.elf')
        run([sys.executable, os.path.join(HERE, 'wvfx.py'), 'build', path, '-o', elf])
        exe = os.path.join(OUT, name + '_host.exe')
        wvbuild.host_compile(exe, [os.path.join(HERE, 'circuits', 'wvfx_host.c')],
                             ['-I', os.path.join(ROOT, 'src'), '-DWVFX_SRC="%s"' % path.replace('\\', '/'),
                              '-DWVFX_DESC=%s' % desc])
        defaults = [p['default'] for p in sorted(entry.params, key=lambda p: p['idx'])]
        ports.append((name, elf, entry))
        sets = [(None, '')]
        if name in DATA_BLOCKS:                         # one run of the scenarios per item's data given
            if not a.data:
                print('skip  %-8s (no --data: it plays an item\'s data, a capture)' % name)
                sets = []
            else:
                sets = [(p, os.path.splitext(os.path.basename(p))[0] + ' ') for p in a.data]
        for data_path, prefix in sets:
            if data_path:
                os.environ['WVFX_DATA'] = os.path.abspath(data_path)
            else:
                os.environ.pop('WVFX_DATA', None)
            data = open(data_path, 'rb').read() if data_path else None
            for tag, knobs, moves, bypass, gain in ([] if a.fw and a.no_unicorn else scen(defaults)):
                xs = (x * gain).astype(np.float32)
                plan = knob_plan(nblocks, knobs, moves)
                ref = run_stock(entry.elf, xs, plan, bypass, data)
                arm = run_arm(elf, xs, plan, bypass)
                pc = run_pc(exe, xs, knobs, moves, bypass, len(entry.params), name)
                ok1, m1 = compare(ref, arm)
                ok2, m2 = compare(ref, pc)
                lvl = 20 * math.log10(max(float(np.sqrt(np.mean(ref.astype(np.float64) ** 2))), 1e-30))
                check(ok1 and ok2 and lvl > -90.0, '%-8s %-30s out %6.1f dBFS; module: %s; PC: %s' % (
                    name, prefix + tag, lvl, m1, m2))
        os.environ.pop('WVFX_DATA', None)
        if a.cost:
            c0, c1 = cost(entry.elf, elf, x, defaults)
            print('      %-8s instructions per block at the defaults: original %d, port %d (%+.0f %%)' % (
                name, c0, c1, 100.0 * (c1 - c0) / c0))
    if a.fw:
        firmware(a.fw, a.elf, ports)
    print('%d failure(s)' % failures)
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
