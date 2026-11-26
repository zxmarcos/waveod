# Plate and Spring (RVB › Plate, RVB › Spring)

Two light reverbs, built as modules for waveod's library: `src/blocks/plate.c` (id `WVFX_ID(RVB, 2)`, 8 types) and
`src/blocks/spring.c` (id `WVFX_ID(RVB, 3)`, 10 types). They share `src/blocks/modfx/rvb.h` (lines, the front,
the resamplers, the dispersion, the design helpers). The type tables `src/blocks/plate_types.h`,
`src/blocks/spring_types.h` and the filter tables `src/blocks/modfx/rvb_fir.h` are generated: do not edit them.
Both are checked against a full reference implementation outside this repository, at about 40 % of its cost.

Both are feedback-delay networks like the Reverb (docs/reverb.md), but run below the full rate and put a chain of
first-order all-passes in every line. A chain of identical all-passes delays the highs and the lows by different
amounts: that is the dispersion that makes a plate shimmer and a spring "chirp".

## The front (both)

Per 16 frames:
1. L + R, faded in / out (~20 ms); a hold fade at the start.
2. + Echo × the predelay's output; Low Cut (first order, or 12 dB/oct by type); High Cut (Plate only, here).
3. The predelay line (0..1000 ms).
4. The reverb's input X = (1 − Echo) × the predelay's output, or its input, by type; the echo is added at the end.

## Plate

