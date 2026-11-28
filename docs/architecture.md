# Architecture

```
            app (main loop)
   input ──► ui (LVGL screens) ──► model (rig, topology, assign, param_format)
                                        │
                                        ▼
                           engine (chain, module_loader, pools, audio_engine)
                                        │
                     fw (library, presets, record_store, ir_store, catalog)
                                        │
                               platform interface
                         ┌──────────────┴──────────────┐
                   board/qme200                       host
```

Every layer only talks to the ones below it; only `src/board` and `src/host` touch hardware (or pretend to).

## Compatibility with the stock firmware

The effect modules (`.axf` in the effect library) are built for the stock application, so the parts they
depend on are reproduced exactly:

- **Loader** (`engine/module_loader.c`): sections placed into four pools by address region and name, SHT_REL
  relocations with the pool bases, header tag `0xFFAA0000 | index`. Cross-checked against an independent
  implementation on all 179 stock modules (`tools/check_loader.py`). The same loader takes the modules of waveod's
  own library, recognised by a `WVFX` header ([blocks.md](blocks.md)).
- **Pools** (`engine/pools.c`): at the stock addresses (ITCM 0x58000, SDRAM 0x81301000 / 0x81321000,
  OCRAM 0x20204000) with the same sizes, emptied and refilled in slot order when the chain is rebuilt; a
  module that does not fit reports `MODULE_NO_MEMORY` like the stock "System overload" (flag 2). waveod's
  addition: each pool also fills from its end, so a preset change loads into one side while the previous preset's
  tails ring out from the other ([spillover.md](spillover.md)). waveod's own blocks take their memory from the
  same pools ([blocks.md](blocks.md)).
- **Calling convention** (`engine/module_call.h`): `Init(knobs, &ctx)` with ctx = 60000 / BPM, or the flash
  address of the IR / capture for Cab, User IR and Clone; `Process(io, 64, knobs, aux, &enabled, &beat, extra)`.
- **Sample scaling** (`engine/audio_engine.c`): guitar = (ADC0 R / 2 - ADC0 L / 2) x input level / 980586112,
  output = io x volume x 980586112, preset volume x 0.02 with pan, MASTER squared.
- **Signal path around the chain** (`engine/audio_engine.c`, the original's `FUN_00031E18`): the guitar and the
  FX return go through a fixed biquad H(z) before the chain and the outputs (and FX send) through 1 / H(z) after
  it (`dsp/io_filter.c`, about -13 dB at the top of the band and back: the stock effects see that spectrum);
  the Gates' side chain is guitar + FX return; after the chain come the preset level, the global EQ
  (`dsp/global_eq.c`, `FUN_00029634`), the AUX IN and MASTER. With an output set to "no cab", Cab and User IR
  blocks run after the whole chain and that side keeps the signal from before them. `tools/check_dsp.py` runs
  the original's EQ and filters in Unicorn: the filters, and the EQ given the original's coefficients, are
  identical bit for bit (built with `-ffp-contract=off`, as the original uses separate multiply and add); the
  EQ coefficients waveod designs itself are within 3e-7 (the math libraries differ).
- **RAM words read by modules** (`engine/host_abi.h`): block toggle 0x20234025 and module header table
  0x20241F44 (parallel routing), block time 0x20238198 and CPU word 0x202382A0 (NAM Clone meter). The linker
  script reserves 0x20234000-0x20242000 for them.
- **MPU map and SysTick**: the stock values (modules keep writable data in ITCM, the NAM module reads
  SYST_CVR expecting a 1 ms reload).

## Flash data

Read with the original's formats (`src/fw`), plus waveod's own preset store:

| Area | Offset | Reader |
|---|---|---|
| waveod's presets (own store, `fw/wv_store.h`) | 0x3A3000 | `wv_store.c`, `presets.c` |
| Clone captures (own file system) | 0x40A000 | `ir_store.c` |
| Factory presets | 0x440000 | `presets.c` |
| Effect library | 0x480000 | `library.c` |
| IR directory + factory cabs | 0x700000 | `ir_store.c` |
| User IRs (own file system) | 0x753000 | `ir_store.c` |
| Record store: settings, preset map, user presets (presets read only) | 0x780000 | `record_store.c`, `presets.c` |

Place `i` follows waveod's map (`fw/presets.h`): one of waveod's records, the original's preset `s` or empty.
The original's preset `s` is record `map[s]` of the record store when present (user preset), otherwise factory
preset `map[s] - 2`, exactly like the stock loader; ids are resolved per menu with the stock fallbacks
(`fw/catalog.c`). Until waveod saves, every place is the original's preset of the same number.

**waveod Studio** (`studio/`, the PC app for this firmware) uses group 0xF of the editor protocol,
waveod's own (`editor/wv_msgs.c`): pedal info, the preset list, presets in waveod's format (get / put, resolved like
`rig_load`), load, move, copy, flash reads, and (protocol 2) describe: a preset's control table as the pedal plays it
(converted from the stock fields when it has none) with its blocks' names and parameter ranges, so the Studio's
controls editor (`studio/waveod_studio/ui/controlsview.py`) needs no effect library. The Studio writes the table
back as chunk 11 of the preset it puts. Protocol 3 adds the global control table: `0x1F000009` answers it as a
chunk 11, `0x1F00000A` {u32 n, the chunk} stores it (`rig_store_global`) and answers {i32 error}. Protocol 4
adds the latency test: `0x1F00000B` starts or cancels it, `0x1F00000C` answers its result
([latency.md](latency.md)). Protocol 8 (`editor/wv_rig_msgs.c`) serves the Studio's new GRID screen: `0x1F000015`
the edited preset as the chain screen shows it (blocks with their cells, routing and joints, scenes, `gen` that
moves on every change), `0x1F000016` a block's parameters with values, texts and named options as the pedal
formats them, `0x1F000017` the catalog of a menu by page, and `0x1F000018` live edits that call the same rig
functions as the pedal's screens (parameter, on/off, `rig_place` / `rig_move_cell` / `rig_replace` / remove,
`rig_route`, `rig_joint`, `rig_scene`, volume, BPM, and `rig_save`). Protocol 9 (`editor/wv_settings_msgs.c`) serves
the Studio's GLOBAL tab: `0x1F000019` lists the GLOBAL screen's settings, both kinds (the stock record 0 and waveod's
own `struct settings`) and the global EQ's 31 parameters, each with its value, range and step; `0x1F00001A` changes a
list of them the way `scr_settings.c` does (same ranges, same effects: applied at once, the chain rebuilt for the
Cab / IR engine) and answers the list again. The records are written after 2 s without changes
(`globals_touch` / `settings_touch`); `globals_generation` / `settings_generation` make the pedal's screens redraw.
Protocol 10 adds the scene menu's edits to `0x1F000018`: pick a scene to edit (`scene_chosen`, as the top bar's
C1-C4 menu), copy one over another, revert one to the stored preset, rename one (the name after the value) and make a
parameter a scene parameter or the preset's again (`rig_scene_param_toggle`, the KNOBS NA CENA switch).

