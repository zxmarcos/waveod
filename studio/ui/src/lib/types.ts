// What waveod_studio/webapp.py hands over: the answers of protocol 8 (src/editor/wv_rig_msgs.c) as
// waveod_studio/protocol.py decodes them.

export type ItemKind = 'library' | 'builtin' | 'module' | 'missing' | 'routing';

export interface Block {
  slot: number;         // 0..11, storage index (not the chain position)
  on: boolean;
  menu: number;
  index: number;        // catalog index in the menu
  id: number;
  cell: number;         // 0..11 = row * 6 + column, -1 = none
  kind: ItemKind;
  params: number;
  name: string;
  type: string;         // the browser's group
}

export interface Route {
  mode: number;         // 0 series, 1 parallel, 2 dual L/R
  split: number;        // first column of lane A
  merge: number;        // column after lane A (parallel)
  joints: number[];     // split balance, level A, level B, merge mix, pan A, pan B (0..100)
}

export interface Rig {
  gen: number;
  loads: number;
  index: number;
  dirty: boolean;
  volume: number;
  tempo: number;
  route: Route;
  name: string;
  scene: { active: number; editing: boolean; names: string[] };
  order: number[];
  blocks: Block[];
}

export interface Param {
  k: number;            // library order: what an edit names
  knob: number;
  synced: boolean;
  scene: boolean;
  min: number;
  max: number;
  step: number;
  default: number;
  value: number;
  name: string;
  text: string;         // as the pedal shows it
  options: string[];    // named values, indexed by the value
}

export interface CatalogItem {
  index: number;
  id: number;
  kind: ItemKind;
  name: string;
  type: string;
}

export interface PresetEntry {
  index: number;
  origin: number;
  place: number;        // 0 waveod's, 1 the original's, 2 empty
  name: string;
  active: boolean;
}

/** a stored preset as the presets screen shows it (webapp.preset_view) */
export interface PresetDetail {
  index: number;
  origin: number;       // 0 user, 1 factory, 2 empty
  name: string;
  style: string;
  volume: number | null;
  bpm: number | null;
  bytes: number;
  route: { mode: number; split: number; merge: number };
  blocks: { slot: number; on: boolean; menu: number; index: number; id: number; cell: number }[];  // signal order
  scenes: { own: boolean; active: number; names: string[]; on: number[][]; params: number };
  mods: number;         // modulators in use
  controls: boolean;    // a control table of its own
}

/** a .wvp picked for import */
export interface PresetFile { path: string; file: string; name: string; blocks: number }

/** a .wvbak written or picked for restore */
export interface BackupInfo {
  path?: string;
  file: string;
  presets: number;
  global: boolean;
  created?: string;
  firmware?: string;
}

export interface Progress {
  label: string; done: number; total: number; unit?: string;
  cancel?: boolean;      // the box offers Cancelar
  task?: string;         // what Cancelar stops: 'dump' (cancel_task) or the firmware update (cancel_update)
}

/** an effect module of waveod's library (protocol 5; webapp.module_view) */
export interface FxParam {
  name: string;
  min: number;
  max: number;
  step: number;
  default: number;
  unit: string;         // a printf format as the pedal uses it ('%.1f dB', 'x%.2f'), '' for a plain number
  options: string[];    // named values, indexed by the value
  knob: number;
  sync: boolean;
}
export interface FxModule {
  id: number;
  hex: string;
  menu: number;
  name: string;
  category: string;
  flags: number;
  state: number;        // bytes per instance
  buffer: number;
  size: number;         // bytes of the module
  params: FxParam[];
}
export interface FxLibrary { capacity: number; used: number; modules: FxModule[] }
/** a module file picked for install: the module, or why it cannot be */
export type ModulePick = Partial<FxModule> & { path: string; file: string; error: string };

// ---------------------------------------------------------------- the Desempenho screen (protocol 11, waveod_studio/bench.py)
/** a slot of the preset playing, timed in the audio interrupt (cycles per 64-frame block) */
export interface ProfileSlot { slot: number; menu: number; id: number; blocks: number; mean: number; max: number; last: number }
export interface Profile {
  core_hz: number;
  budget: number;       // the cycles of one block at the core clock (0 in the emulator's answer when it has none)
  block_ms: number;     // the engine's whole block, mean
  worst_ms: number;
  blocks: number;
  slots: ProfileSlot[]; // chain order
  sections: number[];   // mean cycles of each section of the interrupt (perf.svelte.ts sectionName), the chain's with the slots
}
/** a module to measure, a .elf or a block's .c (built by the SDK): read again (built again) at every measurement;
 *  a source that does not build has error and the compiler's log instead of the module */
