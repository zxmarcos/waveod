# Reverb (RVB › Reverb)

A light 16-line feedback-delay-network reverb, `src/blocks/reverb.c`, built as a module for waveod's library
(id `WVFX_ID(RVB, 1)`). It has 50 types: rooms, halls, chambers, churches, huge spaces, ambience, clouds and echo
spaces. The type table is `src/blocks/reverb_types.h`; it is generated, so do not edit it. The block uses the
modulation blocks' helpers (`src/blocks/modfx/modfx.h`). It is checked against a full reference implementation
outside this repository, at half its cost.

## Signal path

Per 16 frames:

**Input:**
1. L + R. Bypassed, the input fades out (~20 ms).
2. A hold fade-in at the start.
3. Echo x the predelay's output is added.
4. Low Cut (first order) / High Cut (first order, or 12 dB/oct by type).
5. The predelay line (0..1000 ms).

The reverb's input X = (1 - Echo) x the predelay's output, or its input, by type.

**Early reflections:**
- Undiffused X goes through an input delay into a line, read by 8 + 8 taps L / R with their gains.
- From Normal on, each side goes through four Schroeder all-passes.
- The taps are summed where they lie in the line. They are copied out only for the High / Ultra matrix.

**The FDN's input:** X, from Normal on through four all-passes (gain 0.707).

**16 lines:**
- Every Step-th line is read linearly at a position moved by one of the LFOs L, R, -L, -R (a 0.025 s swing x the
  type's depth), and the others at their delay. Step is 4 for Economy, 2 for Normal / High, 1 for Ultra.
- Per line: the damping one-pole, then a first-order low shelf.
- The line inputs are (X + F) + line, with F = -2/16 x the sum of the lines (Householder).
- From High on, the Hadamard transform of the 16 raw early taps x m/8 is added (a fast transform), and X is
  scaled by (1 - m/2).

**The outputs:**
1. L = the lines with the odd ones negated; R = the lines with 2, 3 mod 4 negated; both x Late / 16.
2. Two tone one-poles (pre-emphasis, tone shelf).
3. The early reflections are added.
4. Two peaking EQs, only for the types where they boost or cut.
5. The echo is added.
6. Spread (mid / side).
7. The mix law, the same as the delay's: the wet full from Mix 50 %, the dry falling from it. Then the level,
   ramped over one block.

**Bypassed:**
- With Spillover, the tail rings over the input.
- Without it, the tail is muted, but the block keeps running until the tail has decayed, so nothing stale comes
  back when it is switched on again.
- Once nothing is above -140 dB for 0.1 s with the input closed, the output is the input bit for bit and nothing
  runs.

**Design**, when a knob moves:
- Per line of D frames, the decay gains are `g = 10^(-3 D / (fs T))` at Time T, and also at T x the type's high
  ratio and at T x its low ratio.
- The damping pole is `(g - g_hf) / (g + g_hf)`, at most 0.9999, with a gain of `g (1 - pole)`.
- The low shelf has a DC gain of `g_lf / g` (limited to 0.1..10), around the type's crossover.
- The output tone shelf comes from their mean, and the pre-emphasis from the high ratio.
- The geometry (line delays, taps and their gains, the input and diffuser delays) is the type's at 48 kHz, scaled
  to 44.1 kHz.

## Parameters

| Knob | Range | |
|---|---|---|
| Type | 50 types | |
| Time | 0.1..100 s | |
| Predelay | 0..1000 ms | |
| Mix | 0..100 % | |
| Level | -60..+20 dB | |
| High Cut | 200..20000 Hz | the input's |
| Low Cut | 20..2000 Hz | the input's |
| Quality | Economy / Normal / High / Ultra | modulated lines, diffusers, the early-tap matrix |
| Spillover | Off / On | the tail when bypassed |

## Cost and memory

Instructions per 64-frame block in the ARM emulator (`tools/check_modfx.py`), Hall Large:

| Quality | This block | The reference |
|---|---|---|
| Economy | 48.5 k | 97.6 k |
| Normal | 65.4 k | 139.1 k |
| High | 87.2 k | 170.6 k |
| Ultra | 103.2 k | 194.7 k |

**Where it gained:**
- the lines are read in place, with no window copies;
- each line's read, damping and shelf run in one pass;
- the 16 lines' sums are kept in registers, in the same order, so the rounding does not change;
- the early taps are summed in place, and only High / Ultra copy them for the matrix;
- the diffusers read and write in place;
- the peaking EQs that do nothing are skipped;
- -O2 instead of -Os;
- a wrap that the compiler had made into a division per frame is now a compare.

What is left is the arithmetic itself: ~8.4 instructions per line and frame for the damping and the shelf,
which are two recursions, and ~5 for the sums.

- State: 2.3 KB.
- Buffer: 1.6 MB (the lines at 44.1 kHz).
- Code: 33 KB, 12 KB of it the type table.

## Verification

- **Against the reference** (outside this repository): each type at its own Time / High Cut / Low Cut, Economy,
  plus three types at Normal / High / Ultra and a short room with cuts, 6 s of a guitar DI at Mix 100 %.
  - The wet is -79..-103 dB from the reference's (most around -100: the same linear reads, the same arithmetic).
  - At Mix 30 % it is -62 dB. The reference reads its mix curve from a 512-point table at trunc(1023 Mix), with
    no interpolation, and this block uses the curve itself.
- `tests/test_reverb.c` (host):
  - the descriptor;
  - the 50 types at the four qualities with Time 1 s: the tail falls 60 dB within 0.2..2.5 s. The cloud types,
    made for 20..30 s, are the short ones;
  - bypass bit for bit;
  - with Spillover the tail rings out, then the input; without it, the input after the fade;
  - random knobs.
- `tools/check_modfx.py`: the ARM module bit for bit the PC build at 8 settings. With `--fw`, it is installed
  over USB in the emulated firmware and plays, alone and after 01A.
- On the pedal: not yet (docs/verification.md, "Reverb on the pedal").