**Reading the flash from a PC** (for analysis): in the same group,
`0x1F000000` {u32 offset, u32 length ≤ 0x2000} answers {offset, length, the bytes} from anywhere in the 8 MB
(`editor/wv_msgs.c`, read only); `tools/flashdump.py` dumps the bootloader, a range or the whole flash, and
`--summary` tells what each area and each gap between the update sections holds. The diagnostics panel
(GLOBAL › DIAGNÓSTICO) shows the same gaps on the pedal: the app partition after this image, after the original's
app (0x3A0000-0x40A000), the library partition after the library and after the factory IRs. Bytes left in the
first one are the original's app, which tells that the bootloader writes only what a section holds.

## Writing the flash

Only data areas are ever written: waveod's preset store (0x3A3000), the Clone file system (0x40A000), the user IR
file system (0x753000) and the record store (0x780000, the shared settings only). `platform/flash.h` refuses erase / program anywhere else on both platforms, so
the bootloader, the application, the effects and the factory IRs and presets cannot be touched.

- **Format** (`fw/record_store_write.c`): the original's algorithm (V1.2.2 `FUN_00026160` start-up scan,
  `FUN_000262F4` write). A record is written by moving its group of three to the next free sector: erase,
  copy the group's other valid records, program the record and the header (sequence + 1), then the 0x8E8A
  mark, then 0x8E80 on the old copy. A power cut leaves one readable copy at every step; the start-up scan
  (`store_mount`) erases what is left over, like the original. The only addition is a read-back before the
  commit mark. `tools/check_store.py` runs the original's code in Unicorn with the same writes and compares
  the 512 KB images byte for byte.
- **Presets** (`fw/presets.c`, `fw/wv_preset.c`, `fw/wv_store.c`): waveod's own format (chunks with tags, every
  field of the preset, unknown chunks skipped) in waveod's own store at 0x3A3000-0x40A000 (412 KB), a log of records
  in 4 KB sectors with CRCs: the newest record of a key wins, the oldest sector is cleaned up when free sectors run
  low, a record cut by a power failure is ignored, leftovers of earlier firmware are erased when a sector is first
  needed. A map record says what each of the 240 places holds; moving presets rewrites only the map. The original's
  record store is never written with presets again, so going back to the original finds its presets as they were.
  The area is outside every update: the bootloader erases only the sectors a section reaches (research/NOTES.md,
  `Dump da flash`) and `tools/mkfw.py` refuses an application section that would reach 0x3A3000. The PC editor's
  preset messages still work through the original's record format (`presets_read` / `presets_save` convert).
  Saving also sets the power-up preset (settings record 0, +0x0C; the record is created with the original's
  factory values when missing).