export type BenchModule = Partial<FxModule> & {
  path: string; file: string; source: boolean; modified: number; error: string; log: string; seconds: number;
};
export type BenchTarget = { kind: 'catalog'; menu: number; index: number } | { kind: 'file'; path: string };
export interface BenchOpts { blocks: number; skip: number; cold: boolean; bypass: boolean }
export interface BenchResult {
  status: number;       // 0, or the load's error (bench.STATUS)
  status_text: string;
  load: number;         // load + init cycles
  overhead: number;
  blocks: number;
  min: number; median: number; p99: number; max: number; mean: number;
  crc: string;          // CRC-32 of the output and FX send: the same where the effect computes the same
  peak: number;
  pools: { code: number; const: number; state: number; buffer: number };
  core_hz: number;
  budget: number;
  cold: boolean;
  bypass: boolean;
  cycles: number[];     // every measured block
  menu: number;
  emu: boolean;         // the emulator: its cycles mean nothing, its CRC does
  modified?: number;    // a file's time when it was read
  size?: number;
  error?: string;       // status -100: the source did not build (log: the compiler's words)
  log?: string;         // a source's build output
  build_s?: number;
  name?: string;        // a file's module: its descriptor's name
}
export const CHAIN_SECTION = 5;

/** a Clone slot (protocol 6): empty, the original's capture, or a NAM capture run by waveod's engine */
export interface CloneSlot {
  slot: number;
  kind: number;         // 0 empty, 1 the original's capture, 2 NAM 44.1 kHz, 3 NAM 48 kHz resampled, 4 NAM broken
  kind_text: string;
  named: boolean;
  name: string;
  sample_rate?: number;
  loudness?: number;
  crc?: number;
}
/** a .nam picked for a slot, nothing written yet */
export interface NamFile {
  path: string;
  file: string;
  name: string;
  arch: string;         // the network in words: 'WaveNet A1 (16 / 8 canais)', 'A2 (SlimmableContainer)', 'LSTM'
  lite: boolean;        // it holds the A2-Lite network the NAM engine runs
  convert: boolean;     // it can be converted for the original's Clone (the converter is built)
  di: 'editor' | 'builtin';   // the reference DI the conversion uses
  weights: number;
  loudness: number | null;
  sample_rate: number;
  bytes: number;
  rate48: boolean;      // trained at 48 kHz: resampled, or written to run at 44.1 kHz
}

/** a firmware .bin checked as the original editor checks it */
export interface FirmwareFile {
  path: string;
  file: string;
  version: string;
  size: number;
  sections: { name: string; size: number }[];
}
export interface UpdateDone { seconds: number; version: string; log: string }

/** the pedal screen: protocol.Pedal.info() and what the places hold */
export interface PedalInfo {
  name: string;
  version: string;
  protocol: number;
  presets: number;
  active: number;
  modified: boolean;
  store: { sectors: number; used: number; free: number; live: number };   // waveod's store (sectors of 4 KB)
  own: number;
  original: number;
  vacant: number;
}

/** the pedal's latency test (protocol 4, protocol.Pedal.latency_result) */
export interface Latency {
  status: number;       // 0 none, 1 measuring, 2 ok, 3 unstable, 4 no signal, 5 clipped, 6 cancelled
  status_text: string;
  route: number;        // 0 out L -> input, 1 out R -> input, 2 send -> return, 3 the active preset (no cable)
  runs: number;
  total: number;
  level_db: number;
  min: number;          // samples
  max: number;
  mean: number;
  match: number;        // 0..1
  buffers: number;      // samples the buffers alone take
  rate: number;
  ms: number;
}

/** MEUS RITMOS (protocol 7): the 20 places of the pedal's own drum patterns */
export interface DrumSlot { place: number; once: boolean; bpm: number; events: number; name: string }
export interface DrumState {
  places: number;
  sounds: number;
  max_events: number;
  playing_place: number | null;   // null: the bank's pattern is chosen
  playing: boolean;
  slots: (DrumSlot | null)[];
}

/** what a pattern plays: hits [tick, sound 0..12, velocity] over `length` ticks of 1/120 beat */
export interface DrumRoll { hits: [number, number, number][]; length: number; once: boolean }
export interface DrumPattern extends DrumRoll { name: string; bpm: number; events: number }

