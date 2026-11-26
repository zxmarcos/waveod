# Effect blocks of waveod's own

The original firmware runs only the effects of its library (firmware section 1): ELF modules with a fixed
call, whose names, knobs and display formats live in the library entry. waveod keeps that library as it is and
adds effects of its own, written against one interface (`src/sdk/wvfx.h`) and shipped in two ways:

| | built-in | module |
|---|---|---|
| where the code is | the firmware image (compiled in) | an ELF in waveod's library in the flash |
| loaded | nothing to load: its state is allocated in the pools | into the pools like the original's modules, per slot |
| installed | with a firmware update | from waveod Studio (Efeitos tab), no firmware update |
| suits | what needs the firmware (the Cab / IR engine with its IR data and main-loop work), small utilities | DSP of any size (drives, delays, reverbs...); its code runs from the module pool in ITCM |

A block's source is the same either way: a descriptor (`struct wvfx_desc`) with its name, menu, parameters,
memory and three functions. The simulator and the host tests compile the built-ins for the PC, so (unlike the
original's modules) they run there too; a module from the library stays silent in the simulator, like the
original's.

## The interface (`src/sdk/wvfx.h`)

```c
struct wvfx_desc {
    uint32_t magic, abi;                  /* WVFX_MAGIC, WVFX_ABI */
    uint32_t id;                          /* WVFX_ID(menu, n) = 0x57MMnnnn */
    uint8_t menu, nparams, flags;
    const char *name, *category;          /* category = the type column of the models screen */
    const struct wvfx_param *params;
    uint32_t state_size, buffer_size;     /* per instance: OCRAM state pool / SDRAM buffer pool */
    int  (*init)(void *state, void *buffer, const struct wvfx_env *env, const float *knobs);
    void (*process)(void *state, const struct wvfx_io *io);     /* audio interrupt */
    int  (*poll)(void *state, const float *knobs);              /* main loop, may be 0 */
};
```

- **Parameters** (`struct wvfx_param`):
  - name, min, max, step, default, and the knob word it lives in (0..14, as in a preset slot);
  - a display format: 0 = the number; 0x01..0xC3 = the original's codes, e.g. 0x75 for "Center / R30 / L30";
    0xF0 / 0xF1 = signed / degrees;
  - the original's tempo-sync flag (`f2`);
  - and what the original's records cannot say: `options` (named values, `"Off\0On\0"`) and `unit` (a template,
    `"%.1f dB"`).
- **Memory**: for every instance the host allocates `state_size` bytes in the state pool (fast OCRAM) and
  `buffer_size` in the buffer pool (SDRAM). It passes both to each call, through the same pools as the
  original's modules and on the same side, so the spillover works for blocks too.
  - A block keeps nothing in mutable statics: the same code may run in two slots, and in the spillover's tail
    while the next preset loads.
  - Constant tables are fine.
  - `tools/wvfx.py build` refuses a module with writable sections.
- **Calls**:
  - `init` gets the knobs and `env`: sample rate, largest block, the slot, the item's data (the IR of a Cab / User IR item).
  - `process` gets `io`: the interleaved stereo buffer, in place; `aux`, the chain's input (guitar + FX return,
    for side chains); `send` / `ret` of the FX loop; the knobs; `enabled` (bypass is the block's job, as with
    the original's modules); the frames; ms per beat.
  - `poll` runs in the main loop (`chain_poll`), for work too slow for the interrupt.
- **Ids**: `0x57MMnnnn` never collides with the original's or the research builds' ids. A preset keeps the id;
  the original firmware falls back to the menu's first effect for it, as for any unknown id.
- **Engine blocks** (`WVFX_ENGINE`): a built-in that runs the items of a menu of the original's (Cab, User IR)
  instead of that menu's library entry, its parameters after the entry's (`WVFX_INHERIT`), so a preset keeps
  every knob. Two so far:
  - the Cab / IR engine and its virtual mic (docs/cab.md, `engine/cabir_block.c`): moved onto the interface
    without a sample changing (below);
  - NAM in the Clone slots (docs/nam.md, `engine/nam_block.c`): picked per item, for the Clone items whose data
    is a NAM capture; the others stay on the original's Clone module.
- **Replacement blocks** (`WVFX_REPLACE`, built-ins only): a built-in that runs a menu's library entry in place of
  the original's module, its own parameters after the entry's (`WVFX_INHERIT`). The slot keeps the entry's id,
  name and knobs, so a preset plays on either firmware; the block is not listed again (`blocks_replacement`).
  FX Loop, SND and RTN so far (below).

## Where the effects come from (`fw/catalog.c`)

The models screen, the editor's describe, waveod Studio and the presets see one list per menu, in this order:
1. the original's entries, at the indices they always had;
2. waveod's built-ins (`engine/blocks.c`);
3. the modules in waveod's library.

A slot stores (menu, id) as before; resolving by id finds an effect wherever it is.

The update file brings waveod's own effects with it (`tools/portfw.py`): every block of `src/blocks/*.c` that is
not a built-in nor a port of an original's effect (the circuit drives, the light modulations, the Delay, the
Reverb, Plate and Spring) is built as a module and added at the end of its menu in the effect library (section 1),
so they are in list 1 above on any pedal updated with it, without the Efeitos tab. Their entries name options and
units like waveod's library does: `fw/library.c` reads them for any entry with a waveod id (0x57MMnnnn; the
original's ids never start so). A module installed in waveod's library with an id the firmware already has (the
Efeitos tab put it there before) is not listed twice: the firmware's copy plays (`fw/catalog.c`).
`tools/check_ownfw.py FW.bin` checks it in the emulator.

