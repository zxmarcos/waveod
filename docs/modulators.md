# Modulators

Four modulators per preset (MOD 1-4) move any parameter of its blocks on their own: an LFO, an envelope that
follows the guitar, or a step sequencer. They are keys of the control table like the pedals: a modulator's
parameter links go from their `off` value (the modulator at 0) to their `on` value (at 1) along a curve, and one
modulator may move any number of parameters.

| Type | Value 0..1 | Settings |
|---|---|---|
| LFO | a shape repeating: sine (from its bottom), triangle, square, saw up / down, random (a new value each cycle), smooth random | rate 0.05-20 Hz or synced to a note division of the tempo (1/1 .. 1/16, dotted, triplets); start phase |
| ENVELOPE | the guitar's level at the chain's input, -60 dB .. 0 dB (plus the sensitivity) | sensitivity -20..+40 dB, attack, release |
| SEQUÊNCIA | 1-16 steps of 0..100 % one after the other | rate (a step per 1 / rate s) or synced (a step per division); glide (% of a step) |

They restart when the preset loads, so an LFO or a sequence starts from the same place every time.

## How it works

- **Data:** the preset's `struct preset_mods` (`fw/preset_format.h`), chunk 12 of waveod's format. The links are
  in its control table with the keys `CK_MOD1`..`CK_MOD1 + 3` (after the pedals). Only the preset's own table: a
  global link has no modulator to follow.
- **Routes** (`model/modulation.c`): the main loop turns the parameter links of modulators that are on into
  routes: slot, knob, the two values, the curve, the parameter's range, and its step when it has fewer than 20
  (switches and types then jump from step to step). They are handed to the engine:
  - when the chain is built (before it runs);
  - when the rig tells a change;
  - at every edit of the links or the modulators.
- **The audio interrupt** (`engine/modulators.c`):
  - Once per block (1.45 ms), before the chain, it works out each modulator's value and writes the knob words.
  - So the movement is exact to the block whatever the main loop does, and the effects read it like any knob.
  - It writes nothing while the chain is not playing (while it is being built, or after a suspend).
  - The new set of routes takes the place of the old one at a block boundary (two copies, a flip).
- **The preset itself is not changed:** a modulated knob keeps its stored value, so saving does not store where
  the modulator happened to be. The edit screen shows the moving value, and its corner names MOD n.
- **Cost:** one route and one modulator add about 0.1 k instructions per block to the engine. The Stereo Util it moved
  in the check below costs the rest (+1.1 k in all).

## On the pedal

CONTROLES (CTRL on STOMP / CENAS) › a key's links, TECLA turned to MOD 1-4 (after the pedals):
- **The modulator's own rows**, instead of NOME / COR:
  - TIPO, with its value now (AGORA n %, live);
  - then by type: FORMA, SINCRONIA (LIVRE or a division), VELOCIDADE (Hz, when free), FASE INICIAL;
    SENSIBILIDADE, ATAQUE, RETORNO; SINCRONIA, VELOCIDADE, PASSOS, DESLIZE and one row per step.
  - Knob 1 turns a value, MENU press steps it up.
- **The links below**, as a pedal's: + NOVA LIGAÇÃO opens PARA ONDE with the chain's blocks and, in them, only
  their parameters; then the values at MOD 0 % and MOD 100 % and the curve.

## In waveod Studio

Presets › Controles…: the keys list has MODULADORES (MOD 1-4).
- A modulator's settings show above its links, the step values as a row of fields.
- "Gravar no pedal" stores the links and the modulators together.
- `wvpreset.Preset.mods()` / `set_mods()` read and write the chunk.

## Verification

- `tests/test_mods.c` (host):
  - every shape at its time;
  - synced to 1/4 at 120 BPM;
  - the start phase;
  - random in range, a value per cycle;
  - the sequencer's steps and glide;
  - the envelope at -20 dB and back to 0 in silence;
  - the chunk through waveod's format;
  - routes from 01A's table (none for an off modulator, the whole range by default, stepped values on their grid);
  - nothing written while the chain does not play.
- `tools/check_mods.py FW.bin` (the whole firmware in the emulator, presets over USB): Stereo Util's Gain on
  MOD 1 from -24 to 0 dB, the output level per block against the modulator off:

  | modulator | result |
  |---|---|
  | LFO square 5 Hz | 582 blocks away from a change, all at the expected level (0 or -24 dB) |
  | LFO square 1/4 at 120 BPM | 594 blocks, all right (250 ms each) |
  | ENV, sens 0, a tone at -14 dBFS | -5.60 dB (computed -5.59) |
  | SEQ 4 steps at 10 Hz | 582 blocks, all right (-24, -18, -12, 0 dB) |
  | get | the modulators come back as stored |

- The screens in the simulator (MOD 1 as LFO and as SEQUÊNCIA, PARA ONDE with parameters only) and the Studio's
  dialog against the emulated pedal.
- Not on the pedal yet: docs/verification.md, "Modulators on the pedal".