/** a MIDI file converted for import, nothing written yet (drums.summary) */
export interface MidiPreview extends DrumRoll {
  path: string;
  file: string;
  name: string;
  bpm: number;          // 0: none in the file
  bars: number;
  bar: number;          // ticks per bar
  beats: number;
  seconds: number;
  notes: number;
  events: number;
  dropped: string;      // what has no sound on the pedal ('' none)
  cut: number;          // bars left out to fit
  all_channels: boolean;
  kit: 'acustico' | 'eletronico';
}

/** where the connection stands (webapp.Api.status); asked every second or so, never blocked by a pedal call */
export type LinkPhase = 'starting' | 'searching' | 'busy' | 'silent' | 'old' | 'connected' | 'failed' | 'updating';
export interface LinkStatus {
  phase: LinkPhase;
  message: string;
  code?: string;        // the message's catalog key ('py.link_none'...), '' for a text of its own
  transport: string;    // 'usb' | 'emulador' | 'demonstração'
  attempts: number;
  connections: number;  // bumped on every connection: everything is read again
  name: string;
  version: string;
  protocol: number;
}

export interface Hello {
  connected: boolean;
  transport: string;    // 'usb' | 'emulador' | 'demonstração'
  name: string;
  version: string;
  protocol: number;
  error?: string;
}

export interface EditResult {
  result: number;
  gen: number;
  param?: Param;
}

export const EDIT = {
  PARAM: 0, ON: 1, PLACE: 2, MOVE: 3, REMOVE: 4, REPLACE: 5, ROUTE: 6, JOINT: 7, SCENE: 8, VOLUME: 9, BPM: 10,
  SAVE: 11,
  // protocol 10: the scene menu
  SCENE_PICK: 12, SCENE_COPY: 13, SCENE_REVERT: 14, SCENE_NAME: 15, SCENE_PARAM: 16,
  // protocol 12: the edited preset's name (the save screen's NOME)
  NAME: 17,
} as const;

// ---------------------------------------------------------------- the control tables (waveod_studio/controls.py)
/** a link "key -> what" (struct control_link): see lib/controls.ts for the codes */
export interface CtlLink {
  key: number; modes: number; when: number; action: number; slot: number; arg: number;
  curve: number; flags: number; off: number; on: number;
}
export interface CtlTable { links: CtlLink[]; colour: number[]; label: string[] }
export interface CtlParam { index: number; knob: number; name: string; min: number; max: number; step: number }
export interface CtlBlock { slot: number; menu: number; name: string; params: CtlParam[] }
/** a modulator (wvpreset.Preset.mods) */
export interface ModSettings {
  type: 'off' | 'lfo' | 'env' | 'seq'; shape: string; sync: number; rate: number; steps: number; phase: number;
  glide: number; sens: number; attack: number; release: number; step: number[];
}
export interface ControlsData {
  index: number;
  name: string;
  own: boolean;                     // the preset stores a table (else it is converted from the original's fields)
  table: CtlTable;
  blocks: CtlBlock[];
  knobs: Record<string, number[]>;  // slot -> the stored knob values
  scenes: string[];
  mods: ModSettings[];
  bpm?: number | null;              // the preset's tempo (the modulators' synced rates)
  global: CtlTable | null;          // null: the pedal has no global table (protocol < 3)
}
// ---------------------------------------------------------------- flash dumps (webapp.flash_view)
export interface FlashArea { name: string; start: number; end: number; used: number; last: number | null; note: string; detail: string }
export interface DumpDone { file: string; size: number; seconds: number; summary: FlashArea[] | null }
// ---------------------------------------------------------------- the GLOBAL screen's settings (protocol 9)
export interface Setting { value: number; min: number; max: number; step: number; ro: boolean }
/** protocol.Pedal.settings: each setting by name (protocol.SETTING_IDS), the global EQ's 31 parameters as eq */
export interface SettingsReply { eq: Setting[]; [name: string]: Setting | Setting[] }
export type SettingChanges = Record<string, number | Record<number, number>>;
// ---------------------------------------------------------------- user IRs (waveod_studio/irs.py)
export interface IrSlot { slot: number; name: string; used: boolean; samples: number[] | null }
/** a .wav as a slot would hold it (webapp.ir_preview) */
export interface IrFile {
  path: string; file: string; name: string; samples: number[];
  rate: number; channels: number; bits: number; fmt: string; frames: number; ms: number; peak_db: number;
  kept: number; resampled: boolean; cut: boolean; normalized: boolean; gain_db: number; clipped: number;
}