- **Footswitch links** (`struct preset_controls` in `fw/preset_format.h`, chunk 11, `fw/preset_controls.c`,
  `model/control.c`): up to 48 links {key, mode bits, press / hold / momentary, block / parameter / scene /
  function, slot, argument, OFF / ON values}, plus a name and a LED colour per key. A preset without the chunk gets
  the table from the stock fields `fs` / `ctrl` on load, and while the table is still exactly that conversion the
  stock chunks are written instead (factory-converted presets stay as small as before). A key's block and
  parameter links share one state read back from the first of them, so nothing about it is stored.
  The global table (`rig->global`, the same struct; functions, scenes and MIDI out only) is a chunk 11 in
  waveod's store under (WVS_MAP, key 1) (`presets_global_controls` / `presets_store_global_controls`; older builds
  index it without using it). `control_table` picks per group (key, mode, press or long press): the preset's table
  when it has a link there (CA_NOTHING too), else the global one; every `control_*` call goes through it, so the
  LEDs, the stomp cards and MIDI follow. The stock conversion (`preset_controls_legacy`) drops the groups equal to
  the factory global table (`preset_controls_factory`, the factory presets' `fs` fields) and adds CA_NOTHING where
  the field is empty but the factory table is not; `test_controls` checks over the 240 factory presets that preset
  + factory global table = the full conversion (`preset_controls_original`). MIDI out state of the global links
  (`rig->global_sent`) survives preset loads.
  `ui/footswitch.c` hands the press / hold / down / up events to `control_press` / `control_moment`; functions
  and scenes come back to it through a callback. The pedals (EXP 1A / 1B / 2) are keys of the same table:
  `control_exp` puts their parameter links at heel..toe along a curve with `rig_knob_glide`, which moves the
  knob word over 8 audio blocks through `chain_ramp` (a small table the interrupt steps before the chain runs)
  when the parameter has 20 steps or more; auto blocks go on off the heel and off after 500 ms at it
  (`control_exp_tick`). Converted EXP targets keep the parameter in library order (CF_ORDER) so the conversion
  needs no library. The toe switch is the key CK_TOE. Learning (`ui_learn`): while a screen waits for a key,
  `ui/footswitch.c` hands the pressed key to its callback instead of acting, and `ui_tick` polls
  `expression_learn` instead of `expression_poll` (a pedal counts after a quarter of its range and 0.3 s at rest;
  the toe switch at once); nothing reaches the rig meanwhile, and the next poll after it brings the pedals' targets
  to where they are. Changing screens stops it. DE ONDE (`scr_link.c`) adds links with `control_add_param`;
  PARA ONDE (`scr_target.c`) edits one link in place by its table index (a new one is a no-op function link,
  removed in the screen's `leave` when nothing was picked). Every screen's touch bindings are made at start-up (512, in the SDRAM).
  MIDI: a control change is the key CK_MIDI + its number (0x80..0xFF in the same u8 field, every mode);
  `ui/midi_control.c` hands it to `control_midi` when the preset has links on it (else its fixed meaning), or to
  the learning callback. A MIDI out link (CA_MIDI: channel in `slot`, number in `arg`, CF_PC = program change)
  sends through `midi_send` on both outputs; its CC is stateful like a block, the state being the value sent last
  (`rig.midi_sent`, by table index, cleared on load), and a pedal's or a CC's sweeps it, sent when it changes.
- **Scenes** (`struct preset_scenes` in `fw/preset_format.h`, chunk 10 of waveod's format, written only when they
  differ from the plain blocks): active scene, then per scene a u16 mask of the slots on and a name (length +
  bytes), then the scene parameters {slot, knob position, f32 value per scene}; 4 scenes, 32 parameters, about 60
  bytes for a typical preset. The slots always hold the active scene, so a reader that skips the chunk plays it;
  on load the active scene is taken from the slots. `rig_scene` (model/rig.c) sets `enabled` and the knob words the
  modules read every block, no rebuild; `set_on` / `set_knob` write into the active scene, except moves of the
  expression pedals (`rig.performing`). While `rig_scene_editing` (scene mode, scenes that differ, or
  `rig.scene_chosen` from the scene menu) and `rig.scene_auto` (settings `scene_manual` = 0), `set_knob` makes a
  parameter it moves a scene parameter first (`scene_capture`), unless the move stays the preset's
  (`rig.preset_wide`: footswitch parameter links, a pasted block). `rig_scene_revert` takes one scene back from the
  stored preset. Scene mode is the footswitch mode in waveod's settings record (`scene_mode`,
  the byte that was reserved), so the shared global `fs_group` keeps the original's two values. `WVP_MAX` (3072) is
  the largest encoding with every chunk full (tests/test_scenes.c checks it).
- **Global settings** (`fw/globals_format.c`): record 0, see "Settings" below; only the fields that changed
  are written over the stored record.
- **User IRs and Clone captures** (`fw/ir_fs.c`, written for the PC editor): the original's two small file systems,
  same algorithm (`FUN_00025A84` / `FUN_00025D30` write: data to a free block, then the entry in place ('_' then 'Z')
  or a copy of the directory sector marked 0x5AFA with the old one marked 0x5AF8; renames copy the sector; a Clone
  deletion clears the entry's marker). `tools/check_irfs.py` compares the images with the original's code byte for
  byte. waveod has 20 Clone slots, the original's 10 and 10 more in the same directory part that the original keeps
  but never reads (docs/nam.md, "Twenty slots").
- **Device** (`board/qme200/flexspi.c`, `flash.c`): FlexSPI IP commands with four LUT sequences (write enable,
  read status, 4 KB erase, page program) added in slots the bootloader's memory-mapped read does not use;
  after each command the AHB buffers are reset so the memory map shows the new data (the MPU keeps the flash
  region non-cacheable, as in the original). Every erase and program is read back.
- **While writing** the flash cannot be read, and the audio does not read it: the original IR / Clone modules,
  which read their item's data from the address they get (the original gives them the flash's), get a copy in the
  buffer pool (`engine/chain.c`, `data_copy`). So writing presets, the globals and the settings
  (`engine_flash_begin` / `engine_flash_end`) leaves the sound alone, while the UI waits. Only when a copy found no
  room (the slot is marked in `chain.flash_data`) is the engine suspended around the write, as the original does:
  silence for the erase and the programming. Before, every write muted the sound, which was heard ~2 s after a
  change of the footswitch mode or of a setting, when they are stored.

## Memory map

See the comment at the top of `src/board/qme200/qme200.ld`. Application code and constants run from ITCM, except
main-loop code moved to SDRAM with `platform/sdram_code.h` (the screens, the editor protocol, waveod's preset format,
the effect library writer, the modulator routes); LVGL, fonts and initialised data live in SDRAM (cached). The
OCRAM's `.bss` keeps what the interrupts use (the audio engine, the looper, USB and MIDI rings, the drums);
main-loop buffers are `SDRAM_BSS` (`platform/sdram.h`): the editor protocol's link and server buffers, the rig,
the screens' state, the tuner, the preset store's work buffers. The audio DMA buffers sit in the non-cacheable first 16 KB of
the OCRAM, the USB controller's queue heads, descriptors and buffers in its non-cacheable last 32 KB (an MPU region
of waveod's own, R9); the frame buffers are the stock ones at 0x81F00000 (write-through).

## Touch

The panel's capacitive touch controller is a Goodix GT911, found in the stock firmware (set-up
`FUN_0001E0A8`, report read `FUN_0001E290`, GPIO3 interrupt `FUN_00026D40`):

| Signal | Pin |
|---|---|
| I2C SCL / SDA (bit-banged, ~300 kHz in the original) | GPIO3[4] / GPIO3[5] |
| Reset (active low) | GPIO1[3] |
| INT (falling edge per report) | GPIO3[3], pad GPIO_SD_B1_03 |

