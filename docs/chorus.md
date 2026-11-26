# Chorus (MOD › Chorus)

A light multi-type chorus, `src/blocks/chorus.c`, built as a module for waveod's library (id `WVFX_ID(MOD, 1)`,
`tools/wvfx.py build src/blocks/chorus.c`). It is the first of a set of light modulation, delay and reverb blocks (the others: docs/modfx.md, with what they share):
each keeps its sound close to a full reference implementation at a fraction of the cost.

## Signal path

Per channel, the wet path is:

1. a one-pole low cut (some types);
2. an emphasis shelf, one zero and one pole (some types);
3. a soft clip around a bias, a bucket-brigade's input stage: `v = clamp(0.2 + g x, ±1.5)`, `v - 4/27 v³`, the
   bias's own clip taken out, `/ g` (g = the type's clip gain, 0.05..0.5);
4. High Cut: a 4th-order Butterworth low-pass (Q 0.541 and 1.307), off at 20 kHz;
5. a one-pole high-pass, then the inverse of the emphasis shelf (some types);
6. one delay line, 4096 frames (93 ms) per channel.

1 to 4 voice pairs read the line, each at:
- the fixed Delay, the right channel's at Delay (1 - offset);
- plus 7 frames;
- plus the modulation, through a 4-point, 3rd-order Lagrange interpolator.

The fixed delay and the modulated one share the line. Everything before it is linear or memoryless, so the order
does not matter, and one line and one read do the work of two.

The line's first 3 frames are mirrored past its end, so the four taps of a read are always contiguous: one
address, four loads. The VFP has no register-offset load, so this alone saved ~12 instructions per voice and
frame.

## Modulation

- One LFO, sine or triangle. Its phase is a float sum frame by frame, the sine by a rotation from the block's
  first frame (no table).
- **Sine:** the voice pairs spread the phase evenly. Voice v sits at v · Phase / n on the left and at Phase + v ·
  Phase / n on the right. The odd pairs are scaled by a second LFO, a 1 Hz sine, by `(1 - d2) + d2 · lfo2`.
- **Triangle:** one pair, the right channel Phase ahead.
- **Mono types:** Phase is ignored (0), so L = R for the same input.
- An optional low-pass on the LFO (types that round the sweep).

The sweep is `x = lfo · h + o`:
- h = Depth / 2;
- o = h, or 1/2 for the types centred on the middle of the sweep;
- with Auto Depth, Depth is scaled by 0.15 n / Rate (n = 1 or 2, x2 more on the Analog types), at most 1: slow
  rates sweep wider.

There are two sweep laws:
- **Digital:** the delay offset is `x · Width`, smoothed frame by frame (time constant ~2 ms).
- **Analog:** a bucket-brigade clock. Once per 16 frames the delay goes to `Delay · ((Delay + Width) /
  Delay)^x`, through `modfx_exp2` and a log2 computed when the knobs move, smoothed in between.

After the voices:
1. 1 / sqrt(pairs) and the type's polarity;
2. a 20 Hz high-pass and a 20 kHz low-pass (one-poles);
3. the Space types' cross-mix: each side's voices go to the other side through an 800 Hz high-pass at g1, and
   the dry at g2;
4. the output: equal-power wet / dry (sin / cos of π/2 · Mix) with the level. These ramp over one block when a knob
   or the switch changes; on, the first block fades in.

Bypassed, the line keeps being written (no stale audio when it comes back), the LFOs keep running, the voices do
not run, and the output is the input bit for bit.

## Parameters

| Knob | Range | |
|---|---|---|
| Rate | 0.1..10 Hz | with Sync on, a note division (1/1..1/16) of the tempo |
| Depth | 0..100 % | |
| Delay | 0.1..50 ms | the fixed part |
| Mix | 0..100 % | equal power |
| Phase | 0..180° | the right channel's LFO phase |
| Wave | Sine / Triangle | Triangle: one voice pair |
| High Cut | 200..20000 Hz | 4th order; 20000 = off |
| Level | -60..+20 dB | |
| Sync | Off / On | |
| Type | 23 types | below |

Everything derived from the knobs (filters, rotations, gains) is rebuilt in `process` when a knob changes. The
filters are bilinear with `tan(π f / fs)` from a sin / cos polynomial; their states carry over.

## Types

Frequencies are the filter corners; Width is the sweep; the clip is the gain into the soft clip.

