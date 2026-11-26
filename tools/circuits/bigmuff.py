"""Big Muff Pi from its circuit (tools/circuits/circuit.py): operating point, small-signal responses and the
reference transient the real-time model (src/blocks/circuits/bigmuff.h, the Dark Muff blocks) is checked against;
from research/pulse (its NOTES.md sections 13 and 15 tell how the stages were cut).

    python bigmuff.py op [variant]                       DC operating point
    python bigmuff.py ac [variant] [sustain tone volume] small-signal response, input to output and per stage
    python bigmuff.py thd [variant] [sustain tone]       harmonics of a sine against its level (reference transient)

Variants (component values; Kit Rae's tracings, bigmuffpage.com):
  rams73  Version 2 "Ram's Head", 1973 #3 (circle-face / red / black / blue ink; Marveltone Distortion Sustainer):
          FS36999 (2N5133) NPNs, 1N914-type silicon diodes, linear pots. Kit Rae: the closest to the Muff Gilmour
          used live, solo and with Pink Floyd; very close to the Cornish P-1.

Stages (the drawing's names; signal order Q4 > Q3 > Q2 > Q1):
  IN > R2 33k > C1 100n > Q4 base: 100k to ground, 470k || 470p to the collector, collector 10k to +9, emitter 100
  > C4 100n > SUSTAIN 100k (lin) > 560 to ground; wiper > C5 100n > R19 10k > Q3 base
  Q3, Q2: base 100k to ground, 470k || 470p || (100n + two antiparallel 1N914) to the collector, 10k, emitter 100;
          Q3 > C13 100n > R12 10k > Q2 base
  Q2 collector > tone stack: R8 33k > C8 12n to ground (low-pass leg) and C9 4n > R5 33k to ground (high-pass leg),
          TONE 100k between the legs, wiper > C3 100n > Q1 base: 470k to +9, 100k to ground, collector 10k, emitter 2k7
  > C2 100n > VOLUME 100k (lin) to ground; wiper = OUT
"""
import sys
import numpy as np
from circuit import Circuit, VT

VARIANTS = {
    'rams73': dict(
        r_in=33e3, c_in=100e-9,
        q4=dict(rb=100e3, rf=470e3, cf=470e-12, rc=10e3, re=100.0),
        c4=100e-9, sustain=100e3, r_sus=560.0, c5=100e-9,
        r19=10e3, q3=dict(rb=100e3, rf=470e3, cf=470e-12, cd=100e-9, rc=10e3, re=100.0),
        c13=100e-9, r12=10e3, q2=dict(rb=100e3, rf=470e3, cf=470e-12, cd=100e-9, rc=10e3, re=100.0),
        r8=33e3, c8=12e-9, c9=4e-9, r5=33e3, tone=100e3, c3=100e-9,
        q1=dict(r_up=470e3, r_dn=100e3, rc=10e3, re=2.7e3),
        c2=100e-9, volume=100e3),
}
VCC = 9.0
R_SRC = 1e3                                 # what drives it (a buffer / the previous pedal's output)
R_LOAD = 1e6                                # the amp's input
BF = 250.0                                  # 2N5133: hFE 60..1000, Muffs are usually built from 100..500 parts


