# Cab / IR engine and the virtual mic

waveod runs the Cab (menu 5) and User IR (menu 6) blocks on its own engine (`engine/cabir.c`) instead of the
original's module (library entries 5.0 / 6.0, `CabIrInit` / `CabIrProcess`). The original's is still there:
GLOBAL › PRESETS › MOTOR CAB / IR = ORIGINAL switches back (the preset reloads), for A / B listening.

## What the original does

Read from its code (`tools/check_block.py` extracts it; disassembly with matribox-studio's `effdis.py`):

- the IR: 1024 int32 (24-bit, scale 2⁻²³) straight from the flash (factory cabs at 0x700000 + block, user IRs in
  their file system at 0x753000), mono: the left input only, the result on both outputs;
- convolution by FFT, 16 partitions of 64 (CMSIS `arm_cfft_sR_f32_len128`), so only blocks of 64 frames;
- level 0.27 × VOLUME / 100, moving towards the knob 0.002 per block (plus 2.27e-4 of the distance);
- limited to ±2.75441, then HIGH CUT and LOW CUT (Cookbook low / high pass, w = f × 1.4247585e-4, alpha = sin w
  × 0.70721358, direct form I, that order); a changed cut is recomputed with LOW CUT < 20 → 19 and HIGH CUT >
  20000 → 20001.

## waveod's engine

Uniformly partitioned overlap-save with partitions of the block size (16, 32 or 64; `dsp/rfft.c`, a real FFT
of 2 × block), the same level ramp, limit and filters. `tools/check_cab.py` runs every factory cab and a User IR
through both engines on the same blocks with the knobs moving: 82 to 88 dB under the signal above 30 Hz (float
rounding: the FFTs add in another order, and the 19 Hz low cut turns that into a slow drift under 30 Hz). On the
whole firmware in the emulator (preset 01A) the two engines give the same output and waveod's saves about 10% of
the preset's CPU (286.7 k → 257.6 k instructions per block). It also takes blocks of 32, which the original
cannot (docs/latency.md).

## The virtual mic

Four knobs, in knob positions the original's entries do not use (8-11), shown after VOLUME / LOW CUT / HIGH CUT:

| Knob | Range | 0 |
|---|---|---|
| Mic Position | −100 (centre of the dust cap) … +100 (edge of the cone) | the IR as captured |
| Mic Distance | −100 (on the grille) … +100 (far) | the IR as captured |
| Mic Angle | 0 … 45° | on axis |
| Mic Type | Original, 57 Dynamic | Original: the mic the IR was captured with |

At 0 / 0 / 0 / Original (older presets have zeros there) the IR is the flash data as it is: the sound is exactly
the engine's without a mic.

**Where the curves come from.** A factory cab is one IR, captured at one place, so moving the mic is modelled as
how the response changes when a mic moves, measured on real captures: a set of cab IR banks (analysed outside
this repository) holds, per cab, 12 mics × 2 angles × 6 distances × 4 positions (read from their
responses: along the 4 the highs fall towards the edge and the arrival comes earlier; along the 6 the low end falls,
the proximity effect; the 2 repeat that with less highs). The medians over 46 cabs × 12 mics, per third octave,
relative to the first step, become `cabmic_tables.c` (`tools/gen_cabmic.py`). A factory IR is taken to be
captured at the second position (the dust cap's edge) and the second distance, on axis.

| Move | What it does (medians) |
|---|---|
| dust cap → edge of the cone | up to −8 dB at 4-5 kHz, −3 dB at 16 kHz, nothing under 1 kHz |
| grille → far | lows down to −4.4 dB (no more proximity effect), +4 dB at 2-6 kHz, a small dip near 1 kHz |
| 0 → 45° | −1 to −2 dB in the highs, −1.5 dB near 800 Hz |

(The Ampero II's 6 "positions", `research/ampero/sim/mic_positions.py`, are a gentle synthetic tilt, ±1-2 dB, the
same on every mic, so they were not used.)

**Mic types.** The 12 mics, in the banks' order: 57 Dynamic, 421 Dynamic, 7 Dynamic, 906
Dynamic, 30 Dynamic, 121 Ribbon, 160 Ribbon, 4038 Ribbon, 84 Ribbon, 414 Cond, 47 Cond FET, 67 Cond; their
curves fit the names (the 57's low cut and presence peak, the ribbons' dark top, the 414's air). A type is that
mic's response against the median of the 12, the median over the 46 cabs, and with a type chosen the position /
distance / angle curves are that mic's own. Only the 57 is in the pedal for now (`tools/gen_cabmic.py --mics 0`;
more are an index list there, `CABMIC_TYPES` and the parameter's list in `model/param_format.c`). Against the median
mic the 57 cuts the lows (−6 to −7.5 dB at 25-50 Hz, −3 dB at 80 Hz) and has its presence peak (+4 to +6 dB at 5-10 kHz).

The mic of a factory IR is not known, so it is taken to be the median one: the factory guitar cabs' mean response
is 1.5 dB (RMS, 63 Hz-12.8 kHz) from the banks' median mic and 3.2 dB from their 57 (the nearest single mic is the 7
Dynamic, 1.3 dB), so choosing the 57 does not double a 57 already in the IR.

**How well it predicts real captures:** every cab of the banks in turn, with the curves made from
the other 45 and applied by the pedal's filter (a numpy copy of `cabmic_apply`), against the real capture of that
cab; RMS error per third octave, 63 Hz-12.8 kHz, level removed, median / 90th percentile over cabs and cases:

| Move | Not changing | Model |
|---|---|---|
| any of the other 11 mics → the 57, mic unknown (what the pedal does) | 2.80 / 5.55 dB | 1.73 / 3.67 dB |
| the same, the captured mic known (a future "captured with" knob) | 2.80 / 5.55 dB | 1.15 / 2.10 dB |
| position: to the dust cap's centre / the cone / its edge | 1.25 / 2.63 / 3.35 dB | 1.07 / 1.85 / 2.26 dB |
| distance: on the grille / steps 2-5 | 1.37 / 0.99-2.52 dB | 1.00 / 0.77-2.05 dB |
| 45° | 1.36 dB | 1.24 dB |
| the 57's moves with its own curves instead of the medians | | medians within ±0.04 dB, 90th percentiles up to 0.18 dB better |

**Against Shure's SM57 user guide** (version 3.6, 2025-A; the response chart digitized from its image, 1094
columns, in third octaves, both curves relative to 1 kHz):

| Band | 40 | 63 | 100 | 200 | 400 | 1 k | 2 k | 3.2 k | 4 k | 5 k | 6.4 k | 8 k | 10 k | 12.8 k | 16 k Hz |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Shure | −11.5 | −8.5 | −4.9 | −1.0 | −1.8 | 0 | +0.3 | +2.0 | +3.2 | +5.0 | +5.7 | +2.8 | +2.3 | +0.1 | −6.7 |
| waveod 57 | −7.9 | −5.7 | −2.8 | −0.9 | −0.9 | 0 | +0.3 | +0.9 | +1.7 | +3.1 | +4.1 | +4.2 | +3.4 | +1.7 | −0.3 |

200 Hz-12.8 kHz: 0.95 dB RMS, at most 1.9 dB (the presence peak at the same place, 1.5-1.9 dB lower and wider:
the 3-band smoothing, and the median mic has some presence of its own). Under 160 Hz waveod cuts 2-3.6 dB less:
Shure's chart is the mic away from the source, the banks' captures are close up, where the guide's own proximity
effect adds 6-10 dB under 100 Hz. Above 12.8 kHz the cab IRs hold almost nothing (the factory cabs are 30-36 dB
down there). The rest of the guide: the polar charts at 45° give −0.3 to −1.3 dB at 125-1000 Hz and −0.6 to −2.3 dB
at 2-8 kHz, the 57's angle curve −0.7 to −0.9 dB at 0.5-8 kHz (the banks' second angle is perhaps less than 45°);
"work close for extra bass" and "2 to 3 ft back: reduced bass" agree with the distance curves (grille → far: −3.9
dB at 40 Hz). Its placement table calls the cone's edge "higher frequency sound" and the centre "emphasized bass",
the opposite of the position curves (highs −8 dB at 4 kHz from the dust cap to the edge, lows unchanged); the
captures have the arrival ~4 samples (~3 cm, the depth of a cone) earlier at the step with fewer highs, which is where the
cone comes closest to the mic: its edge.