Address 0x14 (INT pulled up while reset is released); 16-bit register addresses. The original reads the product
id (0x8140), the configured resolution (0x8048) and the switches (0x804C/0x804D) at start-up, then on every INT
reads six bytes from 0x814E (status: bit 7 = report ready, bits 0-3 = points; then x and y of the first
point) and writes 0 to 0x814E. The coordinates go to its UI unchanged.

`board/qme200/touch_gt911.c` does the same from the main loop rather than the interrupt: the INT edge is only
latched in the GPIO edge status, and the report is read when it is set, every 10 ms while a finger is down,
and every 100 ms if no edge has ever been seen (in case the INT polarity differs). The I2C master
(`board/qme200/i2c_soft.c`) is shared with the codec bus. In `src/ui`, `gesture.c` turns the panel into an
LVGL pointer and the press events into taps, long presses and drags for the screens.

## Tuner

The audio interrupt hands the guitar input, averaged in pairs (22.05 kHz), to a 2048-sample ring while the
tuner runs, and stops running the chain when the output is muted. The main loop (`model/tuner.c`) analyses
the last 1024 samples every 256 new ones with YIN (`dsp/pitch.c`): the difference function up to 52 Hz, the
first dip under 0.15, then the period measured over as many whole periods as the range holds (the parabolic
fit is that many times finer). On synthetic strings from 55 Hz to 1 kHz the error stays under 0.1 cent
(`tests/test_tuner.c`). Cents are smoothed while the note holds; a new note must be seen twice.

## Settings

Two sets, both edited on the GLOBAL screen:

- **The original's global settings** (`model/globals.c`, `fw/globals_format.c`): record 0 of the store, a copy
  of the first 0x238 bytes of the original's state block (read by `FUN_000399F0`; layout in
  `fw/globals_format.h`, from the editor protocol's reply `FUN_0002D120` and the audio loop). waveod uses the
  input level (-20..+20 dB, table `0x47070`), the input mode (two analog switches on the shift register, virtual
  pins 0x19 / 0x1A = inverted mode bits, `FUN_00022AE0`), the AUX IN level, the outputs without cabinet and the
  global EQ; everything else in the record (MIDI, footswitch functions, drum machine, display...) is kept as
  stored, so the two firmwares share these settings. The factory record (`0x42E04`) and the EQ ranges
  (`0x428E4`) are generated into `fw/globals_default.c` by `tools/gen_defaults.py`.
- **waveod's own** (`model/settings.c`: brightness, A4, tuner output, touch marker), stored by
  `fw/settings_record.c` in a record of the original's store that its preset map does not point at (299
  normally), tagged "WVOD": the original only reads records 0, 1 and the ones its map names, so it never sees it.

## Core clock

`board/qme200/clock.c` sets the core like the original's `FUN_0001BCA4` (from its MMIO trace,
`research/extracted/mmio_trace.txt`): periph_clk on the crystal, VDD_SOC 1.275 V (DCDC_REG3.TRG 0x13), ARM PLL 24 MHz x
100 / 2 = 1200 MHz, ARM_PODF /2 = 600 MHz, AHB /1, IPG /4 = 150 MHz, periph_clk back on PLL1. It writes nothing when
the bootloader already left exactly that. The rest of the original's clock function is not repeated: the SDRAM,
FlexSPI and system PLL dividers stay as the bootloader set them (they are in use while this code runs), the video
and USB PLLs are set by their drivers, the UART baud rate is computed from the UART clock found, and the GPTs count
the crystal. The diagnostics show the clock measured before and after (`CPU 600 MHz (bootloader N)`).

## Crash protection

`board/qme200/safety.c` (interface `platform/safety.h`):

- **Faults.** HardFault, MemManage, BusFault, UsageFault (the last three enabled in SHCSR), NMI and every interrupt
  without a handler enter `fault_entry`: it moves to a stack of its own, zeroes the audio output buffers (the DMA
  streams silence), writes a record to SNVS_LPGPR0-3 and resets through AIRCR.SYSRESETREQ without the bootloader's
  update request, so the application starts again. The SNVS low-power registers survive a warm reset and are
  lost at power-off.
- **Record** (4 words, magic 0xC5A5 and an 8-bit check): cause, crashes in a row (one counts as "in a row" when
  it came less than 60 s after the start-up before it), the preset loaded, the chain slot whose module code was
  running (`chain.c` notes it around every `Init` in the main loop and every `Process` in the audio
  interrupt), whether the audio interrupt was running (IPSR in the stacked xPSR), the PC, CFSR, the uptime.
- **Watchdogs**, armed at the end of the start-up: a software one in SysTick (priority 0, above the audio
  interrupt) that resets when the main loop has not called `safety_feed` for 1.5 s, with the same record; and the
  RTWDOG (3 s) as the backstop for when interrupts no longer run. The RTWDOG's clock (LPO, 32 kHz) is measured
  against the cycle counter before the timeout is set, with the /256 prescaler first so no clock can expire it
  during the measurement; an unexpected clock only switches it off. CS.UPDATE stays set so the bootloader can
  reconfigure it, and it is switched off before a reset into the bootloader. A reset by the RTWDOG is seen at the
  next start-up in SRC_SRSR. Flash erases / programs and every module `Init` feed the watchdogs.
- **Next start-up** (`app/recovery.c`): when a module of the power-up preset crashed less than 20 s after the
  start-up, or the pedal crashed twice in a row with it loaded, the rig starts at its place on an empty chain
  named MODO SEGURO (`rig_init(r, avoid)`) and a warning names the preset, the block and the cause; holding
  EXIT at power-up does the same on request. Otherwise the warning only tells that the pedal restarted. The
  record is in the diagnostics panel.
- **Tests** on the pedal: GLOBAL › SISTEMA › PROTEÇÃO (an undefined instruction, a main loop that stops, a main loop
  that stops with interrupts off).

## Build note: the FPU