def build(variant='rams73', sustain=0.7, tone=0.5, volume=0.5, r_src=R_SRC, bf=BF):
    p = VARIANTS[variant]
    c = Circuit()
    c.src('vcc', 1.0, VCC)                  # battery (1 ohm inside)
    c.src('in', r_src, 0.0, signal=True)
    # Q4, the input booster
    c.r('in', 'n1', p['r_in']); c.c('n1', 'b4', p['c_in'])
    s = p['q4']
    c.r('b4', '0', s['rb']); c.r('b4', 'c4', s['rf']); c.c('b4', 'c4', s['cf'])
    c.r('vcc', 'c4', s['rc']); c.q('c4', 'b4', 'e4', bf=bf); c.r('e4', '0', s['re'])
    # sustain
    c.c('c4', 'st', p['c4'])
    c.pot('sb', 'sw', 'st', p['sustain'], sustain, 'sustain')         # full up: the wiper at the top
    c.r('sb', '0', p['r_sus'])
    c.c('sw', 'n3', p['c5']); c.r('n3', 'b3', p['r19'])
    # Q3, Q2: the clipping stages

    def clip(b, col, e, d, s):
        c.r(b, '0', s['rb']); c.r(b, col, s['rf']); c.c(b, col, s['cf'])
        c.c(b, d, s['cd']); c.d(d, col); c.d(col, d)
        c.r('vcc', col, s['rc']); c.q(col, b, e, bf=bf); c.r(e, '0', s['re'])
    clip('b3', 'c3', 'e3', 'd3', p['q3'])
    c.c('c3', 'n2', p['c13']); c.r('n2', 'b2', p['r12'])
    clip('b2', 'c2', 'e2', 'd2', p['q2'])
    # tone stack
    c.r('c2', 'tl', p['r8']); c.c('tl', '0', p['c8'])
    c.c('c2', 'th', p['c9']); c.r('th', '0', p['r5'])
    c.pot('tl', 'tw', 'th', p['tone'], tone, 'tone')        # tone 0: the wiper on the low-pass leg
    c.c('tw', 'b1', p['c3'])
    # Q1, the recovery stage
    s = p['q1']
    c.r('vcc', 'b1', s['r_up']); c.r('b1', '0', s['r_dn'])
    c.r('vcc', 'c1', s['rc']); c.q('c1', 'b1', 'e1', bf=bf); c.r('e1', '0', s['re'])
    c.c('c1', 'vt', p['c2'])
    c.pot('0', 'out', 'vt', p['volume'], volume, 'volume')
    c.r('out', '0', R_LOAD)
    return c


def build_staged(variant='rams73', sustain=0.7, tone=0.5, volume=0.5, r_src=R_SRC, bf=BF, loads=None):
    """the pedal cut into stages joined by ideal buffers at the collectors (c4, c3, c2): each stage drives a copy
    of the next coupling network as its load, with the next base grounded (it is a near virtual ground): the
    experiment behind the real-time model's stages (research/pulse/NOTES.md 13). loads per cut: 'net' (that copy), 'lin' (the next
    stage itself, linearised), 'R' (the copy without its coupling capacitors) or a resistance in ohms"""
    loads = loads or ('net', 'net', 'net')
    p = VARIANTS[variant]
    c = Circuit()
    c.src('vcc', 1.0, VCC)
    c.src('in', r_src, 0.0, signal=True)

    def gain_stage(b, col, e, s, diodes=None):
        c.r(b, '0', s['rb']); c.r(b, col, s['rf']); c.c(b, col, s['cf'])
        if diodes:
            c.c(b, diodes, s['cd']); c.d(diodes, col); c.d(col, diodes)
        c.r('vcc', col, s['rc']); c.q(col, b, e, bf=bf); c.r(e, '0', s['re'])

    def sustain_net(src, tag, base):
        c.c(src, 'st' + tag, p['c4'])
        c.pot('sb' + tag, 'sw' + tag, 'st' + tag, p['sustain'], sustain, None if tag else 'sustain')
        c.r('sb' + tag, '0', p['r_sus'])
        c.c('sw' + tag, 'n3' + tag, p['c5']); c.r('n3' + tag, base, p['r19'])

    def inter_net(src, tag, base):
        c.c(src, 'n2' + tag, p['c13']); c.r('n2' + tag, base, p['r12'])

    def tone_net(src, tag, base):
        c.r(src, 'tl' + tag, p['r8']); c.c('tl' + tag, '0', p['c8'])
        c.c(src, 'th' + tag, p['c9']); c.r('th' + tag, '0', p['r5'])
        c.pot('tl' + tag, 'tw' + tag, 'th' + tag, p['tone'], tone, None if tag else 'tone')
        c.c('tw' + tag, base, p['c3'])

    def load(k, net, src):
        if loads[k] == 'lin':                  # the next stage itself as the load, linearised, without its diodes
            nxt = {sustain_net: p['q3'], inter_net: p['q2']}[net]
            net(src, '_L', 'bL' + str(k))
            gain_stage('bL' + str(k), 'cL' + str(k), 'eL' + str(k), nxt)
            c.linearize(len(c.Q) - 1)
            return
        if loads[k] == 'R':                    # that copy without its coupling capacitors (no states)
            c.c(src, 'ac' + str(k), 1.0)        # (an AC load: the real-time model works on deviations)
            if net is sustain_net:
                c.pot('sb_R', 'sw_R', 'ac0', p['sustain'], sustain); c.r('sb_R', '0', p['r_sus']); c.r('sw_R', '0', p['r19'])
            else:
                c.r('ac1', '0', p['r12'])
            return
        if loads[k] == 'net':
            net(src, '_L', '0' if net is not tone_net else 'b1_L')
            if net is tone_net:
                c.r('b1_L', '0', 72e3)           # Q1's base: 470k || 100k || (bf + 1) 2k7
        else:
            c.r(src, '0', float(loads[k]))
    # S1: input network and Q4
    c.r('in', 'n1', p['r_in']); c.c('n1', 'b4', p['c_in'])
    gain_stage('b4', 'c4', 'e4', p['q4'])
    load(0, sustain_net, 'c4')
    c.buf('c4', 'c4b')
    # S2: sustain network and Q3
    sustain_net('c4b', '', 'b3')
    gain_stage('b3', 'c3', 'e3', p['q3'], 'd3')
    load(1, inter_net, 'c3')
    c.buf('c3', 'c3b')
    # S3: Q2
    inter_net('c3b', '', 'b2')
    gain_stage('b2', 'c2', 'e2', p['q2'], 'd2')
    load(2, tone_net, 'c2')
    c.buf('c2', 'c2b')
    # S4: tone stack, Q1, volume (linear)
    tone_net('c2b', '', 'b1')
    s = p['q1']
    c.r('vcc', 'b1', s['r_up']); c.r('b1', '0', s['r_dn'])
    c.r('vcc', 'c1', s['rc']); c.q('c1', 'b1', 'e1', bf=bf); c.r('e1', '0', s['re'])
    c.c('c1', 'vt', p['c2'])
    c.pot('0', 'out', 'vt', p['volume'], volume, 'volume')
    c.r('out', '0', R_LOAD)
    return c


