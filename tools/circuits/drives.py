"""The drive circuits of waveod's circuit blocks (docs/drives.md): netlists for tools/circuits/circuit.py, the whole
circuit (the reference) and the stages the real-time model runs (src/blocks/circuits/<name>.h, sdk/wvdk.h).

    python tools/circuits/drives.py c [name]          write the stages' headers
    python tools/circuits/drives.py op [name]         DC operating point
    python tools/circuits/drives.py ac name [k=v ...] small-signal response (knobs 0..1 as rotations)

The circuits (values from the traced originals; the PNP germanium ones written as NPN on a positive supply, the
same circuit mirrored):
  green   overdrive, diodes in an op-amp's feedback (TS808): input buffer, the clipping amplifier (4k7 + 47n to
          ground, 51k + DRIVE 500k log in the feedback with 51p and two 1N4148), the active tone control (1k / 220n,
          TONE 20k lin, 220R + 220n on the wiper), LEVEL 100k log, output buffer. JRC4558: 3 MHz.
  rodent  distortion, an op-amp at high gain then diodes to ground (RAT): 22n / 1M / 1k / 1n into an LM308
          (1 MHz, 0.3 V/us, swing +-3 V around the bias), DISTORTION 100k log with 100p, 47R + 2u2 and 560R + 4u7 to
          ground; 4u7 + 1k into two 1N914 to ground; FILTER 100k log + 1k5 into 3n3; 22n / 1M into the JFET buffer;
          1u into VOLUME 100k log.
  fuzz    two germanium transistors with feedback (Fuzz Face, AC128): 2u2 in, Q1 33k collector, Q2 8k2 + 470R
          collector (the output between them, 10n into VOLUME 500k log), 100k from Q2's emitter to Q1's base, FUZZ
          1k lin in Q2's emitter with 22u on its wiper.
  treble  a germanium treble booster (Rangemaster, OC44): 5n in, 470k / 68k bias, 3k9 + 47u emitter, BOOST 10k log
          as the collector load, the output from its wiper through 10n.
Every circuit is fed from R_SRC (what drives it) and loaded by R_LOAD (the amp's input).
"""
import os
import sys
import numpy as np
from circuit import Circuit, R_TIE

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'blocks', 'circuits')
R_SRC = 1e3
R_LOAD = 1e6
GE = dict(Is=1e-6, bf=75.0, br=2.0)         # AC128-like germanium (KiCad forum model: IS 1u, BF 75)
GE_OC44 = dict(Is=1e-6, bf=100.0, br=2.0)


# ---------------------------------------------------------------------------------------------------------------- green
def green_clip(c, src, rot):
    """input buffer and the clipping amplifier, from `src` to node 'vclip'"""
    c.c(src, 'n2', 20e-9); c.r('n2', 'n1', 1e3); c.r('n1', '0', 510e3)
    c.buf('n1', 'vbuf')
    c.c('vbuf', 'n4', 1e-6); c.r('n4', '0', 10e3)
    c.opamp('n4', 'n5', 'vclip', gbw=3e6)
    c.r('n5', 'n7', 4.7e3); c.c('n7', '0', 47e-9)
    c.r('n5', 'n6', 51e3); c.pot('n6', 'vclip', 'vclip', 500e3, rot['drive'], 'drive')
    c.c('n5', 'vclip', 51e-12)
    c.dpair('n5', 'vclip', 2.52e-9, 1.752)


def green_tone(c, src, rot):
    """the tone control, the level and the output buffer, from `src` to 'out'"""
    c.r(src, 'n8', 1e3); c.c('n8', '0', 220e-9); c.r('n8', '0', 10e3)
    c.opamp('n8', 'n9', 'n10', gbw=3e6)
    c.r('n10', 'n9', 1e3)
    c.pot('n8', 'n11', 'n9', 20e3, rot['tone'], 'tone')          # tone 0: the wiper on the input side (dark)
    c.c('n11', 'n12', 220e-9); c.r('n12', '0', 220.0)
    c.c('n10', 'n13', 1e-6); c.r('n13', 'n14', 1e3)
    c.pot('0', 'vlev', 'n14', 100e3, rot['level'], 'level')
    c.r('vlev', '0', 510e3)
    c.c('vlev', 'n15', 1e-6); c.r('n15', '0', 510e3)
    c.buf('n15', 'out')
    c.r('out', '0', R_LOAD)


