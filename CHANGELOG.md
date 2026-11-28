# Changelog

waveod's versions (semantic versioning, see [docs/releasing.md](docs/releasing.md)).

## v0.2.0

- Synth (FX › Synth): the guitar as a monophonic synthesizer, from its pitch and envelope: twelve waveforms, a
  second oscillator, a resonant filter with its envelope, glide, and the notes followed or held.
- 20 NAM / Clone capture slots instead of 10. The original's ten are untouched; the new ones live in the same
  Clone area, where the original firmware does not look. The Studio's NAM screen shows all twenty.
- CONTROLES has a list view: every key (footswitches, pairs, the active preset's key, pedals, modulators, the MIDI
  CCs with links, the quick knobs) on one screen with its first links; the button at the top right, or CTRL held,
  switches between the cards and the list, and the pedal remembers which.
- A global link can be removed: NONE (REMOVE) at the top of PARA ONDE, or MENU held on the links screen. On the
  global table it goes from every mode it was in.
- waveod Studio: a status bar with the pedal, its firmware and the protocol; the language as a short code at the
  top right; power and remove buttons on a grid block under the pointer; the browser's IR and capture names follow
  a write or a rename at once.
- USB audio refuses a sample rate the pedal does not run instead of streaming at the wrong one.
- Editor protocol 15: the pedal's controls driven over USB (buttons, encoders, touch, pedals) and its display read
  back. The simulator loads a pedal's flash, plays a WAV into the input and records its screen and outputs.

## v0.1.2

- The pedal and waveod Studio in five languages: English (the default), Portuguese, Spanish, German and Italian.
  The pedal's language is in GLOBAL › DISPLAY › LANGUAGE (and in the Studio's Global tab); the Studio follows the
  computer's language, or the one chosen in its top bar. Effect, parameter and option names stay as they are.
- The translations live in plain JSON files (`i18n/<language>/`); adding a language is adding its files
  (docs/i18n.md).
- The controls screen's knobs and MIDI bars keep to one line, ending in "…" when the text is longer.

## v0.1.1

- The update file brings waveod's own effects: the circuit drives (Dark Muff, Dark Muff Lite, Green Drive, Rodent
  Dist, Round Fuzz, Treble Boost), the modulations (Chorus, Flanger, Phaser, Rotary, Vibe, Tremolo), the Delay and
  the Reverb, Plate and Spring are in the effect library after an update, with no need to install them from the
  Efeitos tab. A copy installed there before is not listed twice.
- Modulators on the pedal: a second page of CONTROLES with a card per modulator (MOD 1-4) and a small live
  drawing of each, opening the new MODULADORES screen: the modulator's drawing as it plays (the LFO's shape, the
  sequence's steps, the envelope), its settings and the parameters it moves. They were only reachable from the
  links screen's TECLA before.
- waveod Studio: the modulators drawn as the pedal plays them (the LFO shapes, the random ones with the values the
  pedal takes; the envelope on two plucked notes; the sequence's steps dragged on their bars), their settings on
  knobs.
- waveod Studio for macOS (Apple silicon), the pedal's MIDI on CoreMIDI.

## v0.1.0

The first public version.

- A firmware of its own for the Sonicake Matribox II Pro (QME-200): interface, effect host, audio, USB audio and
  MIDI, the editor protocol, the stock flash formats (presets, global settings, IRs, Clone captures).
- Every stock effect ported to readable C, bit for bit, and optimised (about 30% fewer CPU cycles).
- waveod's own blocks: NAM engine for the Clone slots (44.1 and 48 kHz), Cab / IR engine with a virtual mic,
  circuit-level drives, light modulation, delay and reverb engines, FX loop, sidechain gate and compressor,
  stereo utility.
- Series, parallel and dual L/R routing on a 2 x 6 grid; four scenes and four modulators per preset; a control
  table for footswitches, expression pedals and MIDI.
- Tuner, looper, drum machine with user rhythms from MIDI files, global EQ, spectrum analyser, spillover,
  latency meter, CPU temperature.
- Crash protection; the bootloader write-protected in the flash chip at every start-up.
- waveod Studio: grid editor, presets and backups, effect library, NAM conversion and capture slots, IRs,
  rhythms, global settings, CPU profile and block bench, firmware updates.
- An SDK for effect blocks (`src/sdk/wvfx.h`) installed without a firmware update.
- Releases built by GitHub Actions from their tag: the update files and waveod Studio for Windows (no Python
  needed) attached to the release.
