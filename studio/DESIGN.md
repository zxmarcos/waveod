---
name: waveod Studio
description: The PC editor of the waveod firmware, in the pedal's own visual identity
colors:
  bg: "#0A0A09"
  surface: "#121210"
  raised: "#1A1917"
  line: "#1F1E1C"
  track: "#22211F"
  wire: "#3A3936"
  border-off: "#2E2D2A"
  dim: "#4A4844"
  off: "#5E5C57"
  label: "#8A8780"
  text: "#EDEBE6"
  accent: "#F2AF48"
  ok: "#75DF8F"
  error: "#FB5855"
  menu-dyn: "#17D0D8"
  menu-fx: "#E698DF"
  menu-wah: "#FE93A4"
  menu-drv: "#FF958D"
  menu-amp: "#F4A25C"
  menu-cab: "#D1B64A"
  menu-eq: "#80CD82"
  menu-mod: "#C4A4FE"
  menu-dly: "#73BDFF"
  menu-rvb: "#36D2BA"
  menu-clone: "#FD9976"
  menu-loop: "#9FB0FF"
  menu-vol: "#B5C159"
typography:
  display:
    fontFamily: "Barlow Semi Condensed, sans-serif"
    fontSize: "26px"
    fontWeight: 600
    lineHeight: 1.1
  label:
    fontFamily: "Barlow Semi Condensed, sans-serif"
    fontSize: "13px"
    fontWeight: 600
    letterSpacing: "0.06em"
  body:
    fontFamily: "IBM Plex Sans, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.4
  value:
    fontFamily: "IBM Plex Mono, monospace"
    fontSize: "13px"
    fontWeight: 500
rounded:
  sm: "6px"
  md: "10px"
  block: "20px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "16px"
  lg: "24px"
components:
  block:
    backgroundColor: "{colors.bg}"
    rounded: "{rounded.block}"
    size: "84px"
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.bg}"
    rounded: "{rounded.sm}"
    padding: "8px 16px"
---

## Overview

The pedal's screen, grown up for a computer. Near-black ground, one amber accent for the app's own actions, and
every effect in its menu colour, exactly as the QME-200 shows it. The layout the user chose: a grid of effect
blocks in the middle, the model browser on the left, the knobs of the selected block at the bottom. Colour belongs to the effects; the chrome stays neutral so the chain reads first.

## Colors

- `bg`, `surface`, `raised`: three steps of near-black for the window, panels and floating layers.
- `text` for content, `label` for secondary text (both pass 4.5:1 on `surface`), `off`/`dim` for what is off.
- `accent` (amber) only for the app's own state and actions: active tab, save, the modified mark, joints.
- Menu colours: a block's border, icon and knob arcs; the darker variant fills a selected block. A block that is
  off loses its colour (border `border-off`, icon `off`).

## Typography

Barlow Semi Condensed (the pedal's face) for names, labels and headings; IBM Plex Sans for running text; IBM
Plex Mono only for measured values (knob readouts, BPM, preset numbers).

## Layout

Top bar 52px (the screens' tabs, the language as a globe and its two letters at the right); status bar 26px along
the bottom (the connection: its state, the device, firmware, USB, protocol); left browser 300px; the rest is the
grid stage (scaled to fit, centred) over the parameter panel. The grid is 2 rows x 6 columns of 112px cells between
an IN pill and the OUT pill(s); a block pointed at shows two round badges over its top right corner (on/off,
remove). Minimum window 1100 x 700.

## Elevation & Depth

Flat panels separated by 1px `line` borders. Only floating layers (preset list, menus, toasts) cast a shadow:
`0 12px 32px rgba(0,0,0,.55)`. No glows.

## Shapes

Blocks are 84px squares, 20px radius, 2px border (3px when selected, with a 2px ring offset by 3px).
Buttons and fields 6px radius; panels 10px.

## Components

- Block: icon 34px stroke 1.9 in the menu colour; the model name under it (13px, `text`, truncates).
  Double-click toggles on/off; drag moves it to another cell; Delete removes it.
- Wire: 2px `wire`; joints (split/merge) are amber diamonds you can select.
- Knob: 270-degree arc, track `track`, value arc in the menu colour, value in Plex Mono under it.
  Drag vertically (Shift = fine), wheel = one step, double-click = default. Named values open a list.

## Do's and Don'ts

- Do use the pedal's words (Paralelo, Cena, Gabinete) and its icons.
- Do show every refusal from the pedal as a toast that says why.
- Don't add colour to chrome; don't glow; don't use gradient text or glass.
- Don't copy another editor's icons or exact look.