def green(rot):
    c = Circuit()
    c.src('in', R_SRC, 0.0, signal=True)
    green_clip(c, 'in', rot)
    green_tone(c, 'vclip', rot)
    return c


def green_stages(rot):
    s1 = Circuit()
    s1.src('in', R_SRC, 0.0, signal=True)
    green_clip(s1, 'in', rot)
    s2 = Circuit()
    s2.src('vx', R_TIE, 0.0, signal=True)
    green_tone(s2, 'vx', rot)
    return [(s1, (), ('vclip',)), (s2, (), ('out',))]


# --------------------------------------------------------------------------------------------------------------- rodent
def rodent_gain(c, src, rot):
    """the input network and the LM308 gain stage, from `src` to 'vfb'"""
    c.c(src, 'n5', 22e-9); c.r('n5', '0', 1e6); c.r('n5', 'n1', 1e3); c.c('n1', '0', 1e-9)
    c.opamp('n1', 'n2', 'vfb', gbw=1e6, a0=2e5, slew=0.3e6, rails=3.0)
    c.pot('n2', 'vfb', 'vfb', 100e3, rot['distortion'], 'distortion')
    c.c('n2', 'vfb', 100e-12)
    c.r('n2', 'n6', 47.0); c.c('n6', '0', 2.2e-6)
    c.r('n2', 'n7', 560.0); c.c('n7', '0', 4.7e-6)


def rodent_clip(c, src, rot):
    """the diodes to ground and the filter, from `src` to 'n9' (the JFET's gate)"""
    c.c(src, 'n8', 4.7e-6); c.r('n8', 'vclip', 1e3)
    c.dpair('vclip', '0', 2.52e-9, 1.752)
    c.pot('vclip', 'n12', 'n12', 100e3, rot['filter'], 'filter')
    c.r('n12', 'vtone', 1.5e3); c.c('vtone', '0', 3.3e-9)
    c.c('vtone', 'n9', 22e-9); c.r('n9', '0', 1e6)


def rodent_out(c, src, rot):
    """the JFET buffer and the volume, from `src` to 'out'"""
    c.buf(src, 'vj')
    c.c('vj', 'vc', 1e-6)
    c.pot('0', 'out', 'vc', 100e3, rot['volume'], 'volume')
    c.r('out', '0', R_LOAD)


def rodent(rot):
    c = Circuit()
    c.src('in', R_SRC, 0.0, signal=True)
    rodent_gain(c, 'in', rot)
    rodent_clip(c, 'vfb', rot)
    rodent_out(c, 'n9', rot)
    return c


def rodent_stages(rot):
    s1 = Circuit()
    s1.src('in', R_SRC, 0.0, signal=True)
    rodent_gain(s1, 'in', rot)
    s2 = Circuit()
    s2.src('vx', R_TIE, 0.0, signal=True)
    rodent_clip(s2, 'vx', rot)
    s3 = Circuit()
    s3.src('gx', R_TIE, 0.0, signal=True)
    rodent_out(s3, 'gx', rot)
    return [(s1, (), ('vfb',)), (s2, (), ('n9',)), (s3, (), ('out',))]


# ----------------------------------------------------------------------------------------------------------------- fuzz
def fuzz_circuit(c, src, rot):
    c.src('vcc', 1.0, 9.0)
    c.c(src, 'b1', 2.2e-6)
    c.r('vcc', 'c1', 33e3); c.q('c1', 'b1', '0', **GE)
    c.r('vcc', 'j', 470.0); c.r('j', 'c2', 8.2e3); c.q('c2', 'c1', 'e2', **GE)
    c.r('e2', 'b1', 100e3)
    c.pot('e2', 'fw', '0', 1e3, rot['fuzz'], 'fuzz')            # fuzz: rotation 0 = the wiper at the emitter
    c.c('fw', '0', 22e-6)
    c.c('j', 'vc', 10e-9)
    c.pot('0', 'out', 'vc', 500e3, rot['volume'], 'volume')
    c.r('out', '0', R_LOAD)


