# Changelog

waveod's versions (semantic versioning, see [docs/releasing.md](docs/releasing.md)).

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
