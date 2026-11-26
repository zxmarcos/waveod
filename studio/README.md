# waveod Studio

The PC app of the waveod firmware (QME-200 / Sonicake Matribox II Pro). It talks to waveod's own protocol group
(0xF, `../src/editor/wv_msgs.c`) and its own preset format (`../src/fw/wv_preset.h`), so it is not limited by
what the original PC editor knows.

Self-contained: Python 3.10+ and its standard library only (Tkinter; USB MIDI through the Windows multimedia API
with ctypes, or CoreMIDI on macOS). Releases bring it as an app for Windows and for macOS (`tools/package_studio.py`). The new interface (`--web`, below) adds pywebview. The visual kit, the USB MIDI transport and the firmware updater started as copies of Matribox
Studio's (MIT licence) and live in `waveod_studio/ui` and `waveod_studio/usb`; the IBM Plex fonts are under the
SIL Open Font License (`waveod_studio/fonts/OFL.txt`).

## Running

    pip install -e studio            # from the repository root, or: cd studio && python -m waveod_studio
    python -m waveod_studio          # the app
    python -m waveod_studio --emu out/QME-200_waveod.bin     # the firmware in the emulator (tools/emu.py), no pedal
    python -m waveod_studio --emu FW.bin --select 0 --controls 12 --shot c.png   # screenshot of the controls editor

Close the original editor first: the MIDI port is exclusive. The app opens it only while an operation runs.

Command line, without the window: `python -m waveod_studio info | list | backup F | restore F | dump F [--all] |
latency [L|R|SEND|PRESET]`.

## What it does

- **Presets**: the 240 places (waveod's own records, the original's presets read only, empty ones), the details
  of each (style, BPM, volume, routing, the chain as the pedal plays it, its scenes), activate, rename, move, copy, export /
  import one preset (`.wvp`), backup / restore all of them (`.wvbak`: a zip with `manifest.json` and one `.wvp`
  per place, plus the global control table as `global_controls.wvc` with protocol 3; restoring the backup writes
  that table back too, older backups leave the pedal's as it is).
- **Controles** (a preset's details, firmware with protocol 2): what each footswitch does per footswitch mode, the
  toe switch, the pedals and the MIDI CCs, edited like the pedal's CONTROLES / PARA ONDE screens (destination,
  what of it, when or the curve, values; MIDI out with number and channel), the card names and LED colours;
  "Gravar no pedal" writes the preset with its table. With protocol 3 the global links that play in the preset
  (the keys it has no links on) show greyed under its own: "Copiar" makes them the preset's, "Sem globais" leaves
  the key doing nothing there; the details mark them (G).
- **Controles globais…** (the Presets bar, protocol 3): the pedal's global table in the same editor (functions,
  scenes and MIDI out; no blocks), written at once.
- **Efeitos** (protocol 5): the effect modules installed in the pedal's library (waveod/docs/blocks.md): install
  `.elf` modules built by `../tools/wvfx.py build`, remove them. The pedal checks the library and rebuilds the
  edited preset at once. `WAVEOD_EMU_WVLIB=LIB.bin` gives the emulated pedal a library at start-up.
