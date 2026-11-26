# Delay (DLY › Delay)

A light multi-type delay, `src/blocks/delay.c`, built as a module for waveod's library (id `WVFX_ID(DLY, 1)`). It
uses the modulation blocks' helpers (`src/blocks/modfx/modfx.h`, docs/modfx.md). It is checked against a full
reference implementation outside this repository, at a third to a quarter of its cost.

## Signal path

Everything runs per 16 frames (the control rate). The shortest loop, 1 ms, is longer than that.

**Input:**
- The input fades in and out frame by frame (~23 ms). Bypassed, it leaves the line and the echoes ring out.
- The ducker, on the Ducking type: the block power of the input in dB. Above the threshold, the wet is
  attenuated by up to 12 dB (attack 0.99 per frame, release 10 ms).
- The input shelf, on the bucket-brigade types: a first-order high shelf.

**The delay time:**
- The time is Time - 1/3000 s per channel; the right one depends on the Mode:
  - x Ratio in stereo and sweep;
  - max(Ratio, 0.1) x the time in ping-pong and dual;
  - the same as the left otherwise.
- The modulation adds to it:
  - LFO 1: Mod Rate, with an amplitude of Mod Depth / Rate. On the stereo types its right channel is a quarter
    turn ahead.
  - LFO 2: on Tape Wide and BBD Stereo only.
  - The sum is scaled by 2.5 ms, or 7.5 ms on the bucket-brigade types.
- The time is smoothed per frame (~23 ms). The smoother keeps the distance to the target and lets it decay, so
  it ends exactly on the target. A plain `s = (1 - r) t + r s` stalls in float up to ±0.66 frame from it,
  wherever its start leaves it, and under slow modulation it moves in steps.

**Two heads per channel:**
- A jump of more than 10 ms starts the other head at the new time and crossfades to it (0.001 per frame,
  ~23 ms), so a Time change does not click.
- Both heads keep moving. Only the heads being heard are read, through the 4-point cubic of `modfx_tap_wrap`.
- The read is 23 frames behind the time: the read's 7 frames, plus the 16 of the block written after it.

**The wet path:**
1. the soft clip: x Drive, `sign(v) (v² + a|v|) / (v² + 2a|v| + 1)` with `v = π/2 x + 0.05`, minus the curve at
   0, / Drive (Drive 0.05..1.5 by type);
2. the diffuser on Sweep, Ambient, Diffused and Breeze: per channel, four Schroeder all-passes with gain 0.707.
   They are fixed, or their delays are modulated by an LFO and read linearly;
3. Low Cut / High Cut, first order or 1..4 biquads by the type's slope. The low cut's biquads are the cookbook's
   high-pass with α = w/2Q;
4. the de-emphasis of three bucket-brigade types;
5. the compander's expander: `x · 10 e`, with e an envelope of |x| plus a bias.

**The feedback:**
- The matrix depends on the Mode (mono, stereo, ping-pong, dual, rotating) and is smoothed per 16 frames.
- The input is added to it. In mono and ping-pong the input L + R goes to the left line.
- Then: a clip at ±8, the compressor (`x / sqrt(10 e)`), bit reduction on two types (16 / 8 bits), and the lines.

**The tail:**
- the output shelf (Compact BBD);
- Sweep: a state-variable band-pass swept 200..1600 Hz by a 0.25 Hz LFO;
- LFO 3 on the level (Trem Echo) or the pan (Panning, Wander);
- the pan matrix (mono: the echo pan; dual: pan L / R; others: as they are) and the polarity;
- the ducker's envelope;
- the mix law: the wet's gain is a curve (a degree-6 fit) from 0 at Mix 0 to full at 50 %, and the dry falls
  from full at 50 % to 0 at 100 %. Then the level. The gains ramp over one block.

**Mono:** the right line never gets a sample, so it is not run (Digital Mono costs 16 k, Digital Stereo 24 k).

**Bypassed:** the echoes ring out over the input. Once nothing is above -140 dB for 0.1 s, the output is the
input bit for bit and nothing runs.

## Parameters

| Knob | Range | |
|---|---|---|
| Type | 24 types | below |
| Time | 1..4000 ms | Sync: a note division of the tempo |
| Feedback | -100..100 % | the left; the right keeps the type's ratio |
| Mix | 0..100 % | the law above |
| Level | -60..+20 dB | |
| Low Cut | 20..2000 Hz | 20 = the type's |
| High Cut | 200..20000 Hz | 20000 = the type's |
| Mod Rate | 0.1..10 Hz | |
| Mod Depth | 0..100 % | |
| Sync | Off / On | |