A preset whose waveod effect is not installed (a module removed, a preset from another pedal) does not fall
back to the menu's first effect like the original's:
- the slot keeps its id and knobs (`RESOLVE_MISSING`, index `CATALOG_MISSING`);
- it shows as AUSENTE and passes the signal;
- installing the module brings it back, even in the preset being edited.

## waveod's library in the flash (`fw/wv_library.h`)

- **Place:** `FLASH_WV_LIBRARY` = 0x2A0000, up to waveod's preset store at 0x3A3000 (1036 KB).
  - It is inside the application partition, past the end of waveod's image (0x1D1000 today).
  - `tools/mkfw.py` refuses an image that reaches it, so firmware updates keep the library.
  - The original firmware's image (up to 0x3A0004) does overwrite it: going back to the original loses the
    library, but nothing else.
- **Format:** the container of the original's library (`fw/library.h`: menus, entries, parameter records,
  strings, then the ELF), so `fw/library.c` reads both, plus:
  - "WVLB" with a CRC-32 of the whole (checked at start-up);
  - in a parameter record, the first field (the original's -1) is an offset into the strings: the options
    (`LIB_P_OPTIONS` in f1) or the unit (`LIB_P_UNIT`).
- **The modules:** ELF files like the original's: four regions, SHT_REL relocations, loaded by
  `engine/module_loader.c`. The code region starts with `struct wvfx_module_header`: the original's header with
  "WVFX" and the descriptor in words the original leaves free, and Init / Process 0, so the original's app
  never calls one.
- **Writing** (group 0xF of the editor protocol, protocol 5: `editor/wv_msgs.c`):
  - **library:** where it is, its size and CRC. The PC reads it with the flash read.
  - **write:** 4 KB at a time from offset 0, each sector erased then programmed, with the audio suspended
    around each sector. The catalog stops reading the library at the first one. The 24-byte header is held back.
  - **commit:** the header is programmed and the whole image checked. Then the catalog reads it again, the
    edited preset's effects are found again by id and the chain rebuilt (its edits kept).
  - Until the commit the flash holds no valid library; a failed write leaves an empty one, never a broken one.

## Making a module

```
python tools/wvfx.py build src/blocks/stereo_util.c           -> out/fx/stereo_util.elf
python tools/wvfx.py lib out/wvlib.bin out/fx/a.elf out/fx/b.elf
python tools/wvfx.py show out/wvlib.bin
```

- **`build`:**
  - Compiles the block with `src/sdk/wvfx_module.c` (the header, `-DWVFX_DESC=<file name>_block`) and
    `src/sdk/wvfx_rt.c` (memset / memcpy).
  - Uses the Arm Toolchain for Embedded's clang for the Cortex-M7 with its FPU, without movw / movt (addresses come
    from literal pools).
  - Links with the toolchain's ld.lld and `src/sdk/wvfx.ld`, keeping the relocations, then rewrites the result into the form the loader
    takes.
- **`new`:** `python tools/wvfx.py new "My Block" --menu DRV` writes a block's source to start from: a level and
  a one-pole tone moving over one block, the bypass, the state, its descriptor named after the file
  (`my_block_block`), an id from the name in the users' range `0x8000..0xFFFF` (`--id` to choose), and in its
  header how to measure it.