NODES = [('b4', 'c4', 'e4', 'Q4'), ('b3', 'c3', 'e3', 'Q3'), ('b2', 'c2', 'e2', 'Q2'), ('b1', 'c1', 'e1', 'Q1')]


def cmd_op(variant):
    c = build(variant)
    v = c.dc()
    for b, col, e, name in NODES:
        rc = 10e3
        ic = (VCC - v[col]) / rc
        print(f'{name}: Vb {v[b]:.3f}  Vc {v[col]:.3f}  Ve {v[e]:.3f}  Ic {ic * 1e3:.3f} mA  '
              f'headroom to cutoff {VCC - v[col]:.2f} V, to saturation {v[col] - v[e] - 0.2:.2f} V')


def cmd_ac(variant, sustain=0.7, tone=0.5, volume=0.5):
    f = np.geomspace(20, 20000, 31)
    c = build(variant, sustain, tone, volume)
    c.dc()
    print(f'sustain {sustain} tone {tone} volume {volume}: small-signal gain (dB), input to each point')
    pts = ['c4', 'b3', 'c3', 'c2', 'tw', 'c1', 'out']
    H = {p: 20 * np.log10(np.abs(c.ac(f, p)) + 1e-30) for p in pts}
    print('   Hz ' + ''.join(f'{p:>8}' for p in pts))
    for i, fr in enumerate(f):
        print(f'{fr:6.0f} ' + ''.join(f'{H[p][i]:8.1f}' for p in pts))


def harmonics(y, f0, fs, n=9):
    w = np.hanning(len(y))
    Y = np.abs(np.fft.rfft(y * w))
    k0 = f0 * len(y) / fs
    return np.array([Y[int(round(k0 * h))] for h in range(1, n + 1)])


def cmd_thd(variant, sustain=0.7, tone=0.5):
    fs = 44100 * 8
    f0 = 220.0
    c = build(variant, sustain, tone, 0.5)
    c.dc()
    print(f'sustain {sustain} tone {tone}: {f0:.0f} Hz sine, harmonics re the fundamental (dB) at the output')
    for amp in (0.003, 0.01, 0.03, 0.1, 0.3, 1.0):
        t = np.arange(int(fs * 0.25)) / fs
        x = amp * np.sin(2 * np.pi * f0 * t)
        y = c.tran(x, fs, ['out', 'c4', 'c3', 'c2'])
        seg = slice(int(fs * 0.15), len(t))
        n = (len(t) - int(fs * 0.15))
        n -= n % int(fs / f0)
        seg = slice(len(t) - n, len(t))
        h = harmonics(y[seg, 0], f0, fs)
        hd = 20 * np.log10(h[1:] / h[0] + 1e-12)
        pk = np.max(np.abs(y[seg]), axis=0)
        print(f'  in {amp:5.3f} V: out {pk[0]:.3f} V pk (fund {20 * np.log10(h[0] * 2 / np.sum(np.hanning(n)) / amp):+.1f} dB)  '
              + ' '.join(f'H{i + 2} {d:6.1f}' for i, d in enumerate(hd[:6]))
              + f'   pk c4 {pk[1]:.2f} c3 {pk[2]:.2f} c2 {pk[3]:.2f}')