The firmware is built for the RT1062's FPU explicitly: `cmake/atfe.cmake` passes `-mcpu=cortex-m7
-mfpu=fpv5-d16 -mfloat-abi=hard` (FPv5-D16, double precision). A CPU model without the FPU feature would turn
every float operation of waveod into a library call (the stock modules are not affected: they come compiled).

## Audio and UI concurrency

The audio interrupt (eDMA, every 64 frames) runs `engine_audio`, which processes the chain in place. The main
loop owns the model and the UI. Knob values and on/off flags are single words the interrupt reads every block;
structural changes go through `engine_suspend()` (one more block fades to silence, then the interrupt stops
touching the chain), `chain_build()` and `engine_resume()` (fade in). A preset change hands the running chain
to the interrupt as a tail instead (`engine_spill`), then builds the new one into the other side of the pools
([spillover.md](spillover.md)).

Input to output takes 128 samples (2.90 ms) of buffers plus the converters: a half of the transmit buffer
written in a block plays after the half playing at that moment. GLOBAL › LATÊNCIA measures the whole of it
with a cable, and what a preset adds without one ([latency.md](latency.md)).

## Routing: series, parallel, dual L/R

Not in the original (its audio loop `FUN_00031e18` runs the chain in series on one buffer). waveod places the
blocks in a grid of 2 rows × 6 cells, one per slot (`model/route.c`); the mode decides the path of the signal:
series (row 1, then row 2), parallel (PRE, then lane A = row 1 between a split and a merge beside lane B = the
whole row 2, then POST) or dual L/R (PRE, then lane A = row 1 from the split on to the left output and row 2 to
the right one). `preset.order` stays the blocks in path order, so everything
that works by chain position (edit screen, editor protocol, CTRL) is unchanged; the engine gets the number of
blocks of each lane (`chain_set_route`) and `chain_process` copies the signal after PRE, runs lane A, keeps its
output, runs lane B on the copy and mixes (merge: mix and pan of each lane) or sends A left and B right. With a
"no cab" output setting each Cab / IR of a lane runs in place and the side without cabinets keeps its signal.
Routing changes and moves only relink the order (`engine_suspend` / `engine_resume`); no module is reloaded.

Storage: the cell of each slot in the CTRL 1 `display` bytes (+0x3E0) and magic "WR", mode, split, merge, the
six joint parameters and a check in the CTRL 2 ones (+0x3F8). The original fills those bytes itself when it
loads a preset, so it plays a waveod preset in series, in `order`. They are written only when the routing is not
the plain series of `order`, so other presets keep the original's bytes. Without valid bytes (a factory preset,
or saved by the original) the chain is laid out in series; when the PC editor changes the order by position,
`route_fit` keeps each block in its lane and packs the lanes in the new order (series when one overflows).

## Icons and code placement

The design's line icons (24 × 24 SVG paths, round caps) are rasterized by `tools/mkicons.py` (no extra
packages: the coverage of each pixel by the stroke) into A8 alpha maps, `ui/images/icons.c`; LVGL draws an A8
image in the image's recolor colour, so one map serves every category colour (`theme_icon`). They and the
screens' code (`ui/sdram_code.h`, a `#pragma clang section`, since the firmware is linked with LTO and file names
are gone) live in SDRAM like LVGL; ITCM keeps the audio path and the rest of the application.

## CPU overload

As in the original (`FUN_00021D28` / `FUN_00022514`), a block that takes longer than 1.25 ms counts and the third
slow block in a row switches every slot off (`engine/overload.c`). The audio interrupt does it itself, because
an overloaded interrupt may starve the main loop; the main loop then sets the preset's on/off flags to match
(the preset counts as edited) and shows the warning (`ui/alert.c`). The modules stay loaded. Block times,
slow blocks and overloads are in the diagnostics panel of the settings.

## Preset assignments

The stored preset carries the performance controls (layout in `fw/preset_format.h`, from the original's preset
import `FUN_0002E110` and the editor's preset model): three quick knobs (a block parameter, the preset BPM or
volume), the EXP 1A / 1B / 2 targets (block, parameter in library order, min, max) and the EXP 1 group, five
CTRL groups (blocks each one switches) and the footswitch functions in two groups (stomp / preset mode).

- `model/assign.c` turns them into what the screens show; `ui/footswitch.c` runs the footswitches like
  `FUN_0002693C` (press = on release, long press, pairs, repeats); `model/expression.c` reads the pedals like
  `FUN_0001AB10` / `FUN_000200D0` (calibration and toe switch from the global settings, EXP 1 curve
  atan(tan(0.3) x) / 0.3); `rig_ctrl`, `rig_exp`, `rig_exp_group` change the blocks like `FUN_0001EDE8`,
  `FUN_000201D8`, `FUN_0001FFBC`.
- Removing a block forgets the assignments that point at it (`FUN_00021138`); replacing it brings parameters it
  lacks back to its first one (`FUN_00021230`).
- The EXP 2 jack can take one or two footswitches instead of a pedal (record 0 +0x24): their switches are
  GPIO1[16] / GPIO1[22] (keys 0x1A / 0x1B of the original, active low), with the functions at +0x25 / +0x27.
- The tempo the effects follow is the global one when it is on (record 0 +0x233 / +0x234), else the preset's;
  tap tempo writes the one in use (`FUN_000397EC`).

## Footswitch LEDs

Implemented after the original as described below (`board/qme200/leds.c` = led_set, `ui/leds.c` = led_config /
led_tick and the status task); checked in the simulator and the emulator (`leds` command), not on the pedal yet.
Approximations: the preset CTRL blink is 18 toggles at 150 ms (the original counts 18 status-task runs, whose
period is not known), and any warning on screen (`ui/alert.c`) switches the six key LEDs off like the overload
screen does. The bank screen is waveod's PRESETS list: BANK -/+ blink cyan (500 ms), A-D cyan, the active preset's
key white.

