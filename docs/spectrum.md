# Spectrum analyser

The edit screen can show the spectrum of the guitar before and after the block being edited, live, while its
knobs turn: FFT in the edit screen's head. The analyser takes the upper half (the block strip goes away, the head
gets thinner) and the active row of parameters stays below, on the three knobs.

```
┌──────────────────────── top bar ────────────────────────┐
│ DRV  Blues OD ▾                          [FFT] [ ON ]   │
│ -20         ╱╲                         ANTES  DEPOIS    │
│ -40        ╱  ╲      ╻                                  │
│ -60   ____╱    ╲    ╱╲    ╻   ╻  ╻ ╻ ╻                  │
│ -80 _╱  50  100  200  500  1k  2k  5k  10k              │
├──────────────────┬──────────────────┬──────────────────┤
│ GAIN       20    │ TONE       73    │ VOLUME     35    │
└──────────────────┴──────────────────┴──────────────────┘
```

## What it shows

- **BLOCO** (the default): the block's input ("ANTES", a grey line) and its output ("DEPOIS", a filled area in the
  block's colour). An EQ moves the curve, a drive adds harmonics, a cab cuts the highs, a NAM capture shows the
  amp's shape.
- **PRESET** (tap the legend): the guitar at the chain's input ("ENTRADA") and the chain's output ("SAÍDA"),
  before the preset level and MASTER.
- Axes: 20 Hz .. 20 kHz on a log scale; −90 .. 0 dB of the converters' full scale (a full-scale sine reads 0 dB),
  lines every 20 dB and at 50, 100, 200, 500 Hz, 1, 2, 5, 10 kHz.
- Touch: tap the plot = a cursor with the frequency and both levels ("113 Hz −21 / −16 dB"); tap it again = no
  cursor; hold = freeze ("PAUSA"), hold again = run.
- MENU turns the row as before (the title shows LINHA n/m); swipe the row up / down = next / previous row.

The analyser stays on for the edit screen until FFT is tapped again (it is not stored: off after a restart).

## How

- **Taps** (`engine/scope.c`): two aligned rings of 8192 mono samples ((L + R) / 2), "before" and "after", in the
  SDRAM. `engine/chain.c` copies the watched slot's input before it runs and pushes both after it (a block of 64
  frames, no cost for the other slots); for PRESET, `engine/audio_engine.c` pushes the filtered guitar and the
  chain's output; for the global EQ screen (SCOPE_EQ) it pushes the EQ's input and output. Only the rig's own chain
  is tapped, never a spillover tail's copy. Nothing arrives while the slot does not run.
- **Analysis** (`dsp/spectrum.c`): the last 4096 samples (10.8 Hz bins, a 93 ms window) through a Hann window and
  `dsp/rfft.c` (now up to 4096 points); per column of the axis, the loudest bin in it, or below ~200 Hz, where a
  column is narrower than a bin, the power interpolated at its centre.
- **Display** (`ui/widgets/analyzer.c`): an LVGL canvas (`LV_USE_CANVAS`) whose XRGB8888 pixels it writes itself:
  the background row by row, the "after" area column by column, the curves on top; labels are LVGL labels over it.
  At most 25 frames per second, each after 1024 new samples; the curves rise at once (70 % per frame) and fall
  30 dB per second, and fall to the floor 150 ms after the last sample.

## Cost

Main loop only (the audio interrupt adds a copy of one block of the watched slot): 1.47 M instructions per frame
in the emulator (the two FFTs 0.75 M, the drawing 0.45 M, the columns 0.17 M), about 6 % of the CPU at 25 frames per
second while the analyser shows, nothing when it is off. Memory: ~0.45 MB of SDRAM (rings, history, FFT tables,
the canvas), no OCRAM; in the ITCM only the taps' few hundred bytes of code.

## Verification

- `tests/test_spectrum.c` (host): tones from 40 Hz to 12 kHz at −40..−6 dB read within 1.6 dB at their frequency
  (Hann's scalloping is 1.4 dB); 100 dB down 4 kHz away from a 1 kHz tone; two notes a third apart at A4 apart by
  20 dB between them; silence at the floor; the rings in order, stereo to mono, a late reader gets the newest, a
  new target empties them.
- Emulator (the whole firmware, preset 01A, a tone into the guitar input): DRV Blues OD, FFT: the tone and the
  drive's harmonics in the block's colour; the cursor's readout; PRESET mode with the chain's harmonics; freeze;
  FFT off brings the usual screen back.
- On the pedal (9883847): works. Known: with FFT on, faint clicks can be heard **in the headphones only**, about at
  the analyser's frame rate (seen with preset 38C, FFT on its AMP block). They are not in the audio: every block
  was processed in time (worst 1139 us with FFT on or off, no late DMA block, no SAI FIFO error), and the output is
  bit-exact with FFT on or off in the emulator; they drop by ~95 % on PAUSA. Most likely interference of the
  display's redraw (half the screen, 25 times a second: SDRAM and LCD activity) on the headphone amplifier.
