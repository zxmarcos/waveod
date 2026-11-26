# Circuit blocks: drives from their circuits

Six blocks of waveod's own (DRV › Circuit) run a drive pedal's circuit in real time, every resistor, capacitor,
pot, diode, transistor and op-amp of it, by the DK method. They are modules (docs/blocks.md): built by
`tools/wvfx.py`, installed from waveod Studio's Efeitos tab, no firmware update.

| Block | Circuit | Knobs | Cycles per block on the pedal |
|---|---|---|---|
| Dark Muff | Big Muff Pi "Ram's Head" (V2 1973 #3), research/pulse's Dark Muff | Sustain, Tone, Volume | 330 k |
| Dark Muff Lite | the same, Q4 and Q3 linearised, the diode solves tabulated | Sustain, Tone, Volume | 142 k |
| Green Drive | TS808: two 1N4148 in the op-amp's feedback, the active tone control | Drive, Tone, Level | 117 k |
| Rodent Dist | RAT: an LM308 at high gain (its slew rate, its rails), 1N914 to ground, the filter | Distortion, Filter, Volume, Quality | 295 k (4x) / 155 k (2x) |
| Round Fuzz | Fuzz Face: two AC128 germanium transistors with feedback | Fuzz, Volume, Quality | 340 k (4x) / 183 k (2x) |
| Treble Boost | Rangemaster: one OC44 germanium transistor | Boost | 127 k |

(`tools/bench.py`, the median over 200 blocks of its test signal, a loud pluck. 64 frames a block: 1.45 ms; the core
runs 870,748 cycles in that time.) Quality starts at 2x: at 4x the fuzz takes about the Dark Muff's time, which was
already heavy on the pedal; 4x is there for a light chain.

## How a circuit runs (`src/sdk/wvdk.h`)

- **The method:** the circuit's linear part (resistors, capacitors as trapezoidal companions, linearised
  transistors, op-amps, the pots) is folded into state-space matrices; on every sample only the nonlinear ports are
  solved, by Newton: the junctions of the transistors (Ebers-Moll), the diodes, an op-amp's input stage.
- **Stages:** a pedal is cut into stages where one part drives the next from a low impedance (an op-amp's output, a
  buffer), so the cut is exact, or, in the Muff, through a copy of the next stage's input network.