- **Measuring while working on it** (docs/bench.md): the Studio's Desempenho tab takes the `.c` itself (built by
  `build` at every run, the compiler's errors shown) and with **Vigiar** measures it again whenever it is saved,
  each run against a reference: the cycles per block on the pedal, and the output's CRC (the same means the same
  output, bit for bit: an optimisation that keeps it changed nothing audible). From a terminal:
  `python tools/bench.py my_block.c --watch`. Nothing is written to the flash; the module runs from RAM.
- **The Studio:** the Efeitos tab installs `.elf` files and removes modules. It reads the pedal's library,
  changes it and writes it whole (`studio/waveod_studio/wvlib.py`, also used by `tools/wvfx.py`).
- **Emulator only:** `WAVEOD_EMU_WVLIB=LIB.bin python -m waveod_studio --emu FW.bin` starts the emulated pedal
  with a library in its flash.

## The first block: Stereo Util (VOL › Utility)

Gain (-24..+12 dB), Balance, Width (0..200 %, mid / side), Polarity (Normal / Invert L / Invert R / Invert L+R),
Channels (Stereo / Mono / Left / Right / Swap). All five are one 2 × 2 matrix on (L, R), moving over a block
when a knob or the switch changes; bypassed it is the identity.

## Gate SC and Comp SC (DYN › Sidechain)

Both have a KEY: Input, the clean guitar at the chain's input (`io->aux`), or Chain, the signal at the block.

- **Gate SC:**
  - Threshold, Range (90 dB = silence), Attack, Hold, Release.
  - It opens at the threshold and closes 6 dB lower after the hold.
  - Keyed by the input, it can sit after a high-gain amp and still open and close on what is played, not on the
    amp's hiss.
  - It starts open, and bypassed it glides back to unity.
- **Comp SC:**
  - Threshold, Ratio, Attack, Release, Knee (quadratic), Makeup, Mix (parallel compression).
  - Feed-forward and stereo-linked.
  - The key's peak falls with the release time (a decoupled peak detector, so a steady tone is measured at its
    peak); the reduction rises with the attack.
  - Bypassed it fades to the dry signal over a block.

Both are built in and build as modules too (2.4 / 2.6 KB).

## FX Loop, SND and RTN (the original's routing entries)

`blocks/fx_loop.c` runs the three in place of the original's modules (`WVFX_REPLACE`), with their knobs and
levels: Send Level x the chain onto FX SEND; Return Level x FX RETURN; Mix as the original (dry = min(1,
2 (1 - mix)), return = min(1, 2 mix): 0 the dry signal, 50 both at full level, a parallel loop, 100 the return
alone, a series loop). SND sends and passes the signal; RTN mixes the return and sends nothing. After the
original's knobs waveod adds Return Phase (Normal / Invert, FX Loop and RTN), for a parallel loop through a pedal
that inverts the polarity. What differs from the original:
- every gain moves over one block (knobs and the switch): no clicks;
- the send is added to the FX send, which the engine zeroes every block: two send blocks mix there instead of the
  last one winning, and a bypassed block leaves it alone instead of clearing it;
- the send is the chain's mono sum, (L + R) / 2, where the original took the left channel (the same in a mono
  chain).

The chain screen now offers the three menus (FX Loop, Send, Return) like the others.

## Circuit blocks (DRV › Circuit)

Six modules that run a drive pedal's circuit in real time (`sdk/wvdk.h`, the DK method; `sdk/wvos.h`, the
oversampling): Dark Muff, Dark Muff Lite, Green Drive, Rodent Dist, Round Fuzz, Treble Boost. Their matrices are
built again in `poll` when a knob moves, into a second set the audio switches to at a block. See docs/drives.md.

## Chorus (MOD › Chorus)

A module: a light multi-type chorus, one delay line per channel read by 1..4 voice pairs through a 4-point cubic,
23 types (sweep law, voices, filters, clip). No poll: the knobs' filters are rebuilt in `process` when one moves.
See docs/chorus.md.

## Flanger, Phaser, Rotary, Vibe, Tremolo (MOD)

Modules sharing `blocks/modfx/modfx.h` with the Chorus: a 32-type flanger (feedback loop in passes of 8 frames,
Thru-Zero), a 13-type phaser (2..12 all-pass stages, the stage count a constant of the inner loop) and a
rotating speaker (drum and horn, Doppler and level from a distance table), a lamp-and-photocell vibe (four
stages, each its own coefficient) and a tremolo / panner. See docs/modfx.md.

