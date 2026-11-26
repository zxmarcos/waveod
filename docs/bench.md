# Measuring effects on the pedal's CPU (protocol 11)

What an effect costs on the QME-200's Cortex-M7, in cycles, measured on the pedal itself. The instruction counts
from the emulator (Unicorn) are not cycles: the M7 issues two instructions per cycle at best, waits on SDRAM
behind its 32 KB data cache, and divisions and double-precision operations take several cycles. Only the pedal
tells.

Two measurements, both through waveod's USB MIDI protocol (group 0xF, `src/editor/wv_bench_msgs.c`), driven by
`tools/bench.py`:

- **bench** (`src/engine/bench.c`): one effect on its own. The PC names an item of the pedal's catalog (a
  built-in, an original module of the effect library, a block of waveod's library) or sends a module ELF into RAM
  (64 KB at most; never written to the flash). The pedal suspends the audio (silence), loads the effect into the
  side of the module pools the running preset does not use (`chain_load_one`), feeds it a test signal made on the
  pedal (plucked E2..E4 notes, 6 harmonics, a little noise; the same in the emulator), runs `skip` blocks
  unmeasured, then `blocks` measured with interrupts off around each process, and answers with the cycles of every
  block, their median / 99th percentile / max, the load + init cycles, the pools' bytes and a CRC-32 of the output
  and FX send. The running preset is not touched and plays again afterwards. `--cold` cleans and invalidates the
  whole data cache before every block (the worst case); `--bypass` measures the slot switched off.
- **profile**: the preset playing, as it plays. Every slot's process is timed in the audio interrupt
  (`chain.c run`, two reads of the cycle counter per slot and block); the PC clears the counts, waits, and reads
  mean / max / last per slot, plus the engine's whole block time. This is the cost with the chain's real cache
  state and the real input: the number that says whether a preset fits. Then the rest of the interrupt: the mean
  cycles of each section of `engine_audio` (`enum engine_section`, one read of the cycle counter per section),
  the chain's section minus the slots being what runs between them. On 2026-11-24, with a 7-slot preset playing
  (global EQ off, no drums, no USB audio), the firmware's own cost besides the slots went 52.2k -> 23.4k cycles
  per block: the drum machine mixed its 16 voices even when silent (11.8k -> 0.7k), the converters' words read one
  non-cacheable word at a time (8.0k -> 3.2k with LDM bursts to the stack), the silent FX send went through its
  filter (now skipped while it and the filter are at rest: 0.8k for the check, read as integers; as floats each
  word's move to a core register made the check cost 5k), the filters' state kept in registers, the global EQ
  stage by stage, the meters' and DAC limits without float compares. All with the same converter words as before
  (emulator A/B, global EQ on and off; tools/check_drums.py).

The time is the cycle counter (DWT CYCCNT) around the process call, less an empty measurement (two reads back to
back). A 64-frame block at 44.1 kHz is 1.451 ms: 870,748 cycles at 600 MHz (the core clock is measured at
start-up and reported with every answer).

```sh
python tools/bench.py id:0x0700002A src/blocks/amps/brit45.c --blocks 1000 --json out/bench_hw.json
python tools/bench.py AMP:"Brit 45" AMP:* --cold
python tools/bench.py --profile 5
python tools/bench.py --list AMP
# the same in the emulator: same CRCs, the pedal computed what the emulator does (its cycles mean nothing)
python tools/bench.py --emu FW.bin id:0x0700002A src/blocks/amps/brit45.c --blocks 1000 --against out/bench_hw.json
```

## In waveod Studio

The **Desempenho** tab (studio/ui/src/components/PerfScreen.svelte, the calls in studio/waveod_studio/bench.py,
which tools/bench.py uses too) shows both:

- the preset playing, read every second (each read resets the counts, so it covers the second before): one bar for
  the block, each slot in its menu's colour, the firmware around them and the room left, the worst block marked;
  the slots in chain order with mean / max / last; the firmware's sections on demand;
- the bench of a model of the pedal, of a module `.elf` or of a block's `.c` (built by the SDK, `tools/wvfx.py
  build`, at every run; a source that does not build shows the compiler's words), cold cache and bypass as
  options; the runs stay listed against a reference run: the median's change and whether the CRC is the same (the
  same output, bit for bit), with the cycles of every block drawn. **Vigiar** measures the file again whenever it
  is saved (for a `.c`, also any `.c` / `.h` beside it): edit, save, read the change.

The same loop from a terminal: `python tools/bench.py my_block.c --watch` (each run against the first: the
median's change, CRC the same or not; a failed build shows the compiler's words and the watch goes on). A new
block to start from: `python tools/wvfx.py new "My Block" --menu DRV` (docs/blocks.md).

In the emulator (`--emu`) the tab works the same, the profile playing half a second of audio per read; its cycles
mean nothing there, its CRCs do.

## Protocol 11

| cmd | request | answer |
|---|---|---|
| 0x1F00001B bench elf | {u32 offset, u32 total <= 64 KB, u32 n <= 4096, bytes}, in order from 0 | {u32 offset, i32 error} |
| 0x1F00001C bench | {u32 source (0 catalog, 1 the ELF sent), i32 menu, i32 index, u32 blocks (1..2000), u32 skip, u32 flags (1 cold, 2 bypassed), f32 level, u32 seed, u32 bpm, f32 knobs[15] (NaN: the item's default, then 0)} | {i32 status, u32 load cycles, u32 overhead, u32 blocks, u32 min, median, p99, max, f32 mean, u32 CRC, f32 peak, u32 pool bytes[4], u32 core Hz, u32 0, f32 knobs[15], u32 cycles[blocks]} |
| 0x1F00001D profile | {u32 reset} | {u32 core Hz, f32 block ms, f32 worst ms, u32 audio blocks, u32 n, per used slot in chain order {u8 slot, i8 menu, u16 0, u32 id, u32 blocks, u32 mean, u32 max, u32 last}, u32 blocks, u32 m, u32 mean cycles[m] (engine_section)} |

status: 0, or `enum module_error` of the load (-1 bad ELF, -2 the pools are full, -3 relocation, -4 header,
-5 init refused), -10 a bad request, -11 no complete ELF.

## Limits

- The bench answers when it is done: up to `skip + blocks` blocks of the effect plus the test signal (a few
  seconds); the main loop (screen, USB) waits meanwhile.
- A module that crashes takes the pedal through the crash protection (record, reset), as it would in a preset.
- In the emulator the cycle counter steps by a fixed amount per read: the cycles read 0 there; the CRCs are what
  the emulator run is for.
