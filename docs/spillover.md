# Spillover

When the preset changes, the delays and reverbs of the preset that was playing ring out over the next one,
like a real pedalboard. The original firmware cuts the sound at every change: it empties the module pools,
loads the new preset and fades back in.

GLOBAL › RASTRO NA TROCA sets how long a tail may ring: DESLIGADO (the original's behaviour), up to 2, 5, 10
(factory) or 20 s. The setting is in waveod's settings record (the byte after MOTOR DE CAB / IR; 0 in older
records means the factory value).

## How it works

**Two presets in memory.** The effect modules live in four pools at the original's addresses
(`engine/pools.h`). waveod fills each pool from both ends: a preset is loaded into one side while the other keeps
the previous one, its modules intact and still running. The next change loads into the side of the tail before
it. Every pair of the 240 factory presets fits side by side: the largest uses 42 KB of the 128 KB code pool, 14 of
128 KB of constants, 67 of 192 KB of state and 3.3 of 6.8 MB of buffers (`tests/test_spill.c`). When the new
preset does not fit beside the tail, the tail is cut and the preset gets the whole pools, as without the
spillover.

**The handover** (`engine_spill`, `engine/audio_engine.c`). `rig_load` makes a copy of the running chain
(`chain_tail`, `engine/chain_tail.c`): the same loaded modules, with their knobs and on/off states. At the next audio block
the interrupt runs that copy instead of the chain, so the module state carries on without a break; the
main loop then loads the new preset into the other side (`chain_build_side`) and resumes it, fading in over
four blocks as before.

**What keeps running.** The guitar leaves the old preset over 4 blocks (6 ms). The whole old chain keeps running
until the slots before its delays and reverbs are silent, because an amp, its cab and EQ still give out sound for
100-300 ms after the guitar stops. "Silent" means their output stays under -100 dB of full scale for 4 blocks;
the limit is 300 ms, for a block with a noise of its own. From then on, while the new preset plays, only the
tail's slots run: a delay or reverb that is on, and every slot after it on its path. With parallel lanes, a
lane counts from its split, and the slots after the merge count when either lane had one. Their input is
silence and their output is at the old preset's volume and pan, added after the new preset's level, so it goes
through the global EQ, the outputs' filters and MASTER like the rest. A tail's FX send goes nowhere and its FX return is
silent.

**When it ends:**

- after the time set, with the last second faded out;
- after 2.5 s of silence (longer than the repeats of a delay);
- at the next preset change, faded out over 4 blocks (one level of spillover, like most pedalboards);
- at anything that suspends the engine (a chain edit, an effect library written from the PC), at the tuner's mute, and on slow
  blocks. A block over 1.05 ms first ends the drain: the slots before the delays and reverbs stop at once,
  and what they still give out is lost. If the blocks stay slow, the tail is cut within the next block, before
  the overload guard (1.25 ms) can switch the preset off.

Only `rig_load` spills; reloading the same preset after an edit (chain changes, MOTOR DE CAB / IR) does not.
The diagnostics panel counts the tails and the ones cut for CPU.

## Verification

- `tests/test_spill.c`:
  - the pools' two sides: alignment, no overlap, one side emptied with the other kept;
  - the slots a tail keeps in series, parallel and dual routing, with delays on and off;
  - a knob still moving ends at its target in the copy;
  - every pair of factory presets fits side by side;
  - preset 01B loads above 01A for real.
- `tools/check_spill.py FW.bin` runs the whole firmware in the emulator with the stock modules. A tone plays into
  01A (Gate, Blues OD, Brit 50JP, its cab, EQ, Room on), then the guitar stops and B is pressed while the audio
  runs. It compares three runs, each against the tone's level:

  | after the change | no change (reference) | spillover | spillover - reference | spillover off |
  |---|---|---|---|---|
  | 0-30 ms | +1.5 dB | +1.5 | -240 dB | +1.5 |
  | 30-100 ms | -5.7 | -5.7 | -240 | -6.1 (cut) |
  | 100-300 ms | -24.0 | -24.0 | -116.5 | silence |
  | 300-800 ms | -45.4 | -45.4 | -139.4 | silence |
  | 0.8-1.8 s | -100.7 | -100.7 | -178.8 | silence |

  So the tail is the old preset ringing out as if nothing had changed: bit for bit while the whole old chain runs,
  and within -116 dB once only Room and Volume do. With a fixed 23 ms before that (the first version) the
  difference was -29 dB: the amp still sounded.
- Cost (instructions per audio block): 249 k for 01A playing; 275 k right after the handover (the old chain
  alone); 410 k during the drain with 01B running too; 206 k once only the tail and 01B run.
- `--keys B,C` (two quick changes): the first tail fades out in 6 ms from -51 dB without a step larger than the
  signal's own, and nothing faults. `--cpu 1100` (every block measured at 1.1 ms): the tail is cut and there is
  no overload.
- The setting: GLOBAL screen in the simulator and the emulator, and it is still there after a reboot.
- Not on the pedal yet.
