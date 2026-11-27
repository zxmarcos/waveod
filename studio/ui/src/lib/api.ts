// The one place the interface talks to the pedal. In the app window the calls go to waveod_studio/webapp.py
// (pywebview's js_api); in a plain browser (npm run dev) a demonstration backend plays the part with data
// captured from the emulated pedal (demo.ts).

import type {
  CloneSlot, ControlsData, CtlTable, DumpDone, FirmwareFile, IrFile, IrSlot, SettingChanges, SettingsReply, ModSettings, NamFile, UpdateDone,
  BackupInfo, CatalogItem, DrumPattern, DrumState, EditResult, FxLibrary, Hello, Latency, LinkStatus, MidiPreview, ModulePick, PedalInfo, Param, PresetDetail, PresetEntry,
  PresetFile, Progress, Rig, Profile, BenchModule, BenchTarget, BenchOpts, BenchResult,
} from './types';

export interface Backend {
  // the page's own preferences (kept between runs) and its language, for the backend's messages
  prefs(): Promise<Record<string, unknown>>;
  setPrefs(changes: Record<string, unknown>): Promise<void>;
  setLanguage(code: string): Promise<void>;
  status(): Promise<LinkStatus>;
  retry(): Promise<void>;
  hello(): Promise<Hello>;
  rig(): Promise<Rig>;
  block(slot: number): Promise<Param[]>;
  catalog(menu: number): Promise<CatalogItem[]>;
  edit(op: number, a?: number, b?: number, c?: number, value?: number, text?: string): Promise<EditResult>;
  presets(): Promise<PresetEntry[]>;
  load(index: number): Promise<void>;
  // the presets screen
  preset(index: number): Promise<PresetDetail>;
  rename(index: number, name: string): Promise<string>;
  move(src: number, dst: number): Promise<void>;
  copy(src: number, dst: number): Promise<void>;
  progress(): Promise<Progress | null>;
  exportPreset(index: number): Promise<string | null>;        // the file's name; null when cancelled
  pickPreset(): Promise<PresetFile | null>;
  importPreset(index: number, path: string): Promise<void>;
  backup(): Promise<BackupInfo | null>;
  pickBackup(): Promise<BackupInfo | null>;
  restore(path: string): Promise<number>;
  // the control tables (a preset's and the global one)
  controls(index: number): Promise<ControlsData>;
  writeControls(index: number, table: CtlTable, mods: ModSettings[] | null): Promise<number>;
  globalControls(): Promise<CtlTable>;
  writeGlobalControls(table: CtlTable): Promise<number>;
  // the drums screen (MEUS RITMOS)
  drums(): Promise<DrumState>;
  drumPattern(place: number): Promise<DrumPattern>;
  pickMidi(): Promise<MidiPreview | null>;
  midiPreview(path: string, kit: string, once: boolean): Promise<MidiPreview>;
  drumImport(place: number, path: string, name: string, kit: string, once: boolean): Promise<DrumState>;
  drumRename(place: number, name: string): Promise<DrumState>;
  drumEmpty(place: number): Promise<DrumState>;
  drumPlay(place: number | null, on: boolean): Promise<DrumState>;
  // the effects screen
  library(): Promise<FxLibrary>;
  pickModules(): Promise<ModulePick[] | null>;
  installModules(paths: string[]): Promise<FxLibrary>;
  removeModules(ids: number[]): Promise<FxLibrary>;
  // the NAM screen (Clone slots)
  clones(): Promise<CloneSlot[]>;
  pickNam(): Promise<NamFile | null>;
  writeNam(slot: number, path: string, name: string, native: boolean): Promise<CloneSlot[]>;
  writeCloneFit(slot: number, path: string, name: string): Promise<CloneSlot[]>;
  // the firmware
  pickFirmware(): Promise<FirmwareFile | null>;
  updateFirmware(path: string): Promise<UpdateDone>;
  cancelUpdate(): Promise<void>;
  // user IRs
  irs(): Promise<IrSlot[]>;
  pickIr(): Promise<IrFile | null>;
  irPreview(path: string, normalize: boolean): Promise<IrFile>;
  writeIr(slot: number, path: string, name: string, normalize: boolean): Promise<IrSlot[]>;
  renameIr(slot: number, name: string): Promise<string[]>;
  // the GLOBAL screen (protocol 9)
  settings(): Promise<SettingsReply>;
  setSettings(changes: SettingChanges): Promise<SettingsReply>;
  // flash dumps (read only)
  dumpFlash(kind: 'all' | 'boot'): Promise<DumpDone | null>;
  pickDump(): Promise<DumpDone | null>;
  cancelTask(): Promise<void>;
  // the pedal screen
  pedalInfo(): Promise<PedalInfo>;
  latency(): Promise<Latency>;
  measureLatency(route: number): Promise<Latency>;
  // the Desempenho screen (protocol 11)
  perfProfile(reset: boolean, play: number): Promise<Profile>;
  pickBenchModule(): Promise<BenchModule | null>;
  benchModule(path: string): Promise<BenchModule>;
  benchStamp(path: string): Promise<number>;
  perfBench(target: BenchTarget, opts: BenchOpts): Promise<BenchResult>;
}

