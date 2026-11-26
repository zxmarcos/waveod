"""A small circuit simulator (modified nodal analysis) for the drive blocks (docs/drives.md): netlists of resistors,
capacitors, Norton sources, diodes, Ebers-Moll NPN transistors and op-amps (a transconductance into a dominant pole,
with a slew limit and rails if asked); DC operating point and small-signal AC here in numpy, the transient
(trapezoidal capacitors, Newton on every sample) in the C engine tools/circuits/refsim.c, which this module compiles
and drives. It is the reference the real-time models (src/sdk/wvdk.h) are checked against, and it writes their
stages (dk_header).

    c = Circuit(); c.r('a', 'b', 10e3); ...; op = c.dc(); h = c.ac(f, 'out'); y = c.tran(x, fs, 'out')

A PNP circuit with a positive ground is the same as the NPN one with a negative ground: written as NPN, the signal
comes out inverted, which nothing hears.
"""
import os
import subprocess
import sys
import numpy as np

VT = 0.025852                               # thermal voltage at 300 K
HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE_SRC = os.path.join(HERE, 'refsim.c')
sys.path.insert(0, os.path.dirname(HERE))
import wvbuild                              # noqa: E402
WORK = os.path.join(HERE, '..', '..', 'out', 'circuits')
R_TIE = 1e-3                                # a stage's input or a rail: a voltage source (1 milliohm)


def _exp(x):
    """exp, continued linearly above 40 so a wild Newton step cannot overflow"""
    x = np.asarray(x, float)
    e = np.exp(np.minimum(x, 40.0))
    return np.where(x > 40.0, e * (1.0 + x - 40.0), e)