R_TIE = 1e-3                                # the stages' inputs: voltage sources (1 milliohm)


def stages(variant='rams73', bf=BF, r_src=R_SRC):
    """the real-time model's three stages (src/blocks/circuits/bigmuff_<variant>.h). Each one is solved exactly; they meet at
    the collectors of Q4 and Q3: a stage drives a copy of the next coupling network (C4 / sustain / C5 / R19, or
    C13 / R12) as its load, ending on the next base, whose voltage comes back from the next stage (the last sample's,
    extrapolated: a weak loop, gain ~0.06). Q2 and Q1 are linearised.
      S1: input network, Q4 (junctions)         in: u, b3      out: c4
      S2: sustain network, Q3 (junctions), D3   in: c4, b2     out: c3, b3
      S3: Q2 (linear), D2, the tone stack as its load       in: c3      out: c2, b2
      S4: tone stack, Q1 (linear), volume: linear, at 1x   in: c2      out: out"""
    p = VARIANTS[variant]
    full = build(variant, bf=bf, r_src=r_src)
    v = full.dc()

    def gain_stage(c, b, col, e, s, diodes=None):
        c.r(b, '0', s['rb']); c.r(b, col, s['rf']); c.c(b, col, s['cf'])
        if diodes:
            c.c(b, diodes, s['cd']); c.d(diodes, col); c.d(col, diodes)
        c.r('vcc', col, s['rc']); c.q(col, b, e, bf=bf); c.r(e, '0', s['re'])

    def sustain_net(c, src, tag, base):
        c.c(src, 'st' + tag, p['c4'])
        c.pot('sb' + tag, 'sw' + tag, 'st' + tag, p['sustain'], 0.7, 'sustain')
        c.r('sb' + tag, '0', p['r_sus'])
        c.c('sw' + tag, 'n3' + tag, p['c5']); c.r('n3' + tag, base, p['r19'])

    def inter_net(c, src, tag, base):
        c.c(src, 'n2' + tag, p['c13']); c.r('n2' + tag, base, p['r12'])

    s1 = Circuit(); s1.src('vcc', 1.0, VCC)
    s1.src('in', r_src, 0.0, signal=True)
    s1.r('in', 'n1', p['r_in']); s1.c('n1', 'b4', p['c_in'])
    gain_stage(s1, 'b4', 'c4', 'e4', p['q4'])
    s1.src('b3x', R_TIE, v['b3'], signal=True)
    sustain_net(s1, 'c4', '_L', 'b3x')

    s2 = Circuit(); s2.src('vcc', 1.0, VCC)
    s2.src('c4x', R_TIE, v['c4'], signal=True)
    sustain_net(s2, 'c4x', '', 'b3')
    gain_stage(s2, 'b3', 'c3', 'e3', p['q3'], 'd3')
    s2.src('b2x', R_TIE, v['b2'], signal=True)   # (the copy ending on ground instead: -36..-54 dB)
    inter_net(s2, 'c3', '_L', 'b2x')

    def tone_net(c, src, base):
        c.r(src, 'tl', p['r8']); c.c('tl', '0', p['c8'])
        c.c(src, 'th', p['c9']); c.r('th', '0', p['r5'])
        c.pot('tl', 'tw', 'th', p['tone'], 0.5, 'tone')

    s3 = Circuit(); s3.src('vcc', 1.0, VCC)
    s3.src('c3x', R_TIE, v['c3'], signal=True)
    inter_net(s3, 'c3x', '', 'b2')
    gain_stage(s3, 'b2', 'c2', 'e2', p['q2'], 'd2')
    q = p['q1']
    tone_net(s3, 'c2', None)                  # the tone stack as Q2's load (a resistance instead: -30..-43 dB),
    s3.short('tw', 'b1L')                     # C3 taken as a short into Q1's base
    s3.r('b1L', '0', 1.0 / (1.0 / q['r_up'] + 1.0 / q['r_dn'] + 1.0 / ((bf + 1.0) * q['re'])))

    s4 = Circuit(); s4.src('vcc', 1.0, VCC)   # the linear tail, at 1x in the block
    s4.src('c2x', R_TIE, v['c2'], signal=True)
    tone_net(s4, 'c2x', None)
    s4.c('tw', 'b1', p['c3'])
    s4.r('vcc', 'b1', q['r_up']); s4.r('b1', '0', q['r_dn'])
    s4.r('vcc', 'c1', q['rc']); s4.q('c1', 'b1', 'e1', bf=bf); s4.r('e1', '0', q['re'])
    s4.c('c1', 'vt', p['c2'])
    s4.pot('0', 'out', 'vt', p['volume'], 0.5, 'volume')
    s4.r('out', '0', R_LOAD)
    return [(s1, (0,), ('c4',)), (s2, (0,), ('c3', 'b3')), (s3, (), ('c2', 'b2')), (s4, (), ('out',))], v