| # | Type | Sweep | Pairs | Auto Depth | Filters | Width |
|---|---|---|---|---|---|---|
| 0 | Digital Mono | digital | 1 | 1 | – | 40 ms |
| 1 | Digital Stereo | digital | 1 | 1 | – | 40 ms |
| 2 | Analog Mono | analog | 1 | 1 | clip 0.5 | 40 ms |
| 3 | Analog Stereo | analog | 1 | 1 | clip 0.5, R offset 0.5 | 40 ms |
| 4 | Compact | digital, centred | 1 | – | 51 Hz LC, 103 Hz HP, shelf 411 / 2337 Hz, LFO 63.7 Hz | 2.4 ms |
| 5 | Warm Wide | analog | 1 | 1 | clip 0.25, R offset 0.5, R inverted | 40 ms |
| 6 | Eighties | analog | 1 | 1 | R inverted | 39 ms |
| 7 | Triangle | digital | 1 | 1 | R inverted | 40 ms |
| 8 | Octet | digital | 4 | 1 | LFO 2 at 0.1 | 40 ms |
| 9-11 | Space 1 / 2 / 3 | digital | 1 | 1 | 51 Hz LC, 103 Hz HP, cross-mix -0.3/0, -0.3/0.2, -1/0.2 | 40 ms |
| 12 | Analog Quad | analog | 2 | 1 | LFO 2 at 0.1 | 40 ms |
| 13 | Analog Octet | analog | 4 | 1 | LFO 2 at 0.1 | 40 ms |
| 14 | Vibrato | digital | 1 | 2 | – | 40 ms |
| 15-16 | Vintage Vibrato / Chorus | digital, centred | 1 | – | 56 Hz HP, shelf 1068 / 3379 Hz | 3.85 ms |
| 17 | Compact Super | digital, centred | 1 | – | 35 Hz LC, shelf 411 / 2337 Hz | 2.8 ms |
| 18 | Compact Analog | digital, centred | 1 | – | 58 Hz LC, shelf 411 / 2337 Hz, LFO 63.7 Hz | 2.48 ms |
| 19 | Small Analog | analog | 1 | – | shelf 398 / 2316 Hz, LFO 3.39 Hz | 9.02 ms |
| 20 | Compact Bass | digital, centred | 1 | – | 51 Hz LC, 188 Hz HP, shelf 411 / 2337 Hz, LFO 63.7 Hz | 2.4 ms |
| 21 | Studio Rack | analog, centred | 1 | – | shelf 1518 / 8537 Hz | 7.2 ms |
| 22 | Stereo Rack | digital, centred | 1 | – | shelf 1389 / 15200 Hz, LFO 0.9 Hz, R inverted | 10.5 ms |

The clip gain is 0.05 unless the table says otherwise; the vintage, compact and rack types use 0.25.

## Cost and memory

Instructions per 64-frame block in the ARM emulator (`tools/check_modfx.py`):

| Setting | Instructions |
|---|---|
| Digital Stereo, Analog Stereo, synced | 12.6..12.7 k |
| Small Analog, Stereo Rack (triangle) | 15.2 k / 15.8 k |
| Space 3 | 16.9 k |
| Compact with High Cut at 6 kHz | 19.0 k |
| Octet, Analog Octet | 30.6..30.9 k |

In the whole firmware it adds 12.8 k over the empty preset and after preset 01A's chain. Each voice pair costs
~6 k; the inner loop is ~44 instructions per voice and frame, near what the arithmetic needs (LFO, smoothing, the
read address, four loads and the cubic).

The reference implementation it was measured against costs 33 k with 2 voices and 87 k with 8, mostly its 16-tap
windowed-sinc read. A linear read would save 14..24 % more (10.9 k / 23.6 k), at -51 dB from the reference instead
of -62; the cubic was kept.

- State: 520 bytes (state pool, OCRAM).
- Buffer: 32.8 KB (buffer pool, SDRAM), the two lines.
- Code: 19 KB.

## Verification

- **Against the reference** (outside this repository): every type at two settings plus triangle, High Cut, slow
  and Mix cases, 6 s of a guitar DI. The wet signal is -51..-79 dB from the reference's (most -57..-66; the
  emphasis types with the 15 kHz pole are the worst, the rest of the difference is the interpolator's top octave:
  with High Cut at 3 kHz it is -76 dB).
- `tests/test_chorus.c` (host), see the file's header:
  - the descriptor;
  - the 23 types: finite, level -9..+3 dB;
  - the sweep and its period, free and synced;
  - stereo and mono;
  - bypass bit for bit, and the one-block fade;
  - knobs moved every block.
- `tools/check_modfx.py`: the module in the ARM emulator bit for bit the PC build at 9 settings. With `--fw`, it is
  installed over USB in the emulated firmware and plays, alone and after 01A.
- On the pedal: not yet (docs/verification.md, "Modulation blocks on the pedal").