interface PyApi {
  prefs(): Promise<Record<string, unknown>>;
  set_prefs(changes: Record<string, unknown>): Promise<null>;
  set_language(code: string): Promise<null>;
  status(): Promise<LinkStatus>;
  retry(): Promise<null>;
  hello(): Promise<Hello>;
  rig(): Promise<Rig>;
  block(slot: number): Promise<Param[]>;
  catalog(menu: number): Promise<CatalogItem[]>;
  edit(op: number, a: number, b: number, c: number, value: number, text: string | null): Promise<EditResult>;
  presets(): Promise<PresetEntry[]>;
  load(index: number): Promise<null>;
  preset(index: number): Promise<PresetDetail>;
  rename(index: number, name: string): Promise<string>;
  move(src: number, dst: number): Promise<null>;
  copy(src: number, dst: number): Promise<null>;
  progress(): Promise<Progress | null>;
  export_preset(index: number): Promise<string | null>;
  pick_preset(): Promise<PresetFile | null>;
  import_preset(index: number, path: string): Promise<null>;
  backup(): Promise<BackupInfo | null>;
  pick_backup(): Promise<BackupInfo | null>;
  restore(path: string): Promise<number>;
  controls(index: number): Promise<ControlsData>;
  write_controls(index: number, table: CtlTable, mods: ModSettings[] | null): Promise<number>;
  global_controls(): Promise<CtlTable>;
  write_global_controls(table: CtlTable): Promise<number>;
  drums(): Promise<DrumState>;
  drum_pattern(place: number): Promise<DrumPattern>;
  pick_midi(): Promise<MidiPreview | null>;
  midi_preview(path: string, kit: string, once: boolean): Promise<MidiPreview>;
  drum_import(place: number, path: string, name: string, kit: string, once: boolean): Promise<DrumState>;
  drum_rename(place: number, name: string): Promise<DrumState>;
  drum_empty(place: number): Promise<DrumState>;
  drum_play(place: number | null, on: boolean): Promise<DrumState>;
  library(): Promise<FxLibrary>;
  pick_modules(): Promise<ModulePick[] | null>;
  install_modules(paths: string[]): Promise<FxLibrary>;
  remove_modules(ids: number[]): Promise<FxLibrary>;
  clones(): Promise<CloneSlot[]>;
  pick_nam(): Promise<NamFile | null>;
  write_nam(slot: number, path: string, name: string, native: boolean): Promise<CloneSlot[]>;
  write_clone_fit(slot: number, path: string, name: string): Promise<CloneSlot[]>;
  pick_firmware(): Promise<FirmwareFile | null>;
  update_firmware(path: string): Promise<UpdateDone>;
  cancel_update(): Promise<null>;
  irs(): Promise<IrSlot[]>;
  pick_ir(): Promise<IrFile | null>;
  ir_preview(path: string, normalize: boolean): Promise<IrFile>;
  write_ir(slot: number, path: string, name: string, normalize: boolean): Promise<IrSlot[]>;
  rename_ir(slot: number, name: string): Promise<string[]>;
  settings(): Promise<SettingsReply>;
  set_settings(changes: SettingChanges): Promise<SettingsReply>;
  dump_flash(kind: string): Promise<DumpDone | null>;
  pick_dump(): Promise<DumpDone | null>;
  cancel_task(): Promise<null>;
  pedal_info(): Promise<PedalInfo>;
  latency(): Promise<Latency>;
  measure_latency(route: number): Promise<Latency>;
  perf_profile(reset: boolean, play: number): Promise<Profile>;
  pick_bench_module(): Promise<BenchModule | null>;
  bench_module(path: string): Promise<BenchModule>;
  bench_stamp(path: string): Promise<number>;
  perf_bench(target: BenchTarget, opts: BenchOpts): Promise<BenchResult>;
}

declare global {
  interface Window {
    pywebview?: { api: PyApi };
  }
}

/** pywebview injects window.pywebview.api a moment after the page loads */
function waitForPywebview(ms: number): Promise<PyApi | null> {
  return new Promise((resolve) => {
    if (window.pywebview?.api) return resolve(window.pywebview.api);
    const done = () => resolve(window.pywebview?.api ?? null);
    window.addEventListener('pywebviewready', done, { once: true });
    setTimeout(done, ms);
  });
}

