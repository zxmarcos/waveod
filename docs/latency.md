# Latency test (GLOBAL › LATÊNCIA)

The pedal measures how long a sample takes from the input to the output, by itself (`engine/latency.c`).

## What the buffers give

Audio runs in blocks of 64 frames at 44.1 kHz, processed in the eDMA interrupt (`board/qme200/audio.c`). When
one half of the receive buffer is full, the interrupt processes it and writes the same half of the transmit
buffer; that half plays after the other one, which is playing at that moment. A sample captured at time *i*
comes out at *i* + 128: **128 samples, 2.90 ms**, whatever the chain costs (a chain that does not fit the block
gives clicks, counted as late blocks, not more delay). On top of that come the SAI FIFO and the converters'
digital filters (ES7243E ADCs; the DAC is not documented); the test measures them.

## Cable routes

| Route | Cable | Output word | Input |
|---|---|---|---|
| SAÍDA L » INPUT | OUT L → INPUT | main L | guitar (ADC0 R − L) |
| SAÍDA R » INPUT | OUT R → INPUT | main R | guitar |
| SEND » RETURN | FX SEND → FX RETURN | FX send | FX return (ADC1 R − L) |

1. The engine's outputs fade to silence in one block (`latency_quiet`); from the next block the test owns
   the converters' words (`latency_io`, at the end of `engine_audio`, after the output filter: nothing of the
   chain, AUX IN, USB or drums reaches the outputs while it runs). The chain keeps running and stays muted.
2. After a gap (8 blocks) a 512-sample chirp (300 Hz → 12 kHz, Hann window) starts at the first frame of a
   block on the chosen output word, and 4096 input samples are recorded from that same frame, before the input
   filter.
3. The main loop (`latency_poll`) cross-correlates the recording with the chirp, takes the strongest peak
   (absolute value: the input may invert the polarity) and refines it with a parabola through its neighbours.
   Output and input use the same frame counter, so the lag is the whole input → output time: buffers, FIFO,
   both converters.
4. Eight runs; the result is their mean, with the spread (more than 2 samples: INSTÁVEL) and the match of the
   weakest run (normalised correlation, 1 = an exact delayed copy).

The chirp starts at −30 dBFS and goes up to −20 and −10 while nothing comes back (match under 0.5). A
converter word of the input over 90% of full scale counts as clipping (either side: a TS cable grounds the
other one): the run is repeated 10 dB lower, and at −30 dBFS the test stops with SATUROU (the input type
LINHA lowers the analog gain). Nothing at −10 dBFS: SEM SINAL.

**The test plays a click on that output: turn the amplifier and headphones down or off.**

## PRESET (no cable)

The chirp (0.25 of the engine's full scale, a strong guitar) replaces the guitar at the chain's input
(`latency_chain_in`, after the looper; the FX return is silenced) and the chain's output is recorded
(`latency_chain_out`, both sides, the better match wins). The outputs stay silent. 48 blocks of silence go
before each run so tails die down. With reverbs or delays the first strong arrival counts (a peak at least a
quarter of the strongest, more than 16 samples before it), not the loudest repeat. The result is what the
preset adds (resampling of NAM / Clone, pitch, look-ahead); amp and cabinet blocks change the chirp a lot,
so the match only has to reach 0.2. Through the chain, a gate or a volume at zero gives SEM SINAL.

Pedal latency of a preset = the cable result + the PRESET result.

## Where it shows

- **GLOBAL › LATÊNCIA**: the four routes as cards (a tap, the knobs or MENU turns), what to plug, MEDIR /
  CANCELAR (MENU press or a tap) with a mark per run. The result: milliseconds in large figures, samples, a bar
  split into the buffers and the converters (or "what the preset adds"), the runs' range, the level and the
  match. Leaving the screen cancels a test. The GLOBAL card shows the last result.
- **GLOBAL › DIAGNÓSTICO**: the buffers' 128 samples and the last measurement.
- **Protocol 4** (`editor/wv_msgs.c`): `0x1F00000B` {u32 route, ~0 = cancel} starts the test and answers
  {i32 error}; `0x1F00000C` answers {u32 seq, u8 status, u8 route, u8 runs, i8 level, f32 min, max, mean, match,
  u16 runs, u16 buffers, u32 rate}. waveod Studio: the Pedal tab's Latência card, or
  `python -m waveod_studio latency [L|R|SEND|PRESET]`.

## Measured on the pedal

L, R and SEND / RETURN: 3.36 ms (about 148 samples: the buffers' 128 plus about 20 of converters and FIFO);
through preset 01A: 0.64 ms more (28.4 samples), the same as in the emulator. waveod Studio reads the same.

## Blocks of 32 frames (evaluated, set aside)

Halving the block would take the buffers from 128 to 64 samples (3.36 → about 1.9 ms). `tools/check_block.py`
runs every module of a library in Unicorn in blocks of 64 and of 32 and compares. On the V1.2.2 library (177
modules) 150 are bit-exact, the modulation effects and Auto Wah differ by −46 to −105 dB (LFOs and smoothing
step per call), Gate 3 / Fuzz Cream / Voks 15TB / FX Loop / RTN only ramp their knobs twice as fast, Harmony D
gives the same spectrum with other grain phases; the per-call overhead is +0.5% (median). But the **Cab and
User IR engines ignore the size and always process 64 frames**, so a 32-frame mode needs an IR engine of
waveod's own. The gain (1.45 ms) is below what most players notice, so the mode was left aside. waveod's own
Cab / IR engine (docs/cab.md) has since replaced the original's and takes blocks of 16, 32 or 64.

## Verification

- `tests/test_latency.c`: a simulated pedal whose outputs come back 128 + N samples later: exact lags,
  inverted polarity, half a sample (±0.2), a quiet return that needs a louder chirp, no cable, clipping, a TS
  cable, the PRESET route with a louder echo later, cancelling, and that no other output word sounds.
- The simulator (`loop:L:7`) and the ARM emulator (`loop:L:7`, `emu.Board.set_loopback`) model the cable:
  the firmware measures 135.0 samples (3.06 ms) there; the Studio's emulator test checks L and SEND over the
  protocol and runs PRESET through the active preset's real effect modules.
