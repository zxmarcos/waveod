# Modulation blocks (MOD)

Six light modulation blocks, each a module for waveod's library: Chorus (`docs/chorus.md`), Flanger, Phaser,
Rotary, Vibe and Tremolo. Each keeps its sound close to a full reference implementation (outside this repository) at about half to
a third of its cost.

| Block | Id | Types | Instructions per 64-frame block | Wet vs the reference |
|---|---|---|---|---|
| Chorus | `WVFX_ID(MOD, 1)` | 23 | 12.6 k (2 voices) .. 31.1 k (8 voices) | -51 .. -79 dB |
| Flanger | `WVFX_ID(MOD, 2)` | 32 | 11.9 k .. 15.8 k | -44 .. -72 dB |
| Phaser | `WVFX_ID(MOD, 3)` | 13 | 5.1 k (2 stages) .. 9.5 k (12 stages) | -75 .. -100 dB |
| Rotary | `WVFX_ID(MOD, 4)` | – | 15.6 k | -52 .. -70 dB |
| Vibe | `WVFX_ID(MOD, 5)` | 3 | 7.7 k .. 9.1 k | -87 .. -95 dB |
| Tremolo | `WVFX_ID(MOD, 6)` | Tremolo / Panner | 3.6 k .. 4.3 k | -103 .. -105 dB (Tremolo) |

The references cost 33 k..87 k (chorus), 26 k (flanger), 10 k..18 k (phaser), 39 k (rotary), 17 k (vibe) and 7.3 k
(tremolo). In the whole
firmware (`tools/check_modfx.py --fw`) each adds about what it costs alone: +12.8 k, +13.0 k, +6.0 k, +15.7 k,
+9.1 k, +4.4 k over the empty preset and after preset 01A's chain.

## What they share (`src/blocks/modfx/modfx.h`)

All of it is static inline, so a block takes only what it uses:

- **Knobs:** a named option's index, and the Rate of a Rate knob, free or synced to a note division (1/1..1/16) of
  the tempo.
- **Math without libm:** sin / cos of turns (error < 1e-7), the bilinear prewarp `tan(π f / fs)`, `modfx_exp2`,
  and `modfx_pow` through it.
  - The SDK's `wvfx_exp2` uses the Taylor terms and errs by up to 8.5e-5. That is enough to shift an envelope fed
    back on itself: the phaser's photocell types were at -17 dB from the reference with it, and at -75 dB with
    `modfx_exp2`, a degree-6 fit (< 1e-8). The SDK's function stays as it was, so the other blocks do not change.
- **Filters:** one-poles and biquads, with their block loops (state in registers). Designs: first-order low and
  high pass, the emphasis shelf, 2nd-order bilinear low-pass of a Q, and the audio cookbook's low and high pass.
- **The LFO phase:** a float sum frame by frame, wrapped at 2π, the way the reference sums it, so the phase never
  drifts from it. Sine by a rotation from the block's first frame; the triangle of a phase.
- **The delay-line read:** a 4-point, 3rd-order Lagrange interpolator.
  - The line's first 3 frames are mirrored past its end, so the four taps are always contiguous: one address,
    four loads.
  - The M7's VFP has no register-offset load, so this saves ~12 instructions per read.
- **Shared stages:** the bucket-brigade soft clip, and the equal-power mix.

Every block:
- rebuilds what the knobs make (filters, gains, rates) in `process` when a knob changes;
- ramps its output gains over one block;
- when bypassed, outputs the input bit for bit and keeps only what must keep running (the LFO, the rotors, the
  lines).

## Flanger (`src/blocks/flanger.c`)

- **Control rate, every 16 frames:**
  1. The LFO (sine, triangle, square or "exp"), optionally quantised to steps, with amplitude 1/2 and the right
     channel Phase ahead.
  2. The sweep `x = (1 - Depth) Manual + Depth (lfo + 1/2)`, through a one-pole smoother whose cutoff follows x.
  3. `1 - y` on the inverted types.
  4. The sweep law: linear, square, or exponential `(M^y - 1) / (M - 1)` with M = Width / Delay x Shape.
  5. The delay target `Delay + (Width' - Delay) y`, where `Width' = min(Delay + (Width - Delay) s, 20 ms)`, and
     s = 1 or Auto Depth (1/4, 1/2, 1 per Hz of Rate).
- **Audio, in passes of 8 frames.** The loop is never shorter than 15 frames, so a pass reads only what earlier
  passes wrote. Each pass:
  1. smooths the delay per frame (~7 ms);
  2. reads it with the cubic;
  3. runs the Low Cut and the High Cut (1st to 5th order, the type's slope) inside the loop;
  4. applies the polarity, which gives the wet;
  5. scales the wet by Feedback, runs it through the type's feedback filter, adds the input, applies the soft clip
     on the driven types, and writes the result to the line.

  The bucket-brigade types low-pass the wet after the feedback tap.
- **Then:**
  - Thru-Zero: the dry from a line of its own, delayed by the sweep at the LFO's centre;
  - the output low cut;
  - Spread;
  - the equal-power mix.
- **Knobs:** Rate 0.05..10 Hz (Sync), Depth, Feedback ±99 %, Delay 0.34..2 ms, Width 2..20 ms, Mix, Phase, Wave,
  High Cut, Level, Sync, Type.
- **Types (32):** Digital, Analog, Thru-Zero, Wide Jet, Zero Cancel, Pop, Vintage Box, Compact BBD, Electric,
  Deluxe, Studio BBD, Fixed Zero, Step, Clean Wide, Square, Cancel, Slow Jet, Deep BBD, Harmonic, Half Sweep,
  Exp Sweep, Melodic Steps, Eighties Rack, Stereo Glide, Subtle, Space Jet, Wide Trip, Tube Sweep, Talk Sweep,
  Fixed Cancel. Each type sets:
  - the law, the polarity, Thru-Zero, Auto Depth, steps, inversion and the LFO form;
  - Manual, the smoother, Shape, the clip and Spread;
  - the filters: in the loop, on the feedback, on the wet, and at the output.
- **Memory:** state 476 bytes; buffer 16.4 KB (two 1024-frame lines, two Thru-Zero lines).

## Phaser (`src/blocks/phaser.c`)

- **Control rate, every 16 frames:**
  1. The LFO: sine, triangle, a rising saw, or an RC "astable" oscillator.
  2. The sweep `x = Scale ((1 - Depth) Manual + Depth (lfo + 1/2))`, through a one-pole smoother.
  3. `max(Bias + x, 0)`.
  4. The law: linear; exponential `(M^x - 1) / (M - 1)` with M = Max / Min x Shape; JFET (square, then linear);
     or a photocell (x², low-passed, into an attack / release envelope).
  5. `f = Min + (Max - Min) x`, then the all-pass coefficient `(1 - w) / (1 + w)` with `w = π f / fs`, reached
     linearly over the 16 frames.
- **Audio, per channel:** 2..12 first-order all-pass stages, `y = s - a x`, `s = x + a y`.
  - The chain's last output times Feedback is added before the type's stage.
  - `out = (y - Feedback y[n-1]) / (1 - Feedback)`.
  - The stage count and the feedback stage are constants of the inner loop: one copy per count, with the states
    in registers. This is where the 2x over the reference comes from.
- **Knobs:** Rate 0.1..10 Hz (Sync), Depth, Feedback ±90 %, Stages, Min Freq, Max Freq, Mix, Phase, Wave, Level,
  Sync, Type.
- **Types (13):** Digital Mono / Stereo, Small 45, Small 90, Fed 90, Stereo 8, Bright 90, Rock, Lamp, Saw Sweep,
  Deep 90, Big Sweep, Twin Lamp. Each type sets the law, the feedback stage, the LFO form, Scale, Manual,
  Bias, Shape, the astable's curvature, the smoother and the photocell's times.
- **Memory:** state 308 bytes, no buffer.

## Rotary (`src/blocks/rotary.c`)

- **Input:** mono, `0.7 (L + R)`.
- **Drive:**
  1. a 2.5 kHz +16 dB shelf;
  2. x Drive;
  3. a soft clip with two cubic knees;
  4. / Drive;
  5. the -16 dB shelf back.

  Even at the lowest Drive the clip's slope gives +0.6 dB.
- **Crossover:**
  - the drum: low-passed at 800 Hz and high-passed at 50 Hz;
  - the horn: high-passed at 800 Hz, with a +5 dB peak at 1.5 kHz.

  Each goes into a 128-frame line.
- **The rotors:**
  - Their speeds follow Rate with inertia: the drum at Low Rate x Rate with 2 s, the horn with 0.8 s. The speeds
    are updated every 16 frames, and each turns a phase.
  - The distance from a mic to the rotor's mouth, `|mic - rotor| / 350 m/s`, comes from a 256-angle table made at
    init (rotor radius 0.14 m, the mic 0.1 m from its circle). It gives the Doppler delay and the level, `1 -
    Depth (distance - nearest) / (farthest - nearest)`.
  - The three mics' delays and levels are computed every 16 frames and reached linearly in between: the delay
    curve bends little over 16 frames (< 1e-3 frame). This took the block from 20.1 k to 15.6 k with no change
    against the reference.
  - The drum feeds both sides through one mic. The horn has two, the right one Spacing ahead.
- **Output:** drum + horn x Hi Level per side, Spread (mid / side), and a linear mix.
- **Bypassed:** only the rotors keep turning; the lines are cleared when the block comes back.
- **Knobs:** Rate 0..10 Hz, Low Rate x0.1..10, Low Depth, Hi Depth, Hi Level ±6 dB, Spacing, Drive 0.05..50,
  Spread ±200 %, Mix, Level.
- **Memory:** state 1.3 KB (with the table), buffer 1 KB.

## Vibe (`src/blocks/vibe.c`)

- **Control rate, every 16 frames:**
  1. The LFO (sine or triangle, amplitude 1/2, the right channel Phase ahead) x Scale. On the Lamp type the scale
     also grows with the rate, x clamp(Rate / 4, 0.33, 1): a lamp swings wider when it is driven faster.
  2. A one-pole smoother (not with the photocell).
  3. `x = (Bias + 1/2) + (Manual - 1/2)(1 - Depth) + Depth lfo`, clamped to 0..1.
  4. The law:
     - **photocell:** x² through a one-pole into an attack / release envelope e (the lamp), then the LDR's
       conductance `g = Gd + e (Gl - Gd)` and the stage corner `f = 1 / (2π C (1/g + Rs))`;
     - **exponential:** `f = Min/10 + (Max - Min)/10 (M^x - 1) / (M - 1)`.
  5. Four stages at `f x (1, r1, r2, r3)`, at most 21.1 kHz. Each has its own coefficient `(1 - w) / (1 + w)`,
     `w = tan(π f / fs)`, reached linearly over the 16 frames.
- **Audio, per channel:** the four all-pass stages unrolled, with the chain's last output x Feedback added at the
  first. The chain output is the wet, with no normalisation. Then the type's low cut / high cut and the
  equal-power mix (Mix 100 % = vibrato).
- **Knobs:** Rate 0.1..10 Hz (Sync), Depth, Feedback ±90 %, Mix (53 %), Phase, Wave, Level, Sync, Type.
- **Types (3):**
  - Lamp: photocell, stage ratios 1 / 14.6 / 46.8 / 468, the rate-dependent swing, 47 Hz / 10.5 kHz;
  - Exp: exponential law, ratios 1 / 8 / 64 / 512, no cuts;
  - Bright Lamp: Lamp without the swing, 30 Hz / 15 kHz.
- **Memory:** state 268 bytes, no buffer.

## Tremolo (`src/blocks/tremolo.c`)

- **The LFO:** sine (by rotation), triangle or square, the right channel Phase ahead, through a 20 Hz one-pole.
- **Modes:**
  - **Tremolo:** `gain = (1 - d/2) + (d/2) lfo` on each side.
  - **Panner:** the same on the left and `(1 - d/2) - (d/2) lfo` on the right. L + R stays at `2 (1 - d/2)` x the
    input while the sound moves between the sides. The reference has no panner; it is checked by
    `tests/test_modfx.c`.
- **Depth:** d fades in when the block starts (33 ms time constant, per 16 frames).
- **Output:** level and balance, ramped over one block.
- **Bypassed:** the LFO waits at its start (a quarter turn), so it always comes back at the same point.
- **Knobs:** Rate 0.2..20 Hz (Sync), Depth, Wave, Phase, Level, Balance, Sync, Mode.
- **Memory:** state 96 bytes, no buffer.

## Verification

- **Against the references** (outside this repository), with 3..6 s of a guitar DI, Mix 100 %:
  - Flanger: the 32 types at two settings (the default, and fast with -80 % feedback), plus triangle, High Cut,
    Mix, Thru-Zero without feedback and 95 % feedback.
  - Phaser: the 13 types at the default and at 12 stages with -70 % feedback, plus 2 stages at 90 %, a wide slow
    sweep, Mix and 8 stages.
  - Rotary: fast, slow, deep, driven, spacing / spread, low rate, Mix, stopped.
  - Vibe: the 3 types at the default, fast and deep with feedback, slow triangle with negative feedback, Mix 53 %.
  - Tremolo: the three waves, stereo phase, 20 Hz full depth, balance, slow.

  The table above gives the ranges. The worst Flanger case is 95 % feedback, where the comb's resonance
  amplifies the interpolator's top-octave difference. The worst Rotary case is Drive 50 with Hi Level +6, where
  the clip amplifies the filters' rounding.
- `tests/test_modfx.c` (host), for each of the five:
  - the descriptor;
  - every type at Mix 100 %: finite, the level -15..+12 dB, and the sweep moving a tone;
  - bypass bit for bit, the one-block fade, and back on;
  - random knobs every block, on and off;
  - and the Panner's L + R.
- `tools/check_modfx.py`: the ARM modules bit for bit the PC builds at 42 settings. With `--fw`, the six are
  installed over USB in the emulated firmware and play, alone and after 01A.
- On the pedal: not yet (docs/verification.md, "Modulation blocks on the pedal").