**Hardware.** Every LED is an output of the button / LED shift register chain (`board/qme200/shift_reg.c`).
Virtual pin n is bit (n & 7) of `shift_reg_out[n >> 3]` (`FUN_0001E38C`, buffer 0x20236538), the layout
waveod already uses. The LEDs are **active low**: 0 = lit. There is no PWM, so there is no brightness, and each
RGB LED shows 8 colours. The original only changes the buffer; the 10 ms key timer (`FUN_00035D4C`) sends it
with the key read, so a change shows within one poll.

| LED | Key (id) | R pin | G pin | B pin |
|---|---|---|---|---|
| 0 | FS A (0x10) | 0x09 (byte 1 bit 1) | 0x0A (1.2) | 0x0B (1.3) |
| 1 | FS B (0x11) | 0x0E (1.6) | 0x0D (1.5) | 0x0C (1.4) |
| 2 | FS C (0x12) | 0x03 (0.3) | 0x02 (0.2) | 0x01 (0.1) |
| 3 | FS D (0x13) | 0x04 (0.4) | 0x05 (0.5) | 0x06 (0.6) |
| 4 | BANK - (0x14) | 0x11 (2.1) | 0x12 (2.2) | 0x13 (2.3) |
| 5 | BANK + (0x15) | 0x16 (2.6) | 0x15 (2.5) | 0x14 (2.4) |
| 6 | EXP 1 A / B indicator | 0x1D (3.5) | 0x1C (3.4) | - |

- The pin table is at 0x47162, 3 bytes per LED with 0xFF = no pin. LED n belongs to the key with id 0x10 + n,
  which is how the test mode lights the key just pressed (`FUN_8000E068`).
- LED 6 has only red and green. It is LED2 on the button board (QME-200_AB_V01), next to the EXP key (J17), with
  one resistor per colour (R299 / R300); it is the only LED on that board, so EXIT / EFX / SAVE / DRUM / GLOBAL
  have none.
- The other bits of the output word are: 0x19 / 0x1A = the input mode switches (`audio_set_input_mode`),
  0x1B = analog enable (0 until the audio is up), and pins 0, 7, 8, 0x0F, 0x10, 0x17, 0x18, 0x1E and 0x1F
  stay 0. This explains the running state `7E 7E 7E 38`: every LED is off, and the analog stage is on.
- Byte 3 holds LED 6 and also the input mode bits, so its read-modify-write has to come from one context, or
  be atomic.

**Colour code.** bit 0 = red, bit 1 = green, bit 2 = blue, so 0 = off, 1 = red, 2 = green, 3 = yellow,
4 = blue, 5 = magenta, 6 = cyan and 7 = white. The test mode's on-screen colours confirm this order: code 4 is
shown as 0x1C7CF9, code 1 as 0xFF093B and code 2 as 0x73FF09.

**API of the original** (ITCM; the UI code calls it through the veneer 0x8000039E):

- `led_set(n, colour)` = `FUN_00022BB4`. It writes the three pins at once, and n >= 7 is ignored.
- `led_config(n, colour, count, rate)` = `FUN_00022B14`. It stores {colour, phase 0, count, rate, acc 0} at
  0x20242E38 + 8n and shows the colour at once.
- `led_tick(10)` = `FUN_00022B44`, called every 10 ms by the key timer. For each LED with count != 0, it adds
  `acc += 10 * rate` and, when acc reaches 30000, does `acc %= 30000` and toggles the LED (lit, then off,
  then lit again). count = 0xFFFF blinks for ever; any other count goes down by one each toggle and the LED
  stays in its last state when it reaches 0.
- So one toggle comes every 30000 / rate ms: rate 60 = 500 ms, 120 = 250 ms, 200 = 150 ms, 240 = 125 ms, and
  rate = BPM gives one blink per beat.

**Normal screens** (`FUN_0003C600`, the status task that runs on every screen). For each key 0..5 it takes the
press function of the active footswitch group (stomp / preset) and, when the function or its state changes,
sets the LED (`FUN_0003C484`, colours at 0x2023445E):

| Function | LED |
|---|---|
| none | off |
| CTRL 1-4 | with **one** member block: the colour of the block's category while the block is on, off while it is off. With 0 or 2+ members: blue after the preset loads, and each press flips it to red and back. This follows the presses, not the blocks (`FUN_000207E0`, `FUN_0001EDE8`, `FUN_00021EF8`) |
| Looper | green |
| Drum machine | red (it does not show whether the drums are playing) |
| Tuner | green |
| Tap | yellow, blinking for ever at rate = BPM in use (`FUN_0001F1D0`). Every change resets all the tap LEDs together, so they stay in phase |
| Bank + / - | white |
| Preset + / - | yellow |
| Preset A-D | white on the key whose preset (A-D) is the active one, off on the others |
| Bank menu | cyan |
| Rhythm + / - | red |
| EXP A / B | green while EXP 1 is on group A, red on B |
| 21 | white |
| Mode | blue |
| 22, slot 1-12 (24-35) | not handled: the LED keeps what it had |

- The category colours (table 0x20234479, by library menu) are: DYN cyan, FX green, WAH magenta, DRV red,
  AMP cyan, CAB yellow, IR yellow, EQ green, MOD blue, DLY blue, RVB cyan, CLN yellow, LOOP / SND / RTN white
  and VOL magenta.
- LED 6 always shows the EXP 1 group: green on A, red on B. It is updated on every screen except the test
  mode.
- Pressing the footswitch of the active preset again switches the preset CTRL. When that CTRL has blocks, the
  LED blinks white at 150 ms for 18 status-task runs and then stays white (`0x26B70` -> `FUN_0003C600`).
  The original picks that LED by the preset letter (A-D = LED 0-3), not by the key that was pressed.

**Screens that own the LEDs.** On these screens the table above is not applied:

| Screen | LEDs |
|---|---|
| tuner (9) | all off (`FUN_8000E8E8`) |
| looper (0xA) | all off when the screen opens. Then A and B show the looper: empty = off, or red blinking at the BPM when armed (auto record); stopped = green blinking at 500 ms; play = green; record = red; overdub = blue. At each loop wrap (play / overdub restart the loop and set the looper's byte +2) they go off for 250 ms. Erasing makes A and B blink red at 125 ms (9 toggles, ending off). D = cyan while AUTO REC is on (`FUN_80011520`, `FUN_80008D28`, `FUN_80008DD8`) |
| banks (0xE) | all six blink cyan at 500 ms (`FUN_80002694`) |
| overload (0xF) | all off (`FUN_8000B8B8`) |
| 0x10 | the table is skipped here too, but `FUN_0002C76C` does not register this screen, so its LED code was not found |
| test mode (0x12) | all off when the screen opens; each press of a key steps its LED off -> blue -> red -> green; releasing A+B lights the six LEDs white |

When the drum machine screen (7) is open, the normal LEDs stay on.

## Looper

`engine/looper.c` follows the original's looper (state at 0x20242D20; FUN_0002B2A4, FUN_0002B98C state machine,
FUN_0002C13C record, FUN_0002C014 play, FUN_0002C2EC overdub, FUN_0002B318 level trigger): 16-bit samples in a
0x792000-sample SDRAM buffer (section `.looper`, NOLOAD, not cleared), half of it for the loop and half for the
last overdub when undo is on; record gain = level x 32768 / 300, playback = level x 300 / 32768 / 1e4; PRE
records the mono guitar before the chain and plays into it, POST records the stereo output after the preset
level and plays before the global EQ. The state machine runs at the start of every audio block and takes the
main loop's commands then. When a new overdub starts, the previous one is merged into the loop a block at a
time by the interrupt (the part being played first). Overdubs saturate where the original's wrap around; tempo
sync, half speed and reverse (present in the original's engine, never switched on by its UI) are not done.

## Drum machine

`engine/drums.c` follows the original's (state 0x20234080; FUN_00040D94 sequencer, FUN_0004104C voices,
FUN_00040D08 volume): patterns of u16 events at 120 ticks per beat, 16 voices playing 16-bit samples 64 at a time
(a hit is heard two blocks later, as in the original), mixed into 32 bits within +-0x7FFFF8, volume moving by
1 per sample, added after the global EQ. The hits go through the original's 16-entry queue, overruns included.
`tools/check_drums.py` runs the original's voices in Unicorn: identical bit for bit.

The sounds and patterns are not in the repository: `tools/mkfw.py` takes the original's sample bank
(0x8001AF78) and pattern bank (0x800BC310) from the base firmware's SDRAM image (`tools/drumpack.py`) and writes
them into the region `.drum_pack` of waveod's SDRAM image (0xB0000 bytes, between the loaded data and the
`.bss`), which the bootloader loads with the rest. A base without them leaves an empty header: the drum screen
says so. The simulator builds the same pack from the firmware file (`host_flash.c`). The pattern bank is in
genre order; the original's name table groups the names differently, so `model/drum_names.c` lists them in the
bank's order.

MEUS RITMOS (`model/drum_user.c`): 20 patterns of waveod's own in its store (`fw/wv_store.h` type `WVS_DRUM`, key =
place; a record is u8 version, u8 flags, u16 BPM, name[16], u16 count, u16 events[count], at most
`DRUM_USER_EVENTS` = 2000, so it fits one record), written by waveod Studio (protocol 7: 0x1F000011 list,
0x1F000012 get, 0x1F000013 put, 0x1F000014 play). The same events as the bank's, ending with 0xFFFF (start over) or
`DRUM_END` 0xFFFE (stop: `drums_playing()` turns 0). The engine copies a chosen one into one of two SDRAM buffers
(`drums_set_events`; the interrupt keeps playing the other until it takes the new one at its next block), and
plays it instead of the bank's pattern until `drums_use_bank`. The choice is waveod's settings byte 10
(`drum_user`), so the shared globals keep a bank pattern for the stock firmware; `drum_user_select` /
`drum_user_deselect` are what the drum screen, the footswitch functions, MIDI CC 94 and the stock editor's pattern
setting call. Studio's `drums.py` turns a Standard MIDI File (format 0 / 1) into a record: channel 10 (or every
channel when there is none), General MIDI drum notes onto the bank's 13 sounds (an acoustic or electronic kick and
snare), ticks rounded onto 1/120 beat, the same sound at the same tick once (the louder), whole bars (the time
signature's) so a loop stays in time, the first tempo as the BPM; a file longer than 2000 events keeps the whole
bars that fit.

## USB

The RT1062's USB OTG1 (ChipIdea core, device mode) is brought up like the original app: PLL_USB1 at 480 MHz,
USBPHY1 out of reset, the 3.0 V regulator, then the controller (`board/qme200/usb_dcd.c`). Queue heads, transfer
descriptors and their bounce buffers live in the non-cacheable OCRAM region, so no cache maintenance is needed.
The USB interrupt runs above the audio one: the isochronous endpoints then only need 1 ms of transfers queued
although an audio block takes about that long to process.

`src/usb` is the same on the pedal and in the host build (where `host/host_usb.c` plays the computer for the
tests). It presents the original's identity byte for byte (`usb/usb_desc.c`: VID 0x84EF, PID 0x0282, both
configurations, the strings), chosen by the global USB mode (record 0 +0x230):

| Mode | Interfaces |
|---|---|
| 0, "6-In 4-Out" (factory) | UAC2 audio: 6 channels to the computer (IN 0x81), 4 from it (OUT 0x01), 16- or 24-bit; USB MIDI (bulk 0x03 / 0x83), the editor's port |
| 1, "2-In 2-Out" | UAC2 audio, stereo each way; no MIDI (so no editor, like the original) |