class Circuit:
    def __init__(self):
        self.nodes = {'0': 0}
        self.R, self.C, self.S, self.D, self.Q, self.O = [], [], [], [], [], []

    def n(self, name):
        return self.nodes.setdefault(name, len(self.nodes))

    # ---- elements ------------------------------------------------------------------------------------------------
    def r(self, a, b, ohms):
        self.R.append((self.n(a), self.n(b), max(float(ohms), 1.0)))

    def c(self, a, b, farads):
        self.C.append((self.n(a), self.n(b), float(farads)))

    def short(self, a, b):
        """a coupling capacitor taken as a short for the signal: a 1 F capacitor here (it blocks DC for the operating
        point), a 1000 S conductance in the real-time model (no state)"""
        self.shorts = getattr(self, 'shorts', set()) | {len(self.C)}
        self.c(a, b, 1.0)

    def src(self, a, ohms, vdc=0.0, signal=False):
        """Norton source into node a: (vdc [+ input]) behind a series resistance"""
        self.S.append((self.n(a), float(ohms), float(vdc), bool(signal)))

    def d(self, a, k, Is=2.52e-9, n=1.752):        # 1N914 / 1N4148
        self.D.append((self.n(a), self.n(k), Is, n))

    def q(self, c, b, e, Is=1e-14, bf=250.0, br=2.0):
        self.Q.append((self.n(c), self.n(b), self.n(e), Is, bf, br))

    def linearize(self, index):
        """model transistor `index` (order of q() calls) by its small-signal stamps at the operating point in the
        transient (the operating point itself is solved with the real device)"""
        self.lin = getattr(self, 'lin', set()) | {index}

    def buf(self, src, dst, g=1e4):
        """an ideal unity buffer: node dst follows node src (a stiff voltage-controlled current source)"""
        self.RAW = getattr(self, 'RAW', []) + [(self.n(dst), self.n(dst), g), (self.n(dst), self.n(src), -g)]

    def vccs(self, p, n, x, gm):
        """a current gm (v(p) - v(n)) into node x"""
        self.RAW = getattr(self, 'RAW', []) + [(self.n(x), self.n(p), -gm), (self.n(x), self.n(n), gm)]

    def dpair(self, a, k, Is=2.52e-9, n=1.752):
        """two antiparallel diodes a-k (1N914 / 1N4148 by default)"""
        self.d(a, k, Is, n)
        self.d(k, a, Is, n)

    def opamp(self, p, n, out, gbw=1e6, a0=1e5, slew=None, rails=None, gm=1e-3):
        """an op-amp: a transconductance gm from the inputs into its compensation node x (resistance a0 / gm, the
        capacitance that puts the unity gain at gbw Hz), x buffered to the output. slew (V/s): the transconductance's
        current limited to slew * C (a tanh), the op-amp's slew rate. rails (volts): x clamped by a diode to a rail
        either side, the output's swing (a voltage source each). The inputs draw no current."""
        x = out + '_x'
        cx = gm / (2 * np.pi * gbw)
        self.r(x, '0', a0 / gm)
        self.c(x, '0', cx)
        if slew:
            self.O.append((self.n(p), self.n(n), self.n(x), float(gm), float(slew * cx)))
        else:
            self.vccs(p, n, x, gm)
        if rails:
            self.src(out + '_rp', R_TIE, float(rails))
            self.src(out + '_rn', R_TIE, -float(rails))
            self.d(x, out + '_rp', 1e-14, 1.0)
            self.d(out + '_rn', x, 1e-14, 1.0)
        self.buf(x, out)
        return x

    def pot(self, a, w, b, total, rot, name=None):
        """potentiometer a-w-b, rot = 0 at end a; named pots are knobs of the generated real-time models"""
        self.r(a, w, total * rot)
        self.r(w, b, total * (1.0 - rot))
        self.pots = getattr(self, 'pots', []) + [(name, self.n(a), self.n(w), self.n(b), float(total),
                                                   len(self.R) - 2)]

    # ---- nonlinear devices: currents into each terminal and their Jacobian ----------------------------------------
    def _devices(self, v):
        """[(terminals, currents, jacobian)] at node voltages v"""
        out = []
        for a, k, Is, n in self.D:
            vd = v[a] - v[k]
            e = _exp(vd / (n * VT))
            i, g = Is * (e - 1.0), Is * e / (n * VT)
            out.append(((a, k), np.array([i, -i]), np.array([[g, -g], [-g, g]])))
        for c, b, e_, Is, bf, br in self.Q:
            vbe, vbc = v[b] - v[e_], v[b] - v[c]
            ef, er = _exp(vbe / VT), _exp(vbc / VT)
            ic = Is * (ef - er) - Is / br * (er - 1.0)
            ib = Is / bf * (ef - 1.0) + Is / br * (er - 1.0)
            dic_be, dic_bc = Is * ef / VT, -Is * er / VT * (1.0 + 1.0 / br)
            dib_be, dib_bc = Is / bf * ef / VT, Is / br * er / VT
            # d/d(vc, vb, ve): vbe = vb - ve, vbc = vb - vc
            jc = np.array([-dic_bc, dic_be + dic_bc, -dic_be])
            jb = np.array([-dib_bc, dib_be + dib_bc, -dib_be])
            out.append(((c, b, e_), np.array([ic, ib, -ic - ib]), np.array([jc, jb, -jc - jb])))
        for p, n, x, gm, imax in self.O:          # the current into x: imax tanh(gm vd / imax), into the device: minus
            t = np.tanh(gm * (v[p] - v[n]) / imax)
            i, g = imax * t, gm * (1.0 - t * t)
            out.append(((p, n, x), np.array([0.0, 0.0, -i]), np.array([[0, 0, 0], [0, 0, 0], [-g, g, 0.0]])))
        return out

    def diode_ports(self):
        """the diodes as the real-time model's ports: [(kind, a, k, index)] — an antiparallel pair (d(a, k) then
        d(k, a), or dpair) is one port (kind 1), any other diode a junction (kind 0)"""
        ports, k = [], 0
        while k < len(self.D):
            a, kk, Is, n = self.D[k]
            if k + 1 < len(self.D) and self.D[k + 1][:2] == (kk, a) and self.D[k + 1][2:] == (Is, n):
                ports.append((1, a, kk, k))
                k += 2
            else:
                ports.append((0, a, kk, k))
                k += 1
        return ports

    def _linear(self, scale=1.0, s=None):
        N = len(self.nodes)
        G = np.zeros((N, N), complex if s is not None else float)
        rhs = np.zeros(N)

        def stamp(a, b, g):
            G[a, a] += g; G[b, b] += g; G[a, b] -= g; G[b, a] -= g
        for a, b, R in self.R:
            stamp(a, b, 1.0 / R)
        if s is not None:
            for a, b, C in self.C:
                stamp(a, b, s * C)
        for a, R, vdc, sig in self.S:
            G[a, a] += 1.0 / R
            rhs[a] += scale * vdc / R
        for r, k, g in getattr(self, 'RAW', []):
            G[r, k] += g
        return G, rhs

    # ---- analyses ----------------------------------------------------------------------------------------------------
    def dc(self, v0=None):
        """DC operating point (capacitors open), source stepping from 0 V"""
        N = len(self.nodes)
        v = np.zeros(N) if v0 is None else np.array(v0, float)
        for scale in (np.linspace(0.1, 1.0, 10) if v0 is None else [1.0]):
            G0, rhs0 = self._linear(scale)
            for it in range(200):
                G, rhs = G0.copy(), rhs0.copy()
                G[np.arange(N), np.arange(N)] += 1e-12
                for t, i, J in self._devices(v):
                    t = list(t)
                    G[np.ix_(t, t)] += J
                    rhs[t] -= i - J @ v[t]
                G[0, :] = 0; G[:, 0] = 0; G[0, 0] = 1; rhs[0] = 0
                vn = np.linalg.solve(G, rhs)
                dv = vn - v
                step = np.max(np.abs(dv))
                if step > 0.1:                  # damp big steps: junctions are exponential
                    dv *= 0.1 / step
                v = v + dv
                if step < 1e-10:
                    break
        self.v_op = v
        return {name: v[i] for name, i in self.nodes.items()}

    def small_signal(self):
        """the devices' conductances at the operating point (call dc() first)"""
        N = len(self.nodes)
        Gd = np.zeros((N, N))
        for t, i, J in self._devices(self.v_op):
            Gd[np.ix_(list(t), list(t))] += J
        return Gd

    def ac(self, freqs, out, inp=None):
        """small-signal transfer from the signal source(s) (1 V behind their resistance) to node `out`"""
        Gd = self.small_signal()
        N = len(self.nodes)
        h = []
        o = self.nodes[out]
        for f in np.atleast_1d(freqs):
            G, _ = self._linear(1.0, s=2j * np.pi * f)
            G = G + Gd
            rhs = np.zeros(N, complex)
            for a, R, vdc, sig in self.S:
                if sig:
                    rhs[a] += 1.0 / R
            G[0, :] = 0; G[:, 0] = 0; G[0, 0] = 1; rhs[0] = 0
            h.append(np.linalg.solve(G, rhs)[o])
        return np.array(h)

    # ---- the real-time model's description (research/pulse/common/dk.h) --------------------------------------------
    def dk_header(self, P, nonlinear, knobs, outputs, forward_only=()):
        """C text of a dk_desc_t named P: the linear part at the operating point (call dc() first; the supply rail
        is ground for the signal, the other transistors linearised), the junctions of the transistors `nonlinear`
        (indices in q() order; those in forward_only without their base-collector junction: they never saturate)
        and every antiparallel diode pair as the nonlinear ports, one Newton group; the
        signal sources are the inputs (in order), `outputs` node names; pots named in `knobs` are its knobs"""
        idx = {}
        rails = {a for a, R, vdc, sig in self.S if not sig}      # supplies and rails: ground for the signal
        for nm, i in sorted(self.nodes.items(), key=lambda t: t[1]):
            if nm != '0' and i not in rails:
                idx[i] = len(idx) + 1
        m = lambda i: idx.get(i, 0)
        pots = getattr(self, 'pots', [])
        pot_r = {r for *_, r in pots} | {r + 1 for *_, r in pots}
        G = [(m(a), m(b), 1.0 / R) for k, (a, b, R) in enumerate(self.R) if k not in pot_r and m(a) != m(b)]
        shorts = getattr(self, 'shorts', set())
        G += [(m(a), m(b), 1e3) for k, (a, b, C) in enumerate(self.C) if k in shorts]
        caps = [(a, b, C) for k, (a, b, C) in enumerate(self.C) if k not in shorts]
        devs = self._devices(self.v_op)[len(self.D):len(self.D) + len(self.Q)]
        raw = [(m(r), m(k), g) for r, k, g in getattr(self, 'RAW', []) if m(r) and m(k)]
        for qi in range(len(self.Q)):
            if qi not in nonlinear:
                t, I0, J = devs[qi]
                raw += [(m(t[r]), m(t[k]), J[r, k]) for r in range(3) for k in range(3)
                        if m(t[r]) and m(t[k]) and abs(J[r, k]) > 1e-15]
        ports = []
        for qi in nonlinear:
            col, b, e, Is, bf, br = self.Q[qi]
            w = (m(col), m(b), m(e))
            ports.append((0, m(b), m(e), w, (1.0, 1.0 / bf, -(1.0 + 1.0 / bf)), Is, VT, self.v_op[b] - self.v_op[e]))
            if qi not in forward_only:
                ports.append((0, m(b), m(col), w, (-(1.0 + 1.0 / br), 1.0 / br, 1.0), Is, VT,
                              self.v_op[b] - self.v_op[col]))
        for kind, a, kk, k in self.diode_ports():
            _, _, Is, n = self.D[k]
            v0 = self.v_op[a] - self.v_op[kk] if kind == 0 else 0.0
            ports.append((kind, m(a), m(kk), (m(a), m(kk), 0), (1.0, -1.0, 0.0), Is, n * VT, v0))
        for p, n, x, gm, imax in self.O:        # kind 3: imax tanh(vd gm / imax) into x
            vd0 = self.v_op[p] - self.v_op[n]
            assert abs(vd0) < 1e-3, 'an op-amp off its linear range at rest'
            ports.append((3, m(p), m(n), (m(x), 0, 0), (-1.0, 0.0, 0.0), imax, imax / gm, 0.0))
        ins = [(m(a), 1.0 / R) for a, R, vdc, sig in self.S if sig]
        f = lambda x: f'{x:.9e}f'
        L = [f'static const dk_g_t {P}_G[] = {{'] + [f'    {{ {a}, {b}, {f(g)} }},' for a, b, g in G] + ['};']
        L += [f'static const dk_raw_t {P}_RAW[] = {{'] + [f'    {{ {a}, {b}, {f(g)} }},' for a, b, g in raw] + ['};' if raw else '    { 0 } };']
        L += [f'static const dk_c_t {P}_C[] = {{'] + [f'    {{ {m(a)}, {m(b)}, {f(C)} }},' for a, b, C in caps] + ['};' if caps else '    { 0 } };']
        mine = [(n, a, w, b, tot) for n, a, w, b, tot, _ in pots if n in knobs]
        L += [f'static const dk_pot_t {P}_POT[] = {{'] + [
            f'    {{ {m(a)}, {m(w)}, {m(b)}, {knobs.index(n)}, {f(tot)} }},   /* {n} */' for n, a, w, b, tot in mine]
        L += ['    { 0 } };' if not mine else '};']
        L += [f'static const dk_port_t {P}_PORT[] = {{']
        for kind, sp, sn, nn, ww, Is, nvt, v0 in ports:
            L.append(f'    {{ {kind}, {sp}, {sn}, {{ {nn[0]}, {nn[1]}, {nn[2]} }}, {{ {f(ww[0])}, {f(ww[1])}, {f(ww[2])} }}, '
                     f'{f(Is)}, {f(nvt)}, {f(v0)} }},')
        if not ports:
            L.append('    { 0 }')
        L += ['};', f'static const uint8_t {P}_GROUP[] = {{ 0, {len(ports)} }};']
        # port kinds as a compile-time constant, 2 bits each: 0 forward junction, 2 junction reverse-biased at rest (it is
        # usually off: tested first), 1 diode pair
        kinds = sum((k if k in (1, 3) else (2 if v0 < 0 else 0)) << (2 * j) for j, (k, *_, v0) in enumerate(ports))
        assert len(ports) <= 4 and len(caps) <= 8 and len(idx) <= 24, (len(ports), len(caps), len(idx))
        L += [f'#define {P}_NI {len(ins)}', f'#define {P}_NC {len(caps)}', f'#define {P}_NP {len(ports)}',
              f'#define {P}_NO {len(outputs)}', f'#define {P}_KINDS {kinds:#x}']
        L += [f'static const dk_desc_t {P} = {{ {P}_G, {len(G)}, {P}_RAW, {len(raw)}, {P}_C, {len(caps)}, {P}_POT, '
              f'{len(mine)}, {P}_PORT, {len(ports)}, {P}_GROUP, 1, {len(idx)}, {len(ins)}, '
              f'{{ {", ".join(str(a) for a, g in ins) or "0"} }}, {{ {", ".join(f(g) for a, g in ins) or "0"} }}, '
              f'{len(outputs)}, {{ {", ".join(str(m(self.nodes[o])) for o in outputs)} }} }};']
        return '\n'.join(L) + '\n'

    # ---- transient (C engine) -------------------------------------------------------------------------------------
    def netlist(self, fs, probes):
        if not hasattr(self, 'v_op'):
            self.dc()
        L = [f'N {len(self.nodes)}', f'T {fs:.17g}']
        L += [f'R {a} {b} {R:.17g}' for a, b, R in self.R]
        L += [f'C {a} {b} {C:.17g}' for a, b, C in self.C]
        L += [f'S {a} {R:.17g} {vdc:.17g} {int(sig)}' for a, R, vdc, sig in self.S]
        L += [f'D {a} {k} {Is:.17g} {n:.17g}' for a, k, Is, n in self.D]
        lin = getattr(self, 'lin', set())
        L += [f'Q {c} {b} {e} {Is:.17g} {bf:.17g} {br:.17g}' for i, (c, b, e, Is, bf, br) in enumerate(self.Q)
              if i not in lin]
        devs = self._devices(self.v_op)[len(self.D):len(self.D) + len(self.Q)]
        for i in sorted(lin):                   # I = I0 + J (v - v0): stamps J and a constant J v0 - I0
            t, I0, J = devs[i]
            for r in range(3):
                for k in range(3):
                    L.append(f'G {t[r]} {t[k]} {J[r, k]:.17g}')
                L.append(f'I {t[r]} {-(I0[r] - J[r] @ self.v_op[list(t)]):.17g}')
        L += [f'G {r} {k} {g:.17g}' for r, k, g in getattr(self, 'RAW', [])]
        L += [f'O {p} {n} {x} {gm:.17g} {imax:.17g}' for p, n, x, gm, imax in self.O]
        L += [f'V {i} {self.v_op[i]:.17g}' for i in range(1, len(self.nodes))]
        L += [f'P {self.nodes[p]}' for p in probes]
        return '\n'.join(L) + '\n'

    def tran(self, x, fs, probes, tag='ckt'):
        """node voltages (AC part: the operating point removed) for the input x (volts, at fs) -> [len(x), probes]"""
        exe = engine()
        probes = [probes] if isinstance(probes, str) else list(probes)
        os.makedirs(WORK, exist_ok=True)
        tag = f'{tag}_{os.getpid()}'
        nl, fi, fo = (os.path.join(WORK, f'{tag}.{e}') for e in ('net', 'in', 'out'))
        open(nl, 'w').write(self.netlist(fs, probes))
        np.asarray(x, np.float64).tofile(fi)
        subprocess.run([exe, nl, fi, fo], check=True)
        y = np.fromfile(fo, np.float64).reshape(-1, len(probes))
        return y - np.array([self.v_op[self.nodes[p]] for p in probes])


def engine():
    exe = os.path.join(WORK, 'refsim.exe')
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(ENGINE_SRC):
        os.makedirs(WORK, exist_ok=True)
        # the reference: plain -O2 for this CPU (not the blocks' flags)
        subprocess.run([wvbuild.host_cc(), '-O2', '-march=native', '-D_CRT_SECURE_NO_WARNINGS', '-o', exe, ENGINE_SRC],
                       check=True)
    return exe