def fuzz(rot):
    c = Circuit()
    c.src('in', R_SRC, 0.0, signal=True)
    fuzz_circuit(c, 'in', rot)
    return c


def fuzz_stages(rot):
    return [(fuzz(rot), (0, 1), ('out',))]


# --------------------------------------------------------------------------------------------------------------- treble
def treble(rot):
    c = Circuit()
    c.src('in', R_SRC, 0.0, signal=True)
    c.src('vcc', 1.0, 9.0)
    c.c('in', 'b', 5e-9)
    c.r('vcc', 'b', 470e3); c.r('b', '0', 68e3)
    c.q('c', 'b', 'e', **GE_OC44)
    c.r('e', '0', 3.9e3); c.c('e', '0', 47e-6)
    c.pot('vcc', 'w', 'c', 10e3, rot['boost'], 'boost')          # boost 0: the wiper at the supply
    c.c('w', 'out', 10e-9)
    c.r('out', '0', R_LOAD)
    return c


def treble_stages(rot):
    return [(treble(rot), (0,), ('out',))]


# ---------------------------------------------------------------------------------------------------------------- table
CIRCUITS = {
    #  name      full     stages          knobs (in the block's order)         defaults (rotations)
    'green':  (green, green_stages, ('drive', 'tone', 'level'), (0.5, 0.5, 0.5)),
    'rodent': (rodent, rodent_stages, ('distortion', 'filter', 'volume'), (0.5, 0.3, 0.5)),
    'fuzz':   (fuzz, fuzz_stages, ('fuzz', 'volume'), (0.0, 0.5)),
    'treble': (treble, treble_stages, ('boost',), (0.7,)),
}


def rot_of(name, **kw):
    _, _, knobs, defs = CIRCUITS[name]
    r = dict(zip(knobs, defs))
    r.update(kw)
    return r


def header(name):
    full, stages, knobs, defs = CIRCUITS[name]
    rot = rot_of(name)
    P = name.upper()
    v = full(rot).dc()
    out = [f'/* The {name} circuit for src/sdk/wvdk.h: generated by tools/circuits/drives.py c {name}, do not edit by',
           f' * hand. Stages: tools/circuits/drives.py {name}_stages; knobs: ' +
           ', '.join(f'{k} {i}' for i, k in enumerate(knobs)) + ' (rotations 0..1). */']
    for k, (c, nonlin, outs) in enumerate(stages(rot)):
        c.dc()
        out.append(c.dk_header(f'{P}_S{k + 1}', nonlin, knobs, outs))
    return '\n'.join(out), v


def cmd_c(names):
    for n in names:
        text, _ = header(n)
        path = os.path.join(OUT_DIR, f'{n}.h')
        open(path, 'w', newline='\n').write(text)
        print('written', os.path.abspath(path))


def cmd_op(names):
    for n in names:
        v = CIRCUITS[n][0](rot_of(n)).dc()
        print(n + ': ' + ', '.join(f'{k} {x:.3f}' for k, x in v.items() if k not in ('0', 'in') and not k.endswith(('_rp', '_rn'))))


def cmd_ac(name, kw):
    c = CIRCUITS[name][0](rot_of(name, **kw))
    c.dc()
    f = np.geomspace(20, 20000, 25)
    h = 20 * np.log10(np.abs(c.ac(f, 'out')) + 1e-30)
    for fr, g in zip(f, h):
        print(f'{fr:7.0f} Hz {g:7.1f} dB')


if __name__ == '__main__':
    a = sys.argv[1:]
    cmd = a[0] if a else 'op'
    names = [x for x in a[1:] if x in CIRCUITS] or list(CIRCUITS)
    if cmd == 'c':
        cmd_c(names)
    elif cmd == 'op':
        cmd_op(names)
    elif cmd == 'ac':
        cmd_ac(names[0], {k: float(v) for k, v in (x.split('=') for x in a[2:] if '=' in x)})