- **Rates:** the nonlinear stages run at 4x (sdk/wvos.h: two halfband stages, 27.5 samples of latency), or at 2x
  where Quality says so; purely linear stages after them at 1x when that bends nothing audible, or at 2x after the
  first decimation (Green Drive's tone stage).
- **The pots:** the matrices are built again when a knob moves. `poll` (the main loop) builds into the matrix set
  the audio is not using, then flips the set; the audio takes it at the next block, the circuit's capacitors as
  they were, as on the real pedal. A modulator (docs/modulators.md) on a circuit knob moves it at the main loop's
  pace.
- **Nonlinear ports:** a junction (`kind 0`, or `2` when reverse-biased at rest), two antiparallel diodes (`1`),
  a transconductance limited by a tanh (`3`, an op-amp's slew rate). A stage whose one port is a diode pair reads
  its solve from a table built per pot setting.
- **Newton:** the Muff's stages keep the research's solver (an extrapolated start, every step clamped to 0.1 V:
  the same samples as the Dark Muff that was heard on the pedal). The others define `DK_PNJLIM`: the start is the
  last sample's solution and a junction's step into conduction is limited as SPICE does (`pnjlim`). Germanium
  transistors that saturate and cut off (the fuzz) need that: with the clamped steps a junction swinging several
  volts in a sample took dozens of iterations, and an unconverged sample threw the circuit off. With more than two
  ports the Newton system is solved with partial pivoting.
- **When Newton stops:** the research's test is the step just taken under `DK_TOL` (0.3 mV), which nearly every
  sample passes only on its second iteration: a port that is linear for the moment (a junction that is off, the
  op-amp's input stage away from its limit) takes a step as big as the signal's change and exact, and the second
  iteration only confirms it. Rodent, Round Fuzz and Treble define `DK_PREDICT`: after a step d the current misses
  its linearisation by e = i'' d^2 / 2 (nothing for a port that is off), which leaves a residual K e, so the next
  step would be J^-1 K e, solved with the factors of J this iteration already has; Newton stops when that is under
  `DK_TOL` and no step was limited. ~30 instructions instead of an iteration (~300-750 cycles): on the bench's
  signal 2.0 iterations a sample become 1.08-1.17. The outputs and the next state are computed from the first
  iteration in the same stretch of code as that test, so the core overlaps the two (the test is a chain of
  dependent operations; computed after it, the wait cost as much as half an iteration); when another iteration is
  needed they are computed again. The Treble Boost also starts from the last two samples' extrapolation
  (`DK_EXTRAP`, a junction it would push beyond vcrit starting from the last solution): one iteration in 99.9 % of
  the samples. Against the circuit the same within 0.2 dB; the Muff's stages keep the research's test.
- **Levels:** a full-scale sample is 2.5 V at the circuit's input, and the output comes back on the same scale, so
  the circuit's own volume pot sets the level. Mono in (L + R) / 2, the same on both sides out.
- **Bypass:** one block of crossfade; bypassed and faded out the circuit is not run.
- **Memory:** the stages and their two matrix sets in the state pool (OCRAM: 8-16 KB a block), the build's workspace
  (doubles, 7 KB) in the buffer pool.

## The circuits (`tools/circuits`)

- `circuit.py`: a circuit simulator (modified nodal analysis): resistors, capacitors, sources, diodes, Ebers-Moll
  NPNs, op-amps (a transconductance into a dominant pole; optional slew limit and rails). DC operating point and
  AC response in numpy; the transient in `refsim.c` (double precision, full Newton to 1e-9 V on every sample), the
  reference the blocks are checked against. `dk_header` writes a stage's description for `wvdk.h`.
- `drives.py`: the netlists of Green Drive, Rodent Dist, Round Fuzz and Treble Boost, each as the whole circuit
  (the reference) and as the stages the block runs; `python tools/circuits/drives.py c` writes
  `src/blocks/circuits/<name>.h`. `bigmuff.py`: the Muff's (from research/pulse, the headers regenerated identical).
- Values: from traced schematics (the TS808 and the RAT from public LTspice schematics, the Fuzz Face from a
  published KiCad project with its AC128 model, the Rangemaster from its parts list, the Muff from Kit Rae's
  tracing). PNP germanium circuits are written as NPN on a positive supply: the same circuit mirrored, the output's
  polarity inverted.
- Not modelled: what drives the pedal is a 1 kOhm source (a buffer, not a guitar's pickup: a fuzz or a treble
  booster straight after a guitar sounds darker and softer than here); op-amps' rails in the TS808 (its diodes keep
  the op-amp far from them); germanium leakage; component tolerances.

## Performance

Measured with `tools/wvfx_emu.py --count N --profile` (the module alone in the ARM emulator: instructions executed,
their mix by class, the hottest code):

- **Where the time goes:** 33-45 % of the instructions are floating-point loads and stores, 13-30 % multiply-adds:
  the per-sample matrix-vector products of the DK method, every coefficient read and used once. Divisions are
  under 1 %. In the Muff and Green Drive the linear part is most of it (Dark Muff: ~590 multiply-adds per 1x sample);
  in Round Fuzz the Newton solve is (one iteration of its four ports ~500 instructions per 4x sample, ~1.9
  iterations on average; the rest of the stage ~28 k a block).
- **What helped:** unrolling the per-sample loops in full (`DK_UNROLL_FULL`, sdk/wvdk.h). Clang left the Newton loops
  of three and four ports rolled, with the Jacobian in memory: Round Fuzz 443 k -> 290 k (-35 %), Rodent Dist
  280 k -> 214 k (-23 %), the others unchanged (already unrolled). The same samples, bit for bit.
- **What did not:** `-O3` (no change); `-ffast-math` (-3..-5 %, and the module no longer gives the PC build's samples,
  which the checks rely on); pivoting by selects instead of row swaps (+20 %).
- **CMSIS-DSP:** its float kernels for the Cortex-M7 are C with the loops unrolled by four and several accumulators
  (the M7 has no floating-point SIMD; that is Helium, on the M55 / M85). They pay off on long blocks: FIRs, biquad
  cascades, FFTs. Here every sample solves a stage of 1 to 4 ports with matrices of at most 8 x 8, and the next
  sample needs this one's state: a call with its setup per product would cost more than the inlined code. The
  oversampling filters, the one block-shaped part, are a few per cent of the cost and already skip the halfbands'
  zero taps (a generic FIR would not). Worth a look for block effects to come (reverbs, long filters), not for these.
- **Assembly by hand:** the compiled per-iteration cost is close to what the algorithm needs (about 400 instructions
  of exponentials, Jacobian, elimination and step limits for four ports, against 500). What assembly could add is
  scheduling for the M7's dual issue and keeping more of the Newton state in the 32 single registers, maybe 10-30 %,
  at the price of a second implementation to keep identical to the C one (the checks compare the ARM module with the
  PC build sample for sample). Not before the cycles have been measured on the pedal.
- **On the pedal** (`tools/bench.py`, docs/bench.md): the M7 runs this code at 0.6-0.7 instructions a cycle. One
  Newton iteration costs 96 k cycles a block in Round Fuzz at 2x (750 a sample for four ports), 73 k in the Treble
  Boost at 4x, 65 k in Rodent Dist at 2x; before `DK_PREDICT` nearly every sample took two. With it: Round Fuzz
  237 k -> 183 k, Treble Boost 178 k -> 127 k (and its worst block 201 k -> 127 k), Rodent Dist 198 k -> 155 k.
  Each port's 1 / a (nvt, for the step limits) is computed once in `dk_init`, not per sample (the same value).
  Green Drive's linear S2 at 2x instead of 4x: 132 k -> 117 k.
- **What did not, on the pedal:** a looser `DK_TOL` (1e-3, 3e-3 V: Treble and Rodent unchanged, the step after the
  first iteration is a few mV); the extrapolated start alone (still two iterations: the test is on the step);
  three accumulators in the halfbands (-1..-2 %, and no longer the research's samples).
- **Next, by the numbers:** stage code generated with the matrices' structural zeros left out (Rodent, Fuzz,
  Treble: 52-62 % of the linear coefficients are nonzero; Muff and Green: 81-86 %, little to gain); the linear
  stages in a sparser state basis (a similarity transform of A, built with the matrices).

## Making the library

```
python tools/wvfx.py build src/blocks/green_drive.c          (and the others)
python tools/wvfx.py lib out/drives.bin out/fx/dark_muff.elf out/fx/dark_muff_lite.elf out/fx/green_drive.elf \
       out/fx/rodent_dist.elf out/fx/round_fuzz.elf out/fx/treble_boost.elf
```

`tools/check_drives_fw.py` builds them all and writes `out/drives.bin`; the Studio's Efeitos tab installs the
`.elf` files one by one or adds them to what the pedal has.

## Verification

- `tests/test_drives.c` (host): the descriptors (ids in DRV, defaults in range, knobs not shared, the workspace in
  the buffer), a tone at 0.5 and at full scale (finite, not silent, mono), poll (nothing with the same knobs; a moved
  knob builds the other set and flips to it), bypass (the input bit for bit; switched off, one block of crossfade).
- `tools/check_drives.py`: every block built for the PC against its whole circuit in the reference simulator at 8x,
  on 1.5 s of a guitar DI and sines of 220 Hz at three levels, three knob settings each (and 2x where there is a
  Quality); the block's output aligned on the reference (its delay found to 0.02 sample), 20 Hz-16 kHz. Then the
  module in the ARM emulator (`tools/wvfx_emu.py`: the module alone, placed and relocated as the loader does)
  against the PC build:

  | block | the DI against the circuit (three settings) | sines: H1..H5 against the circuit | ARM: instructions per block |
  |---|---|---|---|
  | Dark Muff | -51.7, -35.2, -55.2 dB | within 0.3 dB (an even harmonic near -60 dB: 6 dB) | 206 k, bit for bit |
  | Dark Muff Lite | -32.8, -22.9, -32.3 dB | odd harmonics within 0.6 dB; no even ones (by design) | 92 k, bit for bit |
  | Green Drive | -64.4, -61.8, -65.3 dB (S2 at 2x; -72.5, -70.0, -73.8 at 4x) | within 0.1 dB | 72 k, bit for bit |
  | Rodent Dist | -61.7, -64.7, -73.7 dB; 2x: -48.1 dB | within 0.1 dB | 193 k (4x); bit for bit |
  | Round Fuzz | -49.4, -53.2, -53.9 dB; 2x: -38.5 dB | within 0.3 dB (H2 at -1.8..-13 dB: the asymmetric fuzz) | 222-251 k (4x); bit for bit |
  | Treble Boost | -69.8, -69.7, -51.2 dB | within 0.1 dB | 89 k, bit for bit |

  The Dark Muff's 100/80 setting and the Lite are the research's cuts (the stages loading each other through
  copies, or not at all); the four new circuits are cut only where a stage drives the next from a low impedance.
  Getting there took three fixes in the engine: partial pivoting for more than two ports, the SPICE junction limiting
  with both of its branches, and the last sample's solution as Newton's start; without them the fuzz ran away at
  0.01 FS and the RAT on the DI.

- `tools/check_drives_fw.py FW.bin`: the six modules installed over USB in the whole firmware in the emulator, each
  played in a preset (loads, runs, its cost), Dark Muff Lite's Volume on MOD 1 (poll rebuilding in the main loop),
  01A with Dark Muff:

  | case | result |
  |---|---|
  | the library (6 modules, 86 KB) over USB | read back byte for byte |
  | each block in a preset without others, its defaults | loads, plays (no AUSENTE, no fault); over the empty preset: Dark Muff +220 k, Lite +92 k, Green +83 k, Rodent +124 k, Fuzz +143 k (both at 2x), Treble +107 k |
  | Dark Muff Lite, Volume on MOD 1, square 2 Hz, 20 / 90 | the level moves 19.3 dB, high in 305 of 650 blocks |
  | 01A + Dark Muff | 259 k -> 478 k instructions per block |

- The Dark Muff blocks against research/pulse's Dark Muff PC builds: bit for bit, 12 s of the DI at three settings.
- Not on the pedal yet: docs/verification.md, "Circuit blocks on the pedal".
