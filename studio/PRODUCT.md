# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

(A desktop app: the interface is a web page shown in a pywebview window by `waveod_studio/webapp.py`; it is not
published as a website.)

## Users

Guitarists who run the waveod firmware on a Sonicake Matribox II Pro (QME-200). They sit at a computer with the
pedal on USB, usually in a home studio, and build or tweak presets: choosing amps and effects, laying out the
chain, setting knobs, while playing through the pedal.

## Product Purpose

waveod Studio is the PC companion of waveod, an open firmware for the QME-200 written from scratch in C. It
edits the pedal live and manages what the pedal stores: presets, effect modules, NAM captures, drum patterns,
firmware updates, latency tests. Success: building a preset on the PC is faster and more pleasant than on the
pedal's 480 x 272 touch screen, and everything stays in sync with the pedal in both directions.

## Positioning

It speaks waveod's own protocol (group 0xF), so it can do what the original editor cannot: parallel and dual L/R
routing on a 2 x 6 grid, scenes, modulators, waveod's own effect blocks and NAM captures. The pedal is the
source of truth; the app shows what the pedal reports.

## Operating Context

- The pedal on USB MIDI (exclusive port: the original editor must be closed), or the firmware in the ARM
  emulator (`--emu`), or a demonstration mode with data captured from the emulator (no pedal).
- Edits are live in the pedal's edited preset; saving writes the flash, like the pedal's SAVE.
- The user may turn knobs on the pedal while the app is open: the app follows.

## Capabilities and Constraints

- 240 presets (01A..60D), 12 block slots in a grid of 2 rows x 6 columns; routing series, parallel (split /
  merge columns, lane B = row 2) or dual L/R; 4 scenes per preset.
- 16 effect menus (DYN, FX, WAH, DRV, AMP, CAB, IR, EQ, MOD, DLY, RVB, CLONE, LOOP, SND, RTN, VOL); the browser
  offers 13 of them, like the pedal.
- Parameter texts come from the pedal (formatted as the pedal shows them).
- The UI language is Brazilian Portuguese, matching the pedal.
- Tabs other than GRID (presets, effects, NAM, drums, pedal) still live in the Tkinter app while the new
  interface grows.

## Brand Commitments

- Name: waveod (lowercase), "waveod Studio" for the app.
- The pedal's own visual identity is binding: its palette (src/ui/theme.c: near-black, amber accent, one
  colour per effect menu) and its line icons (tools/mkicons.py, the SVG paths of the claude.ai/design project).
- Structure in the spirit of Neural DSP's Cortex Control (grid of blocks, side browser, knob panel), chosen by
  the user; its look and icons are not copied.
- Effects ported from other products are never named after their source in files or commits.

## Evidence on Hand

- Demo presets captured from the emulator (`ui/src/lib/demo.json`: WAVEOD CRUNCH, CLEAN SPACE, FUZZ LEAD ...).
- The effect catalog of the firmware build in use (stock library + waveod's built-in blocks).
- No users, reviews or metrics exist; none may be invented.

## Product Principles

1. The pedal is the truth: show what it reports, never a guess that outlives its answer.
2. Same words, colours and icons as the pedal: what you learn on one works on the other.
3. Direct manipulation first: drag, click, turn; dialogs only when something is destroyed or written to flash.
4. Nothing silent: every refused edit says why (no memory, chain full).
