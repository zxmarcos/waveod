# Changelog

waveod's versions (semantic versioning, see [docs/releasing.md](docs/releasing.md)).

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