import os

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'blocks', 'circuits')
LITE = os.environ.get('BM_LITE', 'l2')      # the Lite cuts (research/pulse/NOTES.md 15); q4 / q3 keep that transistor's junctions, l2 / l3 the copies


def stages_lite(variant='rams73', bf=BF, r_src=R_SRC, cfg=None):
    """the Lite model's stages (src/blocks/circuits/bigmuff_<variant>_lite.h): the same circuit, cheaper cuts. No coupling back
    between the stages (each load copy ends on ground); Q4 linear (so S1 runs at 1x, before the oversampling) and Q3
    linear unless cfg holds 'q4' / 'q3'; the loads of Q3 and Q2 as AC resistances unless cfg holds 'l2' / 'l3' (the
    copies of C13 / R12 and of the tone stack)."""
    cfg = LITE if cfg is None else cfg
    p = VARIANTS[variant]
    v = build(variant, bf=bf, r_src=r_src).dc()

    def gain_stage(c, b, col, e, s, diodes=None):
        c.r(b, '0', s['rb']); c.r(b, col, s['rf']); c.c(b, col, s['cf'])
        if diodes:
            c.c(b, diodes, s['cd']); c.d(diodes, col); c.d(col, diodes)
        c.r('vcc', col, s['rc']); c.q(col, b, e, bf=bf); c.r(e, '0', s['re'])

    def sustain_net(c, src, tag, base):
        c.c(src, 'st' + tag, p['c4'])
        c.pot('sb' + tag, 'sw' + tag, 'st' + tag, p['sustain'], 0.7, 'sustain')
        c.r('sb' + tag, '0', p['r_sus'])
        c.c('sw' + tag, 'n3' + tag, p['c5']); c.r('n3' + tag, base, p['r19'])

    def tone_net(c, src):
        c.r(src, 'tl', p['r8']); c.c('tl', '0', p['c8'])
        c.c(src, 'th', p['c9']); c.r('th', '0', p['r5'])
        c.pot('tl', 'tw', 'th', p['tone'], 0.5, 'tone')

    q = p['q1']
    r_b1 = 1.0 / (1.0 / q['r_up'] + 1.0 / q['r_dn'] + 1.0 / ((bf + 1.0) * q['re']))
    s1 = Circuit(); s1.src('vcc', 1.0, VCC)
    s1.src('in', r_src, 0.0, signal=True)
    s1.r('in', 'n1', p['r_in']); s1.c('n1', 'b4', p['c_in'])
    gain_stage(s1, 'b4', 'c4', 'e4', p['q4'])
    sustain_net(s1, 'c4', '_L', '0')

    s2 = Circuit(); s2.src('vcc', 1.0, VCC)
    s2.src('c4x', R_TIE, v['c4'], signal=True)
    sustain_net(s2, 'c4x', '', 'b3')
    gain_stage(s2, 'b3', 'c3', 'e3', p['q3'], 'd3')
    if 'l2' in cfg:
        s2.c('c3', 'n2_L', p['c13']); s2.r('n2_L', '0', p['r12'])
    else:                                       # Q2's input: R12 into its near virtual ground
        s2.short('c3', 'n2_L'); s2.r('n2_L', '0', p['r12'])

    s3 = Circuit(); s3.src('vcc', 1.0, VCC)
    s3.src('c3x', R_TIE, v['c3'], signal=True)
    s3.c('c3x', 'n2', p['c13']); s3.r('n2', 'b2', p['r12'])
    gain_stage(s3, 'b2', 'c2', 'e2', p['q2'], 'd2')
    if 'l3' in cfg:
        tone_net(s3, 'c2'); s3.short('tw', 'b1L'); s3.r('b1L', '0', r_b1)
    else:                                       # the tone stack's input impedance, ~R8 || (C9 + R5) at the mids
        s3.short('c2', 'ts'); s3.r('ts', '0', float(os.environ.get('BM_LITE_RTS', 22e3)))

    s4 = Circuit(); s4.src('vcc', 1.0, VCC)     # the linear tail, at 1x, as in the full model
    s4.src('c2x', R_TIE, v['c2'], signal=True)
    tone_net(s4, 'c2x')
    s4.c('tw', 'b1', p['c3'])
    s4.r('vcc', 'b1', q['r_up']); s4.r('b1', '0', q['r_dn'])
    s4.r('vcc', 'c1', q['rc']); s4.q('c1', 'b1', 'e1', bf=bf); s4.r('e1', '0', q['re'])
    s4.c('c1', 'vt', p['c2'])
    s4.pot('0', 'out', 'vt', p['volume'], 0.5, 'volume')
    s4.r('out', '0', R_LOAD)
    return [(s1, (0,) if 'q4' in cfg else (), ('c4',)), (s2, (0,) if 'q3' in cfg else (), ('c3',)),
            (s3, (), ('c2',)), (s4, (), ('out',))], v