At half the rate (8 frames per step), after a 64-tap 2x decimator:
1. 16 lines. Every Step-th line (Economy 4, Normal / High 2, Ultra 1) is read linearly at a position moved by one
   of the LFOs L, R, −L, −R (a 0.025 s swing at half the rate × the type's depth); the others at their delay.
2. Per line, the type's number of all-passes (32..64) of coefficient 0.5, then the damping one-pole and the
   low shelf.
3. The lines also go into 16 short output lines.
4. The lines' inputs are (X + F) + line, F = −2/16 × Diffusion × the sum of the lines (Householder).
5. L = the lines with the odd ones negated, R = the output lines read at D × Tap / 4 + 8 with the odd ones negated,
   both / 16.
6. Two tone one-poles, a 2x interpolator (32 taps per phase), two peaking EQs when the type boosts or cuts, + Echo,
   Spread, the mix law (as the Reverb's) and the level.

## Spring

Mono, at a quarter of the rate (4 frames per step):
1. Two peaking EQs when the type has them; a drive, u = clamp(10 d X + 1, 0, 4) →
   (u² − u³/6 − 5/6) / (15 d), small-signal gain 1.
2. A 128-tap 4x decimator.
3. Per line: read at its delay, the type's all-passes (48..128) of coefficient −0.5, the damping and the shelf.
4. Two kinds of tank:
   - **four-line springs** (2 or 3; lines a b c d, outputs A B C D): out += C; c ← (1 − k) A + k D;
     b ← (1 − k) D + k A; d ← C; a ← allpass(B) + X; k = Diffusion / 20;
   - **pairs** (3 or 6; lines a b): out += A; b ← A; a ← X + allpass(B).
   The all-pass is a Schroeder one (0.707) on a line of its own, mixed in by the type's amount.
5. out × 0.5 / √tanks, a 4x interpolator, the tone filter, High Cut, L = R, the mix law, the level.
   - The tone filter is a first-order high-pass, a first-order high shelf, or a second-order tone circuit (the
     Valve Tank and Box Tanks). It is stored as its analog prototype and made digital at the block's rate; the
     pair tanks apply it before the decimator, the four-line springs after the interpolator.

## Design

When a knob moves, the design is the Reverb's, except for the rates:
- per line of D frames at the engine's rate fe (fs / 2, fs / 4), the decay gains are `g = 10^(−3 D / (fe T))` at
  Time T, at T × the type's high ratio and at T × its low ratio;
- the damping pole is `(g − g_hf) / (g + g_hf)`, at most 0.9999;
- the low shelves and the tone shelf are designed at the full rate, though they run at fe (so their corners sit
  2x / 4x higher than their numbers);
- the cuts, EQs and the tone filter run at the full rate;
- the geometry (line delays at the engine's rate, taps, the all-pass delay) is the type's at 48 kHz, scaled to
  44.1 kHz.

## Parameters

| Knob | Range | |
|---|---|---|
| Type | Plate: 8 types; Spring: 10 types | |
| Time | 0.1..100 s | |
| Predelay | 0..1000 ms | |
| Mix | 0..100 % | |
| Level | −60..+20 dB | |
| High Cut | 200..20000 Hz | Plate: the input's; Spring: the output's |
| Low Cut | 20..2000 Hz | the input's |
| Quality | Economy / Normal / High / Ultra | Plate only: how many lines are modulated |
| Spillover | Off / On | the tail when bypassed |

Bypass, Spillover and idling are the Reverb's: once nothing is above −140 dB for 0.1 s with the input closed, the
output is the input bit for bit and nothing runs.

## Cost and memory

Instructions per 64-frame block in the ARM emulator (`tools/check_modfx.py`):

| Block | This block | The reference |
|---|---|---|
| Plate | 94 k (32 all-passes) .. 128 k (64) | 277 .. 337 k |
| Spring | 42 k (2 springs) .. 101 k (6 pairs of 128 all-passes) | 110 .. 246 k |

On the pedal (`tools/bench.py`, cycles a block, the median; a block is 870,748 cycles at 600 MHz):

| Block | Cycles |
|---|---|
| Plate | 140 k (Tight Plate, Economy) .. 151 k (High), was 222 .. 246 k |
| Spring | 45 k (Short Spring) .. 117 k (Rack Spring, 6 pairs of 128 all-passes), was 64 .. 188 k |

**Where it gained:**
- the dispersion runs stage by stage with the frames in registers, two fused multiply-adds per all-pass and frame.
  The state register moves one place per stage, so the stages are unrolled over the rotation's cycle (5 registers
  for 4 frames), which leaves no register moves;
- four lines at a time (`rvb_disperse4x4`): a stage's multiply-adds on one line are a chain, each waiting for the one
  before (about 3 cycles on the M7: one line alone ran at 3.3 cycles per multiply-add, four interleaved at 1.8).
  The Plate's 8 frames go through as two runs of 4, a stage's state carrying over in its slot; the Spring reads all
  its lines before the tanks write any (a tank writes only its own lines). The same operations, the same samples;
- the multiply-adds are `__builtin_elementwise_fma` (one VFMA the compiler schedules) instead of an asm statement
  it could not move: -10 % on the Plate, -20 % on the Spring, the same samples;
- the resamplers load each coefficient once per step for all its outputs, from linear histories (no ring masks);
- the EQs that do nothing are skipped, and the Spring has no tone filters on its tail.

What is left is mostly the dispersion: ~2.3 instructions per all-pass and frame, half of the Plate's cycles.

- State: under 3 KB each.
- Buffer: Plate ~0.42 MB, Spring ~0.21 MB (the predelay line is 0.18 MB of each).

## Verification

- **Against the reference** (outside this repository): every type at its own Time / High Cut / Low Cut (the Plate also
  at Ultra), 6 s of a guitar DI at Mix 100 %:
  - Plate: the wet −96..−102 dB from the reference;
  - Spring: −91..−101 dB.
  - At Mix 30..40 % about −60 dB: the reference reads its mix curve from a table, as for the Reverb.
- `tests/test_plate.c` (host): the descriptors; every type (the Plate at the 4 qualities) with Time 1 s decaying
  60 dB within 0.3..3 s; bypass bit for bit; the tail with and without Spillover; random knobs.
- `tools/check_modfx.py plate spring`: the ARM modules bit for bit the PC builds, Plate at 7 settings, Spring at 6.
  With `--fw`, installed over USB in the emulated firmware: they play, alone and after 01A.
- On the pedal: not yet (docs/verification.md, "Plate and Spring on the pedal").