## Types

| Type | Mode | What it adds |
|---|---|---|
| Digital Mono / Stereo | mono / stereo | clean |
| Analog Mono / Stereo | mono / stereo | compander, 100 Hz / 4 kHz (8th order) |
| Tape Wide | stereo, R x 0.95 | drive 1.5, 50 Hz / 5 kHz, LFO 2 wow (1 Hz) |
| Ping-Pong | ping-pong | 50 Hz / 10 kHz |
| Dual | dual, panned L / R | 50 Hz / 10 kHz |
| Sweep | stereo | the swept band-pass, a light diffuser |
| Ducking | stereo | the ducker (12 dB under -40 dB) |
| Early Digital | mono | 16-bit reduction, 8 kHz |
| Rack Digital | mono | 8-bit reduction, R inverted |
| Ambient | stereo, R x 0.99, R feedback x 0.25 | diffuser, 120 Hz / 4 kHz, R inverted |
| Bucket Deluxe / Stereo | mono / stereo | compander, +6 dB shelf and de-emphasis, drive 0.65, 3.6 kHz |
| BBD Mono / Stereo | mono / stereo, R x 0.945 | compander, 165 / 100 Hz, 2.9 kHz; Stereo: LFO 2 (5 Hz) |
| Dark Copy | mono | compander, +10.5 dB shelf and de-emphasis, 2.1 kHz |
| Compact BBD | mono | compander, ±15 dB shelves, 64 Hz / 3.5 kHz |
| Diffused | mono | modulated diffuser |
| Breeze | stereo | modulated diffuser, 100 Hz / 4 kHz |
| Panning | mono | LFO 3 (triangle 1 Hz) on the echo pan, compander |
| Trem Echo | stereo, R x 0.975 | LFO 3 (5.5 Hz) on the level, compander |
| Wander | dual | LFO 3 (triangle 0.26 Hz) on the pans |
| Rotating | rotating (x 0.33 π/2) | the feedback turns between the sides |

## Cost and memory

Instructions per 64-frame block in the ARM emulator (`tools/check_modfx.py`):

| Types | Instructions |
|---|---|
| Digital Mono, Early Digital | 16.3 k, 17.9 k |
| Digital Stereo, Bucket Deluxe, synced with modulation | 23.5..23.7 k |
| Ping-Pong, Dual, Rotating, Wander, Tape Wide, Ducking | 25.4..26.8 k |
| Analog Stereo, Trem Echo, Sweep | 34.7..37.4 k |
| Ambient (diffuser) | 39.8 k |
| Breeze (modulated diffuser) | 50.5 k |

The reference costs 70 k..134 k for the same types. In the whole firmware the block adds 16.5 k (Digital Mono)
over the empty preset and after preset 01A.

- State: 772 bytes.
- Buffer: 1.6 MB (two 4-s lines and eight 8192-frame diffuser lines).
- Code: 26 KB.

## Verification

- **Against the reference** (outside this repository), with 6 s of a guitar DI at Mix 100 %, compared after 2 s
  (the first echoes carry the start) and after the best fractional shift per channel (±1 frame):
  - The 24 types at the default (400 ms, 30 %), plus cuts, Mix 30 % and 30 ms at -60 %: -55..-82 dB on most of them.
    - Early Digital is -41 dB. Its 16-bit reduction makes any difference into another rounding, so two equally
      right quantisation noises differ.
    - Tape Wide and BBD Stereo are -26..-30 dB. Their LFO 2 moves the time slowly, and the reference's smoother
      follows it in steps a fraction of a frame apart; this one follows it smoothly.
  - 180 ms, 70 % feedback, modulation 2 Hz / 40 %: -39..-50 dB (Early Digital -33), for the same reason.
  - Built with the plain smoother (`-DDELAY_PLAIN_SMOOTHER`, for comparisons only), the modulated settings come
    to -55..-72 dB on the left (Early Digital -36), and Tape Wide / BBD Stereo at the default to -63 / -83 dB. The
    right of the types with a right / left ratio stays at -38..-60 dB: the
    reference starts that head where its type left it.
- `tests/test_delay.c` (host):
  - the descriptor;
  - every type: a click comes back at the Time;
  - ±100 % feedback for 10 s: bounded;
  - bypass bit for bit, and the echoes ringing out then the input bit for bit;
  - a Time jump without a step;
  - random knobs.
- `tools/check_modfx.py`: the ARM module bit for bit the PC build at 15 settings. With `--fw`, it is installed
  over USB in the emulated firmware and plays, alone and after 01A.
- On the pedal: not yet (docs/verification.md, "Delay on the pedal").