def dk_header_lite(variant='rams73', bf=BF, cfg=None):
    cfg = LITE if cfg is None else cfg
    sts, v = stages_lite(variant, bf, cfg=cfg)
    out = [f'/* Big Muff Pi "{variant}", the Lite model, for src/sdk/wvdk.h: generated by tools/circuits/bigmuff.py clite {variant},',
           f' * do not edit by hand. Stages: see bigmuff.stages_lite (cfg "{cfg}"); knobs: ' +
           ', '.join(f'{k} {i}' for i, k in enumerate(KNOBS)) + f' (rotations 0..1). bf {bf:g}. */',
           f'#define BML_Q4_NONLINEAR {int("q4" in cfg)}']
    for k, (c, nonlin, outs) in enumerate(sts):
        c.dc()
        out.append(c.dk_header(f'BML_{variant.upper()}_S{k + 1}', nonlin, KNOBS, outs, forward_only=(0,) if k == 1 else ()))
    return '\n'.join(out)


def cmd_clite(variant):
    path = os.path.join(OUT_DIR, f'bigmuff_{variant}_lite.h')
    open(path, 'w', newline='\n').write(dk_header_lite(variant))
    print('written', os.path.abspath(path))


def dk_header(variant='rams73', bf=BF):
    out = [f'/* Big Muff Pi "{variant}" for src/sdk/wvdk.h: generated by tools/circuits/bigmuff.py c {variant}, do not edit by hand.',
           f' * Three stages (see bigmuff.stages); knobs: ' + ', '.join(f'{k} {i}' for i, k in enumerate(KNOBS)) +
           f' (rotations 0..1). bf {bf:g}. Operating point:']
    sts, v = stages(variant, bf)
    out.append(' * ' + '; '.join(f'{nm} Vb {v[b]:.3f} Vc {v[col]:.3f}' for b, col, e, nm in NODES) + ' */')
    for k, (c, nonlin, outs) in enumerate(sts):
        c.dc()
        out.append(c.dk_header(f'BM_{variant.upper()}_S{k + 1}', nonlin, KNOBS, outs,
                               forward_only=(0,) if k == 1 else ()))   # Q3 never saturates (c3 stays 2 V off it)
    return '\n'.join(out)


KNOBS = ('sustain', 'tone', 'volume')


def cmd_c(variant):
    import os
    path = os.path.join(OUT_DIR, f'bigmuff_{variant}.h')
    open(path, 'w', newline='\n').write(dk_header(variant))
    print('written', os.path.abspath(path))

if __name__ == '__main__':
    a = sys.argv[1:]
    cmd = a[0] if a else 'op'
    var = a[1] if len(a) > 1 and a[1] in VARIANTS else 'rams73'
    nums = [float(x) for x in a[1:] if x not in VARIANTS]
    {'op': lambda: cmd_op(var), 'ac': lambda: cmd_ac(var, *nums), 'thd': lambda: cmd_thd(var, *nums),
     'c': lambda: cmd_c(var), 'clite': lambda: cmd_clite(var)}[cmd]()