- **NAM** (protocol 6): the 10 Clone slots and what each holds (original capture, NAM at 44.1 kHz or at 48 kHz
  resampled, NAM with a broken CRC, empty; a NAM capture's rate, loudness and CRC); "Gravar captura no slot…"
  writes the A2-Lite network of a `.nam` into the selected slot after a dialog with its name, the rate choice and
  what the slot loses (`../docs/nam.md`). Command line: `python -m waveod_studio nam F.nam --slot N`, `clone`.
- **Pedal**: firmware version, the preset store's use, firmware update, and (protocol 4) the latency test: the
  pedal plays a chirp on an output and finds it at the input through a cable (or through the active preset, no
  cable) and reports milliseconds, samples, buffers + converters (`../docs/latency.md`). With `--emu` the
  emulator plugs the cable itself, 7 samples beyond the buffers.
- **Flash**: read-only dumps (bootloader or the whole 8 MB) with a summary of each area.

## Layout

| File | |
|---|---|
| `waveod_studio/protocol.py` | `Pedal`: info, list, get / put, load, move, copy, describe, global controls, latency, the effect library, Clone slots, flash reads |
| `waveod_studio/wvpreset.py` | the preset format; unknown chunks are kept as they are |
| `waveod_studio/controls.py` | the control table (chunk 11): codec, describe's blocks and parameters, the texts |
| `waveod_studio/ui/controlsview.py` | the controls editor dialog |
| `waveod_studio/backup.py` | `.wvbak` files |
| `waveod_studio/wvlib.py` | waveod's effect library: read, add / remove modules (their descriptors), pack |
| `waveod_studio/ui/effectsview.py` | the Efeitos tab |
| `waveod_studio/irs.py` | user IRs: WAV conversion, the slot's bytes, the user IR file system |
| `waveod_studio/nam.py` | NAM captures: the A2-Lite network of a `.nam` packed for a Clone slot |
| `waveod_studio/ui/namview.py` | the NAM tab |
| `waveod_studio/flashmap.py` | the flash areas (`src/fw/flash_map.h`), dump summaries |
| `waveod_studio/bench.py` | protocol 11: an effect's cycles on its own (bench) and the preset playing (profile); the Desempenho tab and `../tools/bench.py` |
| `waveod_studio/app.py` | the window |
| `waveod_studio/ui/kit.py` | theme, fonts and widgets (canvas-drawn, anti-aliased by `ui/aa.py`) |
| `waveod_studio/ui/updateview.py` | the firmware update dialog |
| `waveod_studio/usb/htmidi.py` | the editor protocol's SysEx packets, LZO, Windows MIDI ports (winmm) |
| `waveod_studio/usb/coremidi.py` | the pedal's MIDI ports on macOS (CoreMIDI through ctypes); `htmidi` uses it there |
| `waveod_studio/usb/qmeupdate.py` | firmware upload through the bootloader |
| `waveod_studio/emulated.py` | the emulated pedal (`../tools/emu.py` + `../tools/check_editor.py`'s USB host) |
| `waveod_studio/shot.py` | window screenshots (`--shot out.png`) |
| `waveod_studio/webapp.py` | the new interface's window (pywebview) and the calls its page makes |
| `ui/` | the new interface (Svelte 5 + TypeScript + Vite), built into `waveod_studio/web/` |
| `PRODUCT.md`, `DESIGN.md` | who the app is for and its visual system (the pedal's palette and icons) |
| `tests/test_emulator.py` | every protocol operation against the firmware in the emulator |
| `tests/test_controls_web.py` | the control tables as the new interface sends them (no pedal) |
| `tests/test_irs.py` | WAV conversion and the user IR file system (no pedal) |
| `tests/test_flash_web.py` | flash dumps and their summary against a fake pedal (no USB) |

## The new interface (`--web`)

The classic window is being replaced by a web page in a native window ([pywebview](https://pywebview.flowrl.com),
WebView2 on Windows), screen by screen. The first one is **Grade** (GRID): the edited preset's chain on the
pedal's 2 x 6 grid, wired as the signal flows (série, paralelo with its split / merge joints, dual L/R), the
effect browser on the left (click or drag a model onto the grid; FX Loop, Send and Return included, which waveod
runs with its own code; the original's Split / Path B / Merge EQ entries are not offered, the routing modes do that
now, though a preset that has them still shows them), the selected block's knobs at the bottom, the
scenes C1-C4 (with protocol 10 a scene picked there takes the edits, as in the pedal's C1-C4 menu, and the menu beside
them copies the scene over another, renames it or reverts it to the stored preset; a knob's C switch gives it a value per
scene), the routing modes, saving. A Cab / IR block of waveod's engine gets a **Microfone** editor: the virtual mic
(`../src/engine/cabmic.h`) moved on the speaker seen from the front (position) and from the side (distance, height,
angle), its type, and the change it makes in the response, computed with the firmware's own tables
(`ui/gen_cabmic.py` turns `src/engine/cabmic_tables.c` into `ui/src/lib/cabmic_tables.ts`; run it again when
`tools/gen_cabmic.py` changes them). Every edit is live on the pedal (protocol 8,
`../src/editor/wv_rig_msgs.c`); knobs turned on the pedal show up in the app.

Without the pedal the window waits for it instead of giving up: `webapp.Api` looks for the pedal's MIDI port every
1.5 s and says why it cannot connect yet (no port: switch it on, plug the cable; the port held by another program;
no answer: still booting or another firmware; an older waveod protocol). A connected pedal's port is checked every
2 s, so unplugging it brings the waiting card over the dimmed screens and plugging it back reads everything again.
`tests/test_link.py` runs that state machine on fake ports.

The second is **Presets**: the 240 places as the pedal has them, 60 banks of A-D, each card with its chain as a
strip of its blocks' colours; the picked preset's details (facts, its chain on the grid as the pedal lays it out
on loading, its blocks, its scenes). Activate, rename (F2), move (drag a card; the presets between slide one place,
as the pedal's MOVE does; "Desfazer" moves it back), copy (Ctrl + drag, Ctrl+C / Ctrl+V), export / import `.wvp`,
backup / restore `.wvbak`. These use the protocol's older requests (GET, PUT, MOVE, COPY): any waveod firmware.

**Controles** (from a preset's details, protocol 2; **Controles globais…** in the Presets bar, protocol 3): the control
table editor, with the classic one's rules (`lib/controls.ts` follows `controls.py`; every link text is checked
against it). The keys on the left, each with what it does (the global link that plays there, marked G); the picked
key's links in the footswitch mode on top, the global ones under them (Copiar / Sem globais); the picked link's
destination, what of it, when or the curve, its values. Footswitch card names and LED colours, the modulators
MOD 1-4: each drawn as the pedal plays it (`lib/modwave.ts` follows `../src/engine/modulators.c`; the key list shows
a thumbnail). An LFO's shape over two cycles (the random ones: the values it takes after the preset loads) with its
start phase, picked from thumbnails of the seven shapes; an envelope following two plucked notes, so attack, release
and sensitivity show; a sequence as bars dragged to their values, its glides as ramps. A point runs along the
drawing at the rate (synced ones at the preset's BPM), and the first parameter it moves labels the right edge with
its values at 0 and 100 %. The settings are the grid's knobs (rate and times on a log scale; a synced rate shows,
dimmed, what the division gives, and turning it frees it); a sequence's steps are its bars (drag them, or the wheel
over one). The table is a draft until **Gravar no pedal** (Ctrl+S); `webapp.table_from` refuses a link the pedal would
drop instead of losing it. While a table equals what the original's footswitch fields give, the pedal keeps those
fields instead (`fw/wv_preset.c`), so such a preset still reads as having no table of its own.

**Efeitos** (protocol 5): the effect modules in the pedal's library, by menu, each with its size, its memory per
block and its knobs (ranges, defaults and named values written the way the pedal writes them); module files
(`.elf`, tools/wvfx.py) come in after a look at what they are (new, or replacing the installed one; a bad file is
left out), and are removed with the question about the presets that use them. The grid's browser reads its menus
again afterwards.

**NAM** (protocol 6): the 10 Clone slots, what each holds (empty, the original's capture, a NAM capture and the rate
it runs at, loudness, the weights' CRC); a `.nam` goes into a slot after a sheet that names what the slot loses
(red when it holds something), with the 48 kHz / 44.1 kHz choice for captures trained at 48 kHz.

**Pedal**: which waveod the pedal runs, what its places and store hold, its latency test (protocol 4: a cable from
an output back into the input, or what the active preset adds), and the firmware update (`usb/qmeupdate.py`, like
the original editor). The update takes the port from the connection, reboots a running pedal into its bootloader,
sends the four sections with the progress in packets (cancellable), logs every message to
`%APPDATA%\waveodStudio\update-*.log` and lets the connection find the pedal again. The waiting screen offers it
too when the pedal does not answer (stuck in its bootloader, another firmware) or runs an older waveod.
`tests/test_update.py` runs it against a simulated bootloader. **Cópia da flash** (the same screen, read only): the whole 8 MB or the bootloader into a file, with
the progress in KB (Cancelar stops at the next read); a whole dump, or one saved before (**Abrir cópia…**), is drawn as a
map of the areas (`flashmap.py`, `src/fw/flash_map.h`) with what each uses, where the app image ends and whether
waveod's effect library area holds a library or what an older firmware left there.

**IRs**: the 20 user IR slots the User IR blocks (and waveod's Cab / IR engine) play. What each holds is read from
the pedal's flash (`irs.user_slots` parses the small file system as `../src/fw/ir_store.c` does, reading only the
directory and the used blocks), so the response drawn is what really plays. A `.wav` (PCM or float, any rate; the
first channel) becomes 1024 samples at 44.1 kHz through a sheet with the new curve over the slot's, how much of the
file's energy stays, the conversion done and the level (the file's, or the factory cabs'); it goes in with the
original editor's message (0x14000001, any waveod). Rename with F2. The conversion is Matribox Studio's (`irwav.py`).

**Global** (protocol 9, `../src/editor/wv_settings_msgs.c`): the pedal's GLOBAL screen. The global EQ on top as the
pedal's EQ screen draws it (`lib/globalset.ts` computes the response with the firmware's own designs; drag a
stage's handle, pick a stage for its frequency / Q / gain, FLAT / VOLTAR), then the same cards as the pedal (In / Out,
Afinador, Áudio USB, EXP 2 / FS externos, MIDI, Presets, Tela, Pedais EXP). Every change sounds at once and the
pedal stores it after 2 s without changes; the expression pedals' calibration is shown, and done on the pedal.
An older waveod (protocol 8) gets a note to update it.

The third is **Bateria** (MEUS RITMOS, protocol 7): the 20 places of the pedal's own drum patterns; the picked
one's score drawn from what the pedal stores (the 13 sounds as rows, hits as strong as their velocity), play / stop
on the pedal, rename (F2), empty. A MIDI file comes in through a preview of what the pedal will play, converted
again when the kick / snare kit or the ending (loop or once) changes, with what was left out; nothing is written
before "Gravar".

    waveod-studio-web.bat                          # all of the below: finds Python 3.10+, installs pywebview,
                                                   # builds the page when it changed, opens the window
    waveod-studio-web-emu.bat                      # the same on the emulated pedal (out/QME-200_waveod_grid.bin)
    python ..\tools\buildfw.py                     # that firmware from the last commit, with its ELF beside it
                                                   # (out/QME-200_waveod_grid.elf: the emulator runs the ELF next to
                                                   # the .bin, so the pair always matches; it is also the update
                                                   # file for the pedal)

    pip install pywebview                          # once (or: pip install -e studio[web])
    cd studio/ui && npm install && npm run build   # once, and after changing the page (Node 20+)
    python -m waveod_studio --web                  # the pedal on USB (waveod with protocol 8; 9 for Global)
    python -m waveod_studio --web --emu out/QME-200_waveod.bin     # the firmware in the emulator

Working on the page: `npm run dev` in `ui/` serves it at http://localhost:5173 with live reload. In a plain
browser it runs on a demonstration backend (`ui/src/lib/demo.ts`, presets captured from the emulator in
`ui/src/lib/demo.json`; `?preset=N&select=SLOT&route=MODE&joint=split|merge` and `?screen=presets&pick=N`, `?screen=drums`
set it up for screenshots; files are not offered there);
`python -m waveod_studio --web --dev [--emu FW.bin]` shows the dev server in the app window, on the real pedal.
`npm run check` type-checks it. Layout of `ui/src`:

| Path | |
|---|---|
| `lib/api.ts` | the bridge to `webapp.py` (or the demonstration) |
| `lib/store.svelte.ts` | the state: the pedal's preset, the selection, edits queued one at a time, the poll |
| `lib/menus.ts` | the 16 effect menus: names, colours (`src/ui/theme.c`), line icons (`tools/mkicons.py`) |
| `components/Grid.svelte` | the chain grid, its wires and drag and drop |
| `components/Browser.svelte` | categories, search, models |
| `components/ParamPanel.svelte`, `Knob.svelte` | the selected block's (or joint's) parameters |
| `components/PresetBar.svelte` | preset, list of the 240, save, scenes, routing |
| `components/PresetsScreen.svelte`, `PresetDetail.svelte`, `MiniGrid.svelte` | the presets screen |
| `components/DrumsScreen.svelte`, `DrumRoll.svelte`, `DrumImport.svelte`, `lib/drums.svelte.ts` | the drums screen |
| `components/PedalScreen.svelte`, `lib/pedal.svelte.ts` | the pedal screen |
| `components/EffectsScreen.svelte`, `lib/fx.svelte.ts` | the effects screen |
| `components/NamScreen.svelte`, `lib/nam.svelte.ts` | the NAM screen (Clone slots) |
| `components/ControlsEditor.svelte`, `lib/ctl.svelte.ts`, `lib/controls.ts` | the control table editor (a preset's and the global one) |
| `components/IrsScreen.svelte`, `lib/irs.svelte.ts`, `lib/irmath.ts` | the IRs tab (user IR slots) |
| `components/GlobalScreen.svelte`, `EqPanel.svelte`, `lib/globals.svelte.ts`, `lib/globalset.ts` | the GLOBAL tab (settings and global EQ) |
| `components/FirmwareSheet.svelte` | the firmware update (from the pedal screen or the waiting screen) |
| `components/ConnectScreen.svelte` | waiting for the pedal (before it connects, and over the screens when it goes away) |
| `components/Dialog.svelte` | the app's questions and the progress of backup / restore |
| `components/MicEditor.svelte`, `lib/cabmic.ts`, `lib/cabmic_tables.ts` | the virtual mic editor of Cab / IR blocks |
| `lib/wires.ts`, `lib/floating.ts` | the grid's wires at any size; pop-up lists placed outside clipped boxes |