class PyBackend implements Backend {
  constructor(private api: PyApi) {}
  prefs() { return this.api.prefs(); }
  async setPrefs(changes: Record<string, unknown>) { await this.api.set_prefs(changes); }
  async setLanguage(code: string) { await this.api.set_language(code); }
  status() { return this.api.status(); }
  async retry() { await this.api.retry(); }
  hello() { return this.api.hello(); }
  rig() { return this.api.rig(); }
  block(slot: number) { return this.api.block(slot); }
  catalog(menu: number) { return this.api.catalog(menu); }
  edit(op: number, a = 0, b = 0, c = 0, value = 0, text?: string) { return this.api.edit(op, a, b, c, value, text ?? null); }
  presets() { return this.api.presets(); }
  async load(index: number) { await this.api.load(index); }
  preset(index: number) { return this.api.preset(index); }
  rename(index: number, name: string) { return this.api.rename(index, name); }
  async move(src: number, dst: number) { await this.api.move(src, dst); }
  async copy(src: number, dst: number) { await this.api.copy(src, dst); }
  progress() { return this.api.progress(); }
  exportPreset(index: number) { return this.api.export_preset(index); }
  pickPreset() { return this.api.pick_preset(); }
  async importPreset(index: number, path: string) { await this.api.import_preset(index, path); }
  backup() { return this.api.backup(); }
  pickBackup() { return this.api.pick_backup(); }
  restore(path: string) { return this.api.restore(path); }
  controls(index: number) { return this.api.controls(index); }
  writeControls(index: number, table: CtlTable, mods: ModSettings[] | null) { return this.api.write_controls(index, table, mods); }
  globalControls() { return this.api.global_controls(); }
  writeGlobalControls(table: CtlTable) { return this.api.write_global_controls(table); }
  drums() { return this.api.drums(); }
  drumPattern(place: number) { return this.api.drum_pattern(place); }
  pickMidi() { return this.api.pick_midi(); }
  midiPreview(path: string, kit: string, once: boolean) { return this.api.midi_preview(path, kit, once); }
  drumImport(place: number, path: string, name: string, kit: string, once: boolean) {
    return this.api.drum_import(place, path, name, kit, once);
  }
  drumRename(place: number, name: string) { return this.api.drum_rename(place, name); }
  drumEmpty(place: number) { return this.api.drum_empty(place); }
  drumPlay(place: number | null, on: boolean) { return this.api.drum_play(place, on); }
  library() { return this.api.library(); }
  pickModules() { return this.api.pick_modules(); }
  installModules(paths: string[]) { return this.api.install_modules(paths); }
  removeModules(ids: number[]) { return this.api.remove_modules(ids); }
  clones() { return this.api.clones(); }
  pickNam() { return this.api.pick_nam(); }
  writeNam(slot: number, path: string, name: string, native: boolean) { return this.api.write_nam(slot, path, name, native); }
  writeCloneFit(slot: number, path: string, name: string) { return this.api.write_clone_fit(slot, path, name); }
  pickFirmware() { return this.api.pick_firmware(); }
  updateFirmware(path: string) { return this.api.update_firmware(path); }
  async cancelUpdate() { await this.api.cancel_update(); }
  irs() { return this.api.irs(); }
  pickIr() { return this.api.pick_ir(); }
  irPreview(path: string, normalize: boolean) { return this.api.ir_preview(path, normalize); }
  writeIr(slot: number, path: string, name: string, normalize: boolean) { return this.api.write_ir(slot, path, name, normalize); }
  renameIr(slot: number, name: string) { return this.api.rename_ir(slot, name); }
  settings() { return this.api.settings(); }
  setSettings(changes: SettingChanges) { return this.api.set_settings(changes); }
  dumpFlash(kind: 'all' | 'boot') { return this.api.dump_flash(kind); }
  pickDump() { return this.api.pick_dump(); }
  async cancelTask() { await this.api.cancel_task(); }
  pedalInfo() { return this.api.pedal_info(); }
  latency() { return this.api.latency(); }
  measureLatency(route: number) { return this.api.measure_latency(route); }
  perfProfile(reset: boolean, play: number) { return this.api.perf_profile(reset, play); }
  pickBenchModule() { return this.api.pick_bench_module(); }
  benchModule(path: string) { return this.api.bench_module(path); }
  benchStamp(path: string) { return this.api.bench_stamp(path); }
  perfBench(target: BenchTarget, opts: BenchOpts) { return this.api.perf_bench(target, opts); }
}

export async function connectBackend(): Promise<Backend> {
  const py = await waitForPywebview(4000);
  if (py) return new PyBackend(py);
  const { DemoBackend } = await import('./demo');
  return new DemoBackend();
}

/** a message for the user out of whatever a call threw */
export function errorText(e: unknown): string {
  if (e instanceof Error) return e.message;
  if (typeof e === 'object' && e && 'message' in e) return String((e as { message: unknown }).message);
  return String(e);
}