## Delay (DLY › Delay)

A module: 24 types on one engine (two heads per channel with a crossfade on Time jumps, cubic reads, the
feedback matrix by mode, diffuser, compander, ducker, sweep filter), the stages each type uses and nothing else.
See docs/delay.md.

## Reverb (RVB › Reverb)

A module: 50 types on one 16-line feedback-delay network (early reflections, input / early diffusers, modulated
lines by Quality, damping and low shelf per line, Householder feedback, the early-tap Hadamard matrix from High
on). The type table `blocks/reverb_types.h` is generated. See docs/reverb.md.

## Plate and Spring (RVB › Plate, RVB › Spring)

Two modules sharing `blocks/modfx/rvb.h`. Plate: 8 types, 16 lines at half the rate, each with a chain of
first-order all-passes (the dispersion), damping and low shelf, Householder feedback, R from short output lines.
Spring: 10 types, mono at a quarter of the rate, an input drive, four-line springs or pair tanks with a
Schroeder all-pass each, a tone filter per type. The type tables `blocks/plate_types.h`, `blocks/spring_types.h`
and the filters `blocks/modfx/rvb_fir.h` are generated. See docs/plate.md.

## Memory

The firmware's ITCM was nearly full (5.6 KB left) when these blocks and the modulators came in. The code the audio
interrupt never runs moved to SDRAM with the screens' pragma (`platform/sdram_code.h`): the editor protocol
(`editor/*.c`), waveod's preset format and control-table conversion, the library writer, the modulator routes.
That left 66 KB of ITCM. The OCRAM's `.bss` was full too (20 bytes left): the buffers only the main loop uses
moved to SDRAM (`SDRAM_BSS`, `platform/sdram.h`), the editor protocol's 96 KB of link and server buffers first, then
the rig, the screens' state, the tuner and the preset store's work buffers. The `.bss` went from 216 KB to 80 KB
(136 KB free); what stays is what the interrupts use.

## Verification

- `tests/test_blocks.c` (host):
  - every built-in's descriptor: ids, knobs not shared, defaults in range, one option per value;
  - Stereo Util's matrix: swap, polarity, gain, width, balance, bypass, the ramp;
  - Gate SC: opens with a loud key, closes with a quiet one, its range, keyed by the chain, bypassed;
  - Comp SC: -6 dB at 4:1 over -20 dB gives 10.5 dB of reduction, unity below the knee, mix 0 = dry, keyed by a
    quiet input only the makeup, bypassed;
  - FX Loop, SND, RTN: send x level, the Mix curve, Return Phase, bypass (dry, nothing sent), a knob ramping
    over one block;
  - the catalog: the built-ins after the library's entries, resolved by id, their parameters' text, a Cab
    item with its engine block and without it, the FX Loop / SND / RTN entries run by their replacement block;
  - a missing id kept, an unknown id of the original's still falling back;
  - a library image as packed: options, unit, CRC, a changed byte refused;
  - CRC-32 against the standard check value.
- `tools/check_blocks.py FW.bin --wvlib LIB.bin`: the whole firmware in the emulator, presets stored and
  loaded over USB like the Studio. The reference is a preset without blocks (01A's stock modules keep state
  across loads, so 01A never plays the same twice):

  | case | result |
  |---|---|
  | Stereo Util, gain -6 dB, Invert L | -134 dB from the expected output (+1.4 k instructions per block) |
  | two instances: balance R50, then Swap | L bit for bit, R -158 dB |
  | the block off | the reference, bit for bit |
  | installed over USB while its preset is the edited one | read back byte for byte; the preset plays it without a reload |
  | the module against the built-in, both cases | bit for bit (the module: 35.6 k against 35.9 k instructions) |
  | removed (an empty library), or an id nothing has | AUSENTE, kept in the preset with its knobs, the signal passes |
  | 01A + Stereo Util | +1.3 k instructions per block |

- The Cab / IR engine on the interface: `tools/check_spill.py` (01A, with its cab) gives the same three WAV
  files as before the move, sample for sample.
- The models screen and the edit screen in the simulator (VOL › Utility › Stereo Util, "0.0 dB", "Center",
  "100%", "Normal", "Stereo"); the Studio's Efeitos tab against the emulated pedal.
- Not on the pedal yet: see docs/verification.md, "Effect blocks on the pedal".