Audio (`usb/usb_audio.c`) is 44.1 kHz only. The recording endpoint is asynchronous: each microframe carries 5 or
6 frames from a phase accumulator kept around a ring fill target, so it follows the audio clock (the ES7243E is
the clock master); the host derives the playback rate from it (implicit feedback, no feedback endpoint, like the
original). The feature units take volumes 1..100 and mutes per channel as the original does (`FUN_0003880C`).
The audio engine routes the channels like the original's loop:

| Channel | Signal |
|---|---|
| recording 1 / 2 | dry guitar or processed (per side), x record level (+ AUX IN when set) |
| recording 3 / 4 | processed, before AUX IN and the computer's playback (+ AUX IN when set) |
| recording 5 / 6 | the final mix, before the output filter and MASTER |
| playback 1 / 2 | into the outputs after AUX IN, x monitor level |
| playback 3 | into the chain with the guitar (re-amping) |

USB MIDI (`usb/usb_midi.c`) turns the 4-byte event packets into a byte stream and back; the OUT endpoint is
re-armed only while the receive ring has room for a whole transfer, so a busy main loop slows the computer down
instead of losing data.

## MIDI

Two ports, like the original: the TRS jacks on LPUART1 at 31250 baud (pins GPIO_AD_B0_12 / 13, set up as the
original does in `FUN_00030A70`; `board/qme200/midi_hw.c`) and USB MIDI. `midi/midi.c` reads both every main
loop pass, hands the USB bytes to the editor (its SysEx), and parses the rest (running status, real-time bytes
anywhere):

- **Thru**: every channel and system message received on either port goes out of both, USB included, so the
  computer gets back what it sends (the original's handlers all start that way). SysEx is not passed on.
- **Filters** (global settings): the MIDI source (+0x78: TRS, USB or both) and each port's input channel (+0x70
  TRS, +0x72 USB, 0 = omni). The output channels (+0x74 / +0x76) are kept for the original, which only hands
  them to effect modules.
- **Program change** n: preset n with bank select (CC 0) 0, preset 120 + n with bank 1 (up to 239).
- **Control changes** (`ui/midi_control.c`, the original's event table at `0x45190`): preset volume, EXP 1 and
  its A / B, the quick knobs (absolute and step), bank / preset up and down, stomp / preset mode, blocks 1-12 on
  / off by chain position, tuner / looper / drum screens, looper controls and levels, tempo (CC 68 / 69, 14
  bits), tap tempo, CTRL 1-4, drum machine on / off, pattern and volume. The list is in the file's header. A CC
  with links in the preset's control table does what they say instead (waveod's; see footswitch links).
- **Out** (waveod's): MIDI out links of the control table send control and program changes on both outputs, on
  the link's own channel (`midi_send`, like the thru).
- **Tempo** (`model/tempo.c`, `FUN_000397EC`): the clock source (+0x79) decides who sets it: internal = taps,
  knobs and CCs; TRS / USB / external = the MIDI clock only; mixed = all. The MIDI clock's tempo is measured
  per port over BPM / 4 clocks (clocks x 2500 / ms).
- **Clock out** (+0x7A, TRS and / or USB): 0xF8 every 2 500 000 / BPM µs from GPT2 on the crystal, the fraction
  carried over. The UART and GPT2 interrupts share a priority between USB and audio; a clock byte goes out
  ahead of queued bytes on TRS, and on USB the clock interrupt pends the USB interrupt (`usb_dcd_wake`), which
  puts the clock packets in front of the next IN transfer.

Differences: after a pause in the incoming clock the original measures from its last measurement and runs at 40
BPM for a moment; waveod starts measuring again. A program change with bank select 2 or more is ignored (the
original loads the preset numbered like the input channel setting, from a stale register). The bank screen CCs
26-28 open the PRESETS list from 64 and go back to the main screen below it, like the tuner / looper / drum CCs. CC 31 is waveod's: the footswitch mode (0
preset, 1 stomp, 2 scenes, 3-127 the next one); CC 29 keeps the original's preset / stomp.

## The PC editor's protocol

`src/editor` serves the Matribox II PRO editor and the Matribox Studio over the MIDI port, like the original:

- **Link** (`editor/link.c`): SysEx packets `F0 21 25 4D 50 00 00 cs flag len off <nibbles> F7` reassembled
  with the original's sequence and checksum rules (NAK on a gap or a bad sum); replies in 0xB9-byte packets at
  least 3 ms apart (`FUN_00031BF4`). Payloads are LZO1X: received ones decompressed by `editor/lzo.c`, replies
  sent as literal runs (valid LZO1X the editor's decompressor takes).
- **Dispatcher** (`editor/server.c`, `FUN_0003A084`) and the groups: presets (`preset_msgs.c`: the edited
  preset, export / import, list, the 0x0100..0x010A sub-addresses), global settings, EQ and drums
  (`global_msgs.c`), system (`system_msgs.c`: save as, activate, move / delete / copy, the jump to the
  bootloader for a firmware update), user IRs and Clone captures (`media_msgs.c`).
- **Replies** are built from the stored record format (the original's edited preset is a stored record in RAM),
  so they come out byte for byte the same. Notifications go through the original's queue rule: one repeating the
  previous (command, first word) waits 150 ms and only the newest goes out (`FUN_0003568C`).
- **The pedal's own changes**: `model/rig.c` tells a listener what changed (knob, switch, CTRL, chain, preset,
  modified flag, volume, expression pedal); `preset_msgs.c` turns each into the original's notification, the
  same the original's handlers send when the editor makes the change.
- **Global settings** are changed on record 0's bytes the way the original's setters do (clamps included), then
  decoded and applied, so fields waveod does not use (language, MIDI output channels) survive; the record is
  written 2 s after the last change (`globals_touch`, also used by MIDI).

`tools/check_editor.py` sends the same requests to the original app (its own dispatcher, in the workspace's
`uiemu`) and to waveod (in `tools/emu.py`, through the USB model and the MIDI port) and compares the replies.