The model always gets closer to the real capture than leaving the IR as it is. What is left (about 1-2 dB) is
what one curve for every cab cannot know: how each cab's response changes, and the phase.

**How it is applied.** The knobs give a curve in dB per band (interpolated between the measured steps, relative
to the captured place). In the main loop (`cabir_poll`, from `chain_poll`) that curve becomes a minimum-phase
filter (real cepstrum on a 2048-point FFT, `engine/cabmic.c`), the IR goes through it (the last 64 samples
faded), and the new partitions go to the spare set of spectra; the audio interrupt crossfades to them over one
block. Playing costs nothing more; a change costs one recomputation (a few ms of the main loop).

## Screens

- The edit screen of a Cab / IR block has the mic parameters on its second row and a MIC pill; MENU on that row,
  or the pill, opens **MIC** (`ui/screens/scr_mic.c`): the speaker from the front with the mic on it, from the
  side with the mic at its distance and angle (drawn as the chosen type, named in the corner), the three values
  and the response change (±12 dB, 50 Hz-16 kHz). Knobs 1-3 = position / distance / angle, MENU turns = type, EFX =
  the mic back to where the IR was captured (the type stays), MENU or EXIT = back. Touch: tap or drag on the front
  view = position; drag the mic in the side view = distance and position; tap the type = the next one; hold =
  back to where the IR was captured.
- waveod Studio gets the four parameters through describe (controls, scenes, pedals can move them).

## Verification

- `tests/test_cabir.c`: the real FFT against a DFT; the engine against a direct convolution at blocks of 64, 32
  and 16 (over 100 dB under the signal); the level ramp, bypass, no IR; the mic: a flat curve leaves the IR as it
  is, the IR's response changes by the curve (within 1.5 dB per band), a move while playing ends where an engine
  set up that way is; the 57: its curve at the captured place, its own position curve, its low cut and presence
  peak, out-of-range types as captured, the parameter's names, a type change recomputes the IR.
- The curves against real captures, one cab left out at a time (above).
- `tools/check_cab.py`: against the original's engine, every factory cab and a User IR.
- The emulator: the whole firmware with each engine, and with the mic moved while playing.
- Not on the pedal yet.
