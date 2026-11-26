# Verification

| Check | How | Result (stage 4) |
|---|---|---|
| Flash formats, value text, record store | `build/host/waveod-tests QME-200_FW_USB_V1.2.2.bin` | 580 checks pass |
| Loader on every stock module | same test + `tools/check_loader.py` | 179 modules, identical to the reference |
| Factory presets resolve | same test | 913 slots; 1 falls back (factory preset 07B has a Cab with id 0; the stock loader does the same) |
| UI, all screens | `waveod-sim` screenshots | match the design, adapted to 480 x 272 |
| ARM firmware boots, loads and initialises the stock modules | `tools/emu.py` | boots, preset 01A (11 modules) on screen |
| Audio through the stock modules | `tools/emu.py ... audio:N` | signal passes; ~295k instructions per block for 01A |
| Footswitches, encoders | `tools/emu.py` with press / turn | preset and stomp switching work |
| Touch gestures on every screen | `waveod-sim` with tap / long / drag | select, edit, drag values, swipe presets, browse and load |
| Chain editing by drag and drop | `waveod-sim` with down / move / up (shots mid-drag), `tools/emu.py` with drag / long | insert, replace, move, delete on the bin, full lanes, palette glide; parallel layout with a forced topology |
| Navigation of the "Fluxo de Uso" design | `waveod-sim` and `tools/emu.py` with tap / long / hold:EXIT | tabs, tap = edit, hold = on/off, models load at once, ‹ back, hold EXIT home; parallel layout checked with a forced topology |
| GT911 driver | `tools/emu.py` with tap / drag (I2C slave model on the GPIO3 pins) | controller found at 0x14, taps and drags reach the UI |
| Record store writer = the original's | `tools/check_store.py STOCK.bin` | 600 writes from blank and 150 after simulated power cuts give byte-identical 512 KB images; the original reads back every record |
| Writer, power cuts, write guard, preset save | unit tests | 628 checks; all 120 factory presets encode back byte for byte |
| Tuner pitch accuracy | unit tests, synthetic strings (8 harmonics, noise) 55 Hz-1 kHz, ±30 cents | worst error 0.08 cent |
| Tuner and settings on the ARM firmware | `tools/emu.py ... hold:A play:300:110 ... press:GLOBAL ... reboot` | A2 at 110.0 Hz, 0 cents, live; settings come back after a reboot |
| Save on the ARM firmware | `tools/emu.py ... press:SAVE ... flash:store.bin reboot` (FlexSPI model) | 2 erases, 1472 bytes programmed, nothing below 0x780000; the preset comes back after the reboot; the original's code reads the record |
| CPU overload guard | unit tests (the original's rule) + `tools/emu.py ... cpu:1300 play:30 shot:... tap:...` | third slow block in a row switches every block off, the preset shows EDITADO, the warning closes on a tap or a press without reaching the screen below |
| Global EQ, fixed input / output filters = the original's | `tools/check_dsp.py STOCK.bin` (Unicorn) | filters and EQ processing identical bit for bit (EQ fed the original's coefficients); own coefficients within 3e-7 |
| Global settings record | unit tests + `tools/emu.py ... press:GLOBAL ... flash reboot` | factory record decodes, unchanged settings encode back byte for byte, only changed fields written; settings come back after a reboot |
| Preset assignments | unit tests (factory 01A: knobs, EXP 1A volume / 1B wah, CTRL, footswitches, combos; unassign on removal; template) + all factory presets encode back byte for byte | pass |
| Footswitches, CTRL, EXP, tap tempo | `waveod-sim` and `tools/emu.py` with press:A+B etc. and pot:EXP1 | modes, CTRL 1-4, preset CTRL, tuner pair, bank, toe switch A / B, 107 BPM for 560 ms; an edited CTRL survives save + reboot |
| Spillover | unit tests (`tests/test_spill.c`: pools from both ends, the slots a tail keeps per routing, every factory pair fits) + `tools/check_spill.py FW.bin` (01A → 01B in the emulator against no change and against the spillover off; `--keys B,C`, `--cpu 1100`) | the tail follows the old preset ringing out on its own (bit for bit, then within -116 dB); off = silence as the original; quick changes fade, slow blocks cut the tail without an overload ([spillover.md](spillover.md)) |
| Effect blocks of waveod's own | unit tests (`tests/test_blocks.c`: descriptors, Stereo Util's matrix, the catalog with built-ins and missing ids, a library image, CRC-32) + `tools/check_blocks.py FW.bin --wvlib LIB.bin` (presets over USB in the emulator; the library installed and removed over USB) + `tools/check_spill.py` WAVs against the previous build (the Cab / IR moved onto the interface) | Stereo Util as computed (-134 dB, two instances bit for bit / -158 dB); the module = the built-in bit for bit; installed while playing, the preset takes it without a reload; removed or unknown: AUSENTE, kept, signal passes; Cab / IR sample for sample as before ([blocks.md](blocks.md)) |
| Modulators | unit tests (`tests/test_mods.c`: shapes and timing, sync, sequencer, envelope, the chunk, routes from the table, nothing written while the chain does not play) + `tools/check_mods.py FW.bin` (Stereo Util's Gain on MOD 1, per block in the emulator) | LFO square 5 Hz and 1/4 synced, SEQ 4 steps: every block away from a change at its level; ENV within 0.01 dB ([modulators.md](modulators.md)) |
| Circuit blocks | unit tests (`tests/test_drives.c`: descriptors, a loud tone, poll's rebuild and flip, bypass) + `tools/check_drives.py` (each block on the PC against its whole circuit in `tools/circuits/refsim.c` at 8x: a guitar DI and sines, three settings; the ARM module in `tools/wvfx_emu.py` against the PC build) + `tools/check_drives_fw.py FW.bin` (the six installed over USB in the emulated firmware) | the DI -35..-74 dB from the circuit (Dark Muff Lite -23..-33, by design), harmonics within 0.3 dB; the modules bit for bit the PC builds; in the firmware they load, play, and a knob on MOD 1 rebuilds through poll ([drives.md](drives.md)) |
| Chorus | unit tests (`tests/test_chorus.c`: descriptor, the 23 types, the sweep's range and period free and synced, stereo / mono, bypass bit for bit, knobs moved every block) + `tools/check_modfx.py` (the ARM module against the PC build at 9 settings; `--fw FW.bin`: installed over USB in the emulated firmware, alone and after 01A) + a reference implementation outside this repository | the module bit for bit the PC build; 12.6 k (2 voices) .. 30.9 k (8 voices) instructions per block, +12.8 k in the firmware; the wet -51..-79 dB from the reference ([chorus.md](chorus.md)) |
| Flanger, Phaser, Rotary, Vibe, Tremolo | unit tests (`tests/test_modfx.c`: descriptors, every type finite and moving a tone, bypass bit for bit and back, random knobs, the Panner's L + R) + `tools/check_modfx.py` (ARM against PC; `--fw FW.bin`: installed over USB, alone and after 01A) + the reference implementations outside this repository | the modules bit for bit the PC builds; 11.9-15.8 k / 5.1-9.5 k / 15.6 k / 7.7-9.1 k / 3.6-4.3 k instructions per block; the wet -44..-72 / -75..-100 / -52..-70 / -87..-95 / -103..-105 dB from the references ([modfx.md](modfx.md)) |
| Delay | unit tests (`tests/test_delay.c`: descriptor, every type's echo at the Time, ±100 % feedback bounded, bypass bit for bit and the ring-out, a Time jump without a step, random knobs) + `tools/check_modfx.py` (ARM against PC at 15 settings; `--fw FW.bin`: installed over USB, alone and after 01A) + the reference implementation outside this repository | the module bit for bit the PC build; 16-51 k instructions per block, +16.5 k in the firmware; the wet -55..-82 dB from the reference on most types (see [delay.md](delay.md) for the exceptions and why) |
| Reverb | unit tests (`tests/test_reverb.c`: descriptor, the 50 types at 4 qualities decaying with the Time, bypass bit for bit, the tail with and without Spillover, random knobs) + `tools/check_modfx.py` (ARM against PC at 8 settings; `--fw FW.bin`: installed over USB, alone and after 01A) + the reference implementation outside this repository | the module bit for bit the PC build; 48.5 / 65.4 / 87.2 / 103.2 k instructions per block by quality; the wet -79..-103 dB from the reference ([reverb.md](reverb.md)) |
| Plate / Spring | unit tests (`tests/test_plate.c`: descriptors, every type (the Plate at 4 qualities) decaying with the Time, bypass bit for bit, the tail with and without Spillover, random knobs) + `tools/check_modfx.py plate spring` (ARM against PC at 7 / 6 settings; `--fw FW.bin`: installed over USB, alone and after 01A) + the reference implementation outside this repository | the modules bit for bit the PC builds; Plate 94-128 k, Spring 42-101 k instructions per block; the wet -96..-102 dB (Plate) and -91..-101 dB (Spring) from the reference ([plate.md](plate.md)) |
| The original's effects (every stock amp: B-Man, Brit, Calif, Dark, Doctor, Eng, Flyman, Supero, Superb, Voks, Bog, Dizzy, Dragon, Sol, the single amps and the bass and acoustic preamps; all 24 drives; all 23 modulation effects; all 17 delays; all 12 reverbs; all 14 dynamics; all 8 filter and pitch effects; all 6 wahs; all 5 EQs) | `tools/check_stock.py STOCK.bin` (each block's 7-10 scenarios, knobs moved and a bypass stretch among them, through the original's module in Unicorn, the block as a module in `tools/wvfx_emu.py` and the PC build; `--fw FW.bin`: installed over USB in the emulated firmware, 3 presets each played with the original module and with the port) | the 2 B-Man, 9 Brit, 11 Calif, 2 Dark, 2 Doctor, 2 Eng, 4 Flyman, 5 single-amp, 4 bass / acoustic preamp, 2 Supero, 2 Superb, 4 Voks, 4 Bog, 4 Dizzy, 3 Dragon, 3 Sol, 24 drive, 23 modulation, 17 delay, 12 reverb, 14 dynamics, 8 filter / pitch, 6 wah and 5 EQ blocks bit for bit the original's modules, module and PC; in the firmware (Brit) the same DAC words as the original in every preset ([stock_port.md](stock_port.md)) |
| FX Loop / SND / RTN | unit tests (`tests/test_blocks.c`) + the emulated firmware (a preset with the original's FX Loop slot: describe, FX SEND words) | the original's levels and Mix curve, Return Phase, bypass, the ramp; in the firmware the block runs the slot (describe adds Return Phase), Send 50 = half of Send 100 within -183 dB, Mix 100 with no return silent, off sends nothing |
| Gate SC / Comp SC | unit tests (`tests/test_blocks.c`) | gate opens / closes on its key, range; compressor 10.5 dB at 4:1 for 14 dB over, knee, mix, key, bypass |
| Looper | unit tests (record / play at unity, overdub, undo / redo, an undone overdub dropped by the next, stop, erase, POST, auto record) + `waveod-sim` / `tools/emu.py` with press:C+D, press:A | pass; 2-5k instructions per block more |
| Drum machine = the original's | `tools/check_drums.py STOCK.bin` (Unicorn) + unit tests (the firmware's pack, `1/4` at 120 BPM: a click every 22050 samples) | voices identical bit for bit over 9000 blocks; about 10k instructions per block |
| USB device | `tools/check_usb.py FW.bin` (the real driver on a ChipIdea model in `tools/emu.py`: queue heads, descriptors, priming, completion interrupts) | enumeration, the original's descriptors, address, configuration, UAC2 clock / feature unit requests, both isochronous streams at 24 bits (5-6 frames per microframe, 44.1 kHz kept), playback reaching the outputs and recording channel 5, MIDI endpoint armed |
| Editor protocol = the original's | `tools/check_editor.py FW.bin STOCK.bin` (the original's dispatcher in matribox-studio's `uiemu` against waveod through USB MIDI) | every read (edited / exported presets, list, sub-addresses, settings, 34 global parameters, EQ, drums, IR / Clone names) identical byte for byte except the bytes the original leaves unset; writes in sequence (see the script) |
| IR / Clone file systems = the original's | `tools/check_irfs.py STOCK.bin` (Unicorn) | 157 random writes / renames / deletions across a remount, then user IR renames, give byte-identical images |
| LZO1X, file systems on the host | unit tests | literal streams of every length class, a match, damaged input refused; write / rewrite / rename / delete read back |
| MIDI parsing, filters, clock | unit tests (`tests/test_midi.c`, fake ports) | running status, real-time bytes inside messages, bank select, CC 68 / 69, SysEx to the editor only, input channel and source filters, tempo from the clock (100 / 150 BPM, after a pause, per clock source), clock out at 120 / 300 BPM with the fraction carried |
| MIDI on the ARM firmware | `tools/check_midi.py FW.bin` (LPUART1 and GPT2 models in `tools/emu.py`, USB as above; effects read from the editor notifications) | LPUART1 set like the original (BAUD 0x1F000050); program changes from TRS and USB, bank 1, volume and block CCs, input channel and source set through the editor protocol, thru on both outputs, clocks on both outputs, tempo 100 from a TRS clock, CC 68 / 69 refused with an external clock source and taken with the internal one |
| Crash protection | `waveod-sim --crash ...` / `--hold EXIT` (screens), `tools/emu.py` with GLOBAL › TESTAR PROTEÇÃO (faults delivered to the vector table with an exception frame, SysTick running the firmware's handler, an RTWDOG model at 32.768 kHz, AIRCR / RTWDOG resets restart the firmware keeping SNVS_LPGPR and SRC_SRSR) | undefined instruction: UsageFault, record, reset, `PEDAL REINICIADO`; the second one in a row starts in MODO SEGURO; a stopped main loop resets after 1.5 s (software watchdog); stopped with interrupts off, the RTWDOG resets after 3 s and the start-up reports it; the RTWDOG clock is measured (~32.8 kHz) before it is armed |
| Core clock | `tools/emu.py` (registers read 0, so the whole sequence runs) | the original's writes in its order, every wait bounded; the start-up message shows the measured clock |
| Routing (series / parallel / dual L/R) | unit tests (`tests/test_route.c`), `waveod-sim` (chain screen), `tools/emu.py` with `audio:N` (L and R peaks) | path order and lane counts per mode (lane B = the whole second row), joint ranges, record round trip in the CTRL display bytes and a series preset's bytes untouched, editor-style position changes keep the lanes; on the ARM firmware with factory 01A the stock modules run in all three modes: parallel L = R (0.055 FS), dual = amp lane left (0.054) and row 2 right (0.013), back to series settles at the series level |
| Footswitch LEDs | `waveod-sim ... leds`, `tools/emu.py ... leds` (the shift register's latched outputs decoded with the pin table) | factory 01A: preset mode A / BANK white, EXP green; stomp mode: CTRL with one block in the category colour while on, off while off, TAP blinking yellow; tuner and warnings: key LEDs off; looper: red recording, green playing after 250 ms off |

## What the emulator does not cover

- Real timing: instructions are not cycles, caches are not modelled. On the pedal the block time is measured
  by the engine (block times in the diagnostics panel of the settings).
- The clock tree: the emulator measures 595 MHz whatever is written. On the pedal the diagnostics show the clock
  before (the bootloader's) and after `clock.c`.
- The SNVS registers' retention, SRC_SRSR and the RTWDOG's real clock: modelled after the reference manual.
- Peripheral behaviour beyond plain registers (codecs, DMA timing, LCD timing). The board code replays what
  the proof-of-concept application already ran successfully on the pedal.

## First run on the pedal

1. Back up presets with the stock editor.
2. Build: `python tools/wvbuild.py fw && python tools/mkfw.py BASE.bin out/QME-200_waveod.bin` (BASE = the firmware
   currently installed, so the effect library stays the same).
3. Install with the stock editor.
4. Expect the home screen with the start-up message (CPU clock, "toque OK" when the touch controller
   answered). Check sound, footswitches, encoders (direction), MASTER.
5. Touch: press GLOBAL and touch the four corners; the marker must follow the finger (otherwise set
   `SWAP_XY` / `MIRROR_X` / `MIRROR_Y` in `touch_gt911.c`).
6. Going back: recovery footswitch combination at power-up, then the stock firmware through the editor.

## First save on the pedal (stage 5)

Done on 2026-10-13: saving over a preset and into an empty location next to a user preset (the copy path)
works. Two problems found on the way, both now caught by `tools/emu.py`: the TX FIFO ran dry during a
256-byte page program (sequence timeout, STS1 code 0xE), and memory-mapped reads while an IP command was
being prepared corrupted programmed data (AHB prefetch).


1. Back up the presets with the stock editor (the record store is the only area written).
2. Edit a preset, SAVE, pick a free location (e.g. 30D), save. Expect "Preset salvo em 30D" after a short
   mute.
3. Load another preset and come back; power-cycle: the pedal starts on 30D with the edit.
4. Install the stock firmware: 30D must show the same preset there (the format is shared).
5. If a save reports "Falha ao gravar na flash", nothing was committed; the previous copy stays.

## USB on the pedal (phase 5)

Enumeration confirmed on 2026-10-18 (167ba28; the first attempt failed with "Device Descriptor Request Failed"
until a DSB came before each prime). The other steps, in order, each reversible:

1. Install the build and plug the USB cable. GLOBAL › DIAGNÓSTICO shows `USB: configurado · 6/4 · MIDI ok`; Windows
   lists `Matribox II PRO` (audio) and its MIDI port, as with the original.
2. Matribox Studio, "Pedal e presets": the preset list, the edited preset's chain, a switch and a knob (the
   preset must show EDITADO afterwards; reload it on the pedal to drop the edit).
3. The PC editor: it must connect (list, current preset, IRs, Clone, settings, drums, EQ) and follow knob
   turns on the pedal.
4. USB audio in a DAW at 44.1 kHz: record channels 1/2 while playing; play a track back (channels 1/2 in the
   outputs). The diagnostics line counts packets; `curtos` / `falhas` must stay near zero.
5. Firmware update from the Studio or the editor without the footswitch combination (`0x0500000E`): the pedal
   restarts in the bootloader; install the same build again.
6. Writing an IR or a Clone capture only with an explicit OK for that slot (it writes the original's file
   systems, which the original firmware reads too).

If the pedal does not start, the bootloader's footswitch combination still enters the update mode.

## MIDI on the pedal (phase 6)

Not run on the pedal yet. Each step reversible (the settings go back from GLOBAL):

1. GLOBAL › DIAGNÓSTICO: `MIDI recebido: TRS n · USB n` counts messages as they arrive.
2. USB: from a DAW or MIDI-OX on the `Matribox II PRO MIDI` port, program change 5 loads preset 02B; CC 7 sets
   the preset volume. A program change reloads the preset like the original (unsaved edits are dropped).
3. TRS: the same from a MIDI controller on the MIDI IN jack (with the TRS adapter type the pedal uses); MIDI OUT
   repeats what comes in.
4. GLOBAL › MIDI: ENVIAR CLOCK = TRS E USB: a DAW following external clock shows the pedal's tempo.
5. GLOBAL › MIDI: FONTE DO TEMPO = CLOCK DO USB, the DAW sending clock: the BPM on the home screen follows.
   Back to INTERNO E CLOCK (factory) afterwards.

## Protection, clock and LEDs on the pedal

Not run on the pedal yet. Each step is reversible; unsaved preset edits are lost at each restart.

1. Start-up: the message shows `CPU 600 MHz`. GLOBAL › DIAGNÓSTICO: `CPU 600 MHz (bootloader N)` (N = what the
   bootloader left: 600 means clock.c wrote nothing), `watchdog NNNNN Hz` (about 32768; `desligado` means the
   RTWDOG clock was not as expected and only the software watchdog runs), `nenhuma falha antes deste início`.
2. Sound, MIDI (31250 baud) and USB as before; the DSP load of a heavy preset (chain screen) is lower than with
   the bootloader's clock if N was below 600.
3. LEDs, preset mode with factory presets: A white on 01A, BANK - / + white, EXP green; B on 01B. Stomp mode (STOMP
   tab): each CTRL with its block's colour while on; TAP blinking at the tempo. A+B (tuner): key LEDs off. C+D
   (looper): A / B red while recording, green while playing. The EXP LED's place on the panel is not known yet.
4. GLOBAL › SISTEMA › PROTEÇÃO = FALHA, tap it: the sound stops, the pedal restarts within a few seconds and shows
   PEDAL REINICIADO (UsageFault in the diagnostics). Once more right away: MODO SEGURO (the preset is not loaded).
   Load a preset to go on.
5. TRAVAR O PROGRAMA: the screen freezes, about 1.5 s later the pedal restarts (`o programa parou de responder`).
6. TRAVAR SEM INTERRUPÇÕES: about 3 s later the RTWDOG restarts it (`o watchdog do chip reiniciou o pedal`).
   If nothing happens within 10 s, the RTWDOG did not run: power-cycle the pedal and report the diagnostics line.
7. Hold EXIT while powering on: MODO SEGURO on request.
8. Firmware update from the Studio / editor still works (the watchdogs are switched off before the bootloader).

## Spillover on the pedal

Not run on the pedal yet. Nothing is written but the settings record.

1. Preset mode, 01A (its Room reverb is on): play a chord, press B while it rings. The reverb goes on ringing over
   01B; 01B sounds as soon as it is loaded. GLOBAL › DIAGNÓSTICO: Rastros: 1, cortados por CPU 0.
2. A preset with a long delay (feedback high): change while the repeats run; they keep coming for up to 10 s
   and fade out over the last second.
3. Quick changes (A, B, C in a row): each tail fades out at the next change without a click; no sobrecargas.
4. Heavy presets (NAM Clone, two amps): change between them while playing. Either the tail rings, or it is cut
   (cortados por CPU goes up) and the new preset plays on; never sobrecargas.
5. RASTRO NA TROCA = DESLIGADO: a change cuts the sound as before.
6. Tuner, saving, the PC editor: no tail survives them and nothing faults.

## Effect blocks on the pedal

Not run on the pedal yet. Step 4 writes the flash at 0x2A0000-0x3A3000 for the first time on the pedal (the area
the original firmware's image used; nothing reads it there): take a full flash dump from the Studio first.

1. The Cab / IR blocks sound as before (01A; GLOBAL › PRESETS › MOTOR CAB / IR switching between WAVEOD and ORIGINAL
   gives the same sound) and the MIC screen still works.
2. A block's models: VOL › Utility › Stereo Util. Gain, Polarity Invert L with headphones, Channels Swap after a
   stereo effect, Width 0: as named, no clicks when turning the knobs or switching the block.
3. Save a preset with Stereo Util, power off and on: it comes back with its knobs.
4. Studio › Efeitos: install `out/fx/stereo_util_m.elf` (`tools/wvfx.py build src/blocks/stereo_util.c -D
   STEREO_UTIL_COPY=1 -o out/fx/stereo_util_m.elf`: Stereo Util M, a copy under its own id), or any module. It shows in the list and on the pedal after the built-ins. Use it in a preset and save.
5. Remove it in the Studio: the preset shows AUSENTE there and plays through; install it again: back with its
   knobs.
6. A firmware update from the Studio keeps the library (Efeitos lists it afterwards).

## Modulators on the pedal

Not run on the pedal yet.

1. A preset with a delay or a modulation block: CONTROLES › a card › TECLA turned to MOD 1. TIPO = LFO, FORMA
   SENOIDE, VELOCIDADE 1 Hz: AGORA moves 0..100 %.
2. + NOVA LIGAÇÃO › a block › a parameter (a delay's mix, a volume): it moves once a second between its MOD EM 0%
   and MOD EM 100% values; the edit screen shows the value moving and MOD 1 in its corner.
3. SINCRONIA 1/4 with TAP: the movement follows the tempo. SEQUÊNCIA with 4 steps: steps audible, DESLIZE smooths.
4. ENVELOPE on a volume or a wah: it follows the picking; SENSIBILIDADE moves where it opens.
5. Save, load another preset and come back: the modulators and links are there and start again from the
   beginning; the parameter's stored value is unchanged.
6. Gate SC after a high-gain amp, KEY Input: silent between notes, no chatter on the tails; Comp SC on a clean
   sound: even level, MIX for parallel compression.
7. waveod Studio › Presets › Controles…: MOD 1-4 show the settings set on the pedal; change one and record it.

## Circuit blocks on the pedal

Not run on the pedal yet. Installing them writes waveod's library in the flash (see "Effect blocks on the pedal").

1. Build the six (`tools/check_drives_fw.py` leaves them in `out/fx/*.elf`) and install them from Studio ›
   Efeitos; DRV › Circuit lists Dark Muff, Dark Muff Lite, Green Drive, Rodent Dist, Round Fuzz, Treble Boost.
2. Dark Muff against research/pulse's Dark Muff (heard before): the same sound at the same knobs.
3. Each one in front of a clean amp: turning a knob moves the sound without clicks or dropouts; switching the block
   on and off is a short crossfade.
4. The cost in cycles: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", for 01A alone and with each block added (note
   both). Against the emulator's instructions (docs/drives.md) that gives the M7's real cycles per instruction on
   this code, which decides whether hand-tuned code is worth it. Round Fuzz and Rodent Dist with Quality 4x in a
   full chain: if the overload guard trips, 2x is the answer.
5. Round Fuzz with Fuzz at 100 and a hot input: the gated, sputtering fuzz, no runaway or silence after loud notes.
6. A modulator on a circuit knob (Green Drive's Drive on MOD 1, LFO 1 Hz): it moves, a little stepwise (the main
   loop rebuilds the circuit).

## Modulation blocks on the pedal

Not run on the pedal yet. Installing them writes waveod's library in the flash (see "Effect blocks on the pedal").

1. Build them (`tools/check_modfx.py` leaves `out/fx/{chorus,flanger,phaser,rotary,vibe,tremolo}.elf`) and install them
   from Studio › Efeitos; MOD lists Chorus, Flanger, Phaser, Rotary, Vibe and Tremolo.
2. The Chorus steps below.
3. Flanger at its defaults (Digital Stereo, 0.5 Hz, Feedback 60 %): a smooth jet sweep; Feedback near ±99 % rings
   but stays bounded; Thru-Zero and Zero Cancel go through the cancellation point; turning Delay / Width does not
   click.
4. Phaser at its defaults (4 stages, triangle): Stages 2..12 and Feedback ±90 % while playing, no clicks; the
   photocell types (Lamp, Twin Lamp) lag on the way down.
5. Rotary at its defaults (fast): the horn's Doppler and the drum's slower throb; Rate from 6.5 to 0.8 Hz: the
   rotors slow down over ~1-2 s, the horn first; Drive 20+ gives the overdriven cabinet.
6. Vibe at its defaults (Lamp, 1 Hz, Mix 53 %): the throb lags and swings wider as Rate goes up; Mix 100 % gives the
   vibrato; Feedback adds the resonance without whistling at ±90 %.
7. Tremolo: the three waves at 5 Hz, Depth 100 % without clicks on the square; Panner moves the sound between the
   sides at a steady loudness; Sync follows the tap tempo.
8. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", 01A alone and with each block added.

### Chorus

1. Build it (`tools/check_modfx.py` leaves it in `out/fx/chorus.elf`) and install it from Studio › Efeitos; MOD ›
   Chorus lists Chorus.
2. In front of a clean amp, at its defaults (Digital Stereo, 1 Hz, Mix 50 %): a smooth chorus, no zipper, no clicks
   while turning Rate, Depth, Delay and High Cut; Type switches without a burst.
3. The types by ear: Compact / Vintage (short, centred, darker), the Analog ones (the sweep bends like a
   bucket-brigade), Octet (dense), Space 1-3 (wide, little motion), Vibrato at Mix 100 % (pitch only).
4. Sync on: Rate shows note divisions and follows the tap tempo.
5. On and off while playing: a short fade, no stale audio when it comes back.
6. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", 01A alone and with Chorus added (Octet too).

## Delay on the pedal

Not run on the pedal yet. Installing it writes waveod's library in the flash (see "Effect blocks on the pedal").

1. Build it (`tools/check_modfx.py delay` leaves `out/fx/delay.elf`) and install it from Studio › Efeitos; DLY lists
   Delay. The buffer pool must hold its 1.6 MB (twice during a spillover).
2. Digital Stereo at its defaults (400 ms, 30 %, Mix 30 %): clean repeats; Time turned while playing glides
   without a click (the heads crossfade); Sync follows the tap tempo.
3. Feedback at ±100 %: it builds up but stays bounded (the clip at ±8 inside the loop).
4. The types by ear: Analog / Bucket (darker, compressed), Tape Wide (wow), Ping-Pong and Dual (sides), Sweep
   (the swept band), Ducking (the echoes rise when you stop), Ambient / Breeze (diffused), Trem Echo, Panning.
5. Switch it off while it echoes: the echoes ring out; once silent, nothing changes the input.
6. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", 01A alone and with Delay added (Breeze too).

## Reverb on the pedal

Not run on the pedal yet. Installing it writes waveod's library in the flash (see "Effect blocks on the pedal").

1. Build it (`tools/check_modfx.py reverb` leaves `out/fx/reverb.elf`) and install it from Studio › Efeitos; RVB lists
   Reverb. Its lines take 1.6 MB of the buffer pool (twice during a spillover; with the Delay, 3.2 MB).
2. Hall Large at its defaults (3.2 s, Mix 30 %): a smooth tail without flutter or metallic ringing; Time, High Cut
   and Low Cut turned while playing: no clicks.
3. The types by ear: rooms (short, early reflections), halls, the huge ones, the clouds (Time 20 s and up), the echo
   spaces; Quality from Economy to Ultra: smoother, denser tails.
4. Switch it off while it rings: with Spillover the tail rings out; without, it stops (after a short fade) and
   does not come back stale when switched on.
5. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", 01A alone and with Reverb added, at Economy and Ultra.

## Plate and Spring on the pedal

Not run on the pedal yet. Installing them writes waveod's library in the flash (see "Effect blocks on the pedal").

1. Build them (`tools/check_modfx.py plate spring` leaves `out/fx/plate.elf`, `out/fx/spring.elf`) and install them
   from Studio › Efeitos; RVB lists Plate and Spring. Their lines take ~0.42 MB and ~0.21 MB of the buffer pool.
2. Plate, each type at its defaults, Mix 30 %: a dense, bright tail that shimmers, no flutter; Quality from Economy
   to Ultra: a smoother chorusing of the tail. Time, High Cut and Low Cut turned while playing: no clicks.
3. Spring, each type: the drip and "boing" of a spring tank on picked notes; the pair tanks (Rack Spring, Classic
   Tank) sparser, the three-spring ones (Long Spring, Wide Spring) denser; Valve Tank and the Box Tanks brighter
   (their tone circuit).
4. Switch each off while it rings: with Spillover the tail rings out; without, it stops (after a short fade).
5. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", 01A alone and with each added (Mid Plate, Rack Spring are
   the heaviest).

## NAM captures on the pedal

Not run on the pedal yet. Writing a capture replaces a Clone slot, and all 10 hold the user's own captures: pick a
slot with the user first (and keep a copy: the PC editor exports Clone captures).

1. Install the firmware; the Clone slots that were not written play as before, on the original's module.
2. Studio › NAM: the slots as the pedal has them; select one, "Gravar captura no slot…" (a .nam with the A2-Lite
   network, or a full A2 holding one; or `python -m waveod_studio nam amp.nam --slot N`); the chain reloads. On
   the pedal: CLONE › NAM lists it under its name, "48k" with the resampler's icon (or "44.1k"). A preset with that Clone slot: the amp's sound at the level of the others, Gain / Volume / Bass /
   Middle / Treble working, no clicks while turning them.
3. The same capture with `--44k` (no resampling): slightly brighter, cheaper.
4. The cost: GLOBAL › DIAGNÓSTICO, "DSP: bloco ... us", with and without the Clone block; with a Cab / User IR
   added (on the original firmware the research NAM Clone module, the slower engine, made a 913 us block with an
   IR on, of the 1250 us the overload guard allows). The same preset on a build from before the faster engine
   (docs/nam.md "Cost"): the emulator counts 19 % fewer instructions for the block at 48 kHz and 11 % fewer at
   44.1 kHz, and a cache model about 40 % and 55 % fewer fetched lines; what that is in microseconds is what
   this step finds out. With the Clone block alone the short histories are in the ITCM; a preset with more than
   about 26 KB of stock modules' code in the slots before it keeps them in the OCRAM (the second case to time).
5. A preset change from a NAM preset to another NAM preset with Spillover on: the tail is cut (they do not fit
   side by side), no overload, no stale sound.
## Spectrum analyser on the pedal

Steps 1-4 passed on the pedal (9883847). Nothing is written to the flash.

1. A block's edit screen, FFT: the strip goes, the analyser shows; playing, the "after" area moves with the guitar,
   the grey "before" line under it. Turn a drive's gain: harmonics grow; an EQ's band: the curve bends there.
2. Tap the legend: ENTRADA / SAÍDA of the preset. Tap the plot: the cursor and its readout; hold: PAUSA.
3. The UI stays responsive with it on (knobs, touch); GLOBAL › DIAGNÓSTICO "DSP" does not move when FFT is
   switched on (the analysis runs in the main loop, not in the audio interrupt).
4. FFT off: the usual screen; leaving the screen and coming back keeps the choice.

## Writing the flash without a cut in the sound

Verified in the emulator: BANK- + BANK+ with a tone playing, the settings stored 2985 ms later, no block below half
the level (before: down to 3.6 % at 2988 ms, then the fade in; on the pedal the erase lasts longer); the original
Cab module (cab engine = original) bit-exact with its data read from the copy, also with the IR library wiped from the
emulated flash after the chain was built. On the pedal (2026-11-20): no cut any more after a mode change. The original
Clone engine is not covered in the emulator (no capture in its flash): step 3 still to do.

1. Play, change the footswitch mode (BANK- + BANK+), keep playing 5 s: no cut ~2-3 s later.
2. A setting in GLOBAL (any), and SAVE of the preset: no cut either.
3. A preset with a Clone capture on the original engine (not NAM): it sounds as before; saving keeps the sound.
