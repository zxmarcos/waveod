// Demonstration backend: the interface without a pedal (npm run dev in a browser). Its data were captured from
// the emulated pedal with the demo presets (demo.json); edits change that copy the way the pedal would,
// roughly: values are clamped and stepped, texts keep the pedal's unit.

import type { Backend } from './api';
import { stepped, valueText } from './format';
import { EDIT } from './types';
import type {
  CloneSlot, ControlsData, CtlBlock, CtlParam, CtlTable, DumpDone, FirmwareFile, FlashArea, IrFile, IrSlot, SettingChanges, SettingsReply, ModSettings, NamFile, UpdateDone,
  BackupInfo, Block, CatalogItem, DrumPattern, DrumState, EditResult, FxLibrary, FxModule, Hello, Latency, LinkStatus, MidiPreview, ModulePick, Param, PedalInfo, PresetDetail,
  PresetEntry, PresetFile, Progress, Rig, Profile, BenchModule, BenchTarget, BenchOpts, BenchResult,
} from './types';
import data from './demo.json';

interface Fixture {
  info: { version: string; protocol: number };
  presets: PresetEntry[];
  catalog: Record<string, CatalogItem[]>;
  rigs: Record<string, Rig & { params: Record<string, Param[]> }>;
  details?: Record<string, PresetDetail>;
  drums?: { state: DrumState; patterns: Record<string, DrumPattern>; previews: Record<string, MidiPreview> };
  fx?: { library: FxLibrary; picks: ModulePick[] };
  nam?: { slots: CloneSlot[]; picks: NamFile[] };
  firmware?: FirmwareFile;
  flash?: FlashArea[];
  settings?: SettingsReply;
  irs?: { names: string[]; slots: ({ name: string; samples: number[] } | null)[]; pick: IrFile };
  blockModels?: Record<string, Param[]>;   // "menu:id": the params of models no captured preset has
  ctl?: {
    models: Record<string, CtlParam[]>;
    presets: Record<string, Omit<ControlsData, 'blocks' | 'mods' | 'global'> & {
      blocks: (Omit<CtlBlock, 'params'> & { model: string })[]; mods: (ModSettings | null)[] | null }>;
    global: CtlTable;
  };
}

/** the other kit's kick and snare (drums.py KITS: acoustic 1 / 5, electronic 0 / 3) */
const KIT_SWAP: Record<string, Record<number, number>> = { eletronico: { 1: 0, 5: 3 }, acustico: { 0: 1, 3: 5 } };

/** a place of the demonstration's preset list: which captured preset it holds (-1 none) under what name */
interface Place { src: number; name: string; place: number; origin: number }

const NO_FILES = 'Arquivos só no app (waveod-studio-web.bat): a demonstração no navegador não grava nada';

const FIX = data as unknown as Fixture;
const clone = <T>(x: T): T => JSON.parse(JSON.stringify(x)) as T;
const delay = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** the parameters of a model, from any captured preset that has it */
function paramsOf(menu: number, id: number): Param[] {
  for (const r of Object.values(FIX.rigs))
    for (const b of r.blocks)
      if (b.menu === menu && b.id === id && r.params[b.slot]) return clone(r.params[b.slot]);
  const model = FIX.blockModels?.[`${menu}:${id}`];
  if (model) return clone(model);
  return [
    { k: 0, knob: 0, synced: false, scene: false, min: 0, max: 100, step: 1, default: 50, value: 50, name: 'Level',
      text: '50', options: [] },
  ];
}

export class DemoBackend implements Backend {
  private index = 0;
  private rigs = new Map<number, Rig & { params: Record<string, Param[]> }>();
  private gen = 1;
  private places: Place[] = FIX.presets.map((p) => ({
    src: p.place === 2 || p.origin === 2 ? -1 : p.index, name: p.name, place: p.place, origin: p.origin,
  }));

  private cur() {
    let r = this.rigs.get(this.index);
    if (!r) {
      const at = this.places[this.index];
      const src = at.src >= 0 ? FIX.rigs[String(at.src)] : undefined;
      r = src ? { ...clone(src), name: at.name } : this.empty(this.index);
      this.rigs.set(this.index, r);
    }
    return r;
  }

  private empty(index: number) {
    const e = this.places[index];
    return {
      gen: 0, loads: 1, index, dirty: false, volume: 50, tempo: 120, name: e?.name || '', order: [], blocks: [],
      route: { mode: 0, split: 1, merge: 4, joints: [50, 100, 100, 50, 50, 50] },
      scene: { active: 0, editing: false, names: ['CENA 1', 'CENA 2', 'CENA 3', 'CENA 4'] }, params: {},
    };
  }

  // ?link=searching|busy|silent|old|failed shows that phase for good; ?link=lost connects, loses the pedal after
  // 4 s and finds it again 6 s later (design reviews, screenshots)
  private link = new URLSearchParams(location.search).get('link');
  private born = Date.now();
  private tries = 0;

  async status(): Promise<LinkStatus> {
    const base = { transport: 'demonstração', attempts: ++this.tries, name: 'Matribox II Pro', version: FIX.info.version,
                   protocol: FIX.info.protocol, message: '', connections: 1 };
    const t = (Date.now() - this.born) / 1000;
    const fixed: Record<string, Partial<LinkStatus>> = {
      searching: { phase: 'searching', message: 'Nenhum pedal conectado.', name: '' },
      busy: { phase: 'busy', message: 'A porta MIDI do pedal está aberta em outro programa.' },
      silent: { phase: 'silent', message: 'O pedal está conectado mas não respondeu.' },
      old: { phase: 'old', message: 'O waveod do pedal (protocolo 6) é anterior a esta interface (protocolo 8).',
             version: 'a41c0de', protocol: 6 },
      failed: { phase: 'failed', message: 'O emulador não iniciou.' },
    };
    if (this.link && fixed[this.link]) return { ...base, ...fixed[this.link] } as LinkStatus;
    if (this.updating) return { ...base, phase: 'updating', message: 'Gravando o firmware.', name: '' };
    if (this.afterUpdate && Date.now() < this.afterUpdate)
      return { ...base, phase: 'searching', message: 'O pedal está reiniciando com o firmware novo.', name: '' };
    if (this.afterUpdate) return { ...base, phase: 'connected', connections: 2 + this.updates };
    if (this.link === 'lost' && t > 4 && t < 10)
      return { ...base, phase: 'searching', message: 'O pedal foi desconectado.', name: '' };
    return { ...base, phase: 'connected', connections: this.link === 'lost' && t >= 10 ? 2 : 1 };
  }

  async retry() { this.tries = 0; }

  async hello(): Promise<Hello> {
    return { connected: true, transport: 'demonstração', name: 'demonstração', version: FIX.info.version,
             protocol: FIX.info.protocol };
  }

  async rig(): Promise<Rig> {
    await delay(15);
    const r = this.cur();
    const { params: _params, ...rest } = r;
    return clone({ ...rest, gen: this.gen, index: this.index });
  }

  async block(slot: number): Promise<Param[]> {
    await delay(20);
    return clone(this.cur().params[slot] ?? []);
  }

  async catalog(menu: number): Promise<CatalogItem[]> {
    return clone(FIX.catalog[String(menu)] ?? []);
  }

  async presets(): Promise<PresetEntry[]> {
    await delay(20);
    return this.places.map((p, index) => ({ index, origin: p.origin, place: p.place, name: p.name,
                                           active: index === this.index }));
  }

  // ---------------------------------------------------------------- the presets screen
  async preset(index: number): Promise<PresetDetail> {
    await delay(30);
    const at = this.places[index];
    const d = at && at.src >= 0 ? FIX.details?.[String(at.src)] : undefined;
    if (d) return { ...clone(d), index, name: at.name, origin: at.origin };
    return {
      index, origin: 2, name: '', style: '', volume: 50, bpm: 120, bytes: 0, route: { mode: 0, split: 1, merge: 4 },
      blocks: [], scenes: { own: false, active: 0, names: ['', '', '', ''], on: [[], [], [], []], params: 0 }, mods: 0,
      controls: false,
    };
  }

  async rename(index: number, name: string) {
    await delay(60);
    const n = name.trim().slice(0, 17).trimEnd();
    this.places[index].name = n;
    if (this.places[index].src < 0) Object.assign(this.places[index], { place: 0, origin: 0 });
    this.rigs.delete(index);
    return n;
  }

  /** out of src, into dst: the ones between slide one place, the active preset stays the same one (wv_msgs.c) */
  async move(src: number, dst: number) {
    await delay(80);
    const [p] = this.places.splice(src, 1);
    this.places.splice(dst, 0, p);
    const keep = new Map([...this.rigs].map(([i, r]) => [this.slid(i, src, dst), r]));
    this.rigs = keep;
    this.index = this.slid(this.index, src, dst);
    this.gen++;
  }

  private slid(i: number, src: number, dst: number) {
    if (i === src) return dst;
    if (src < i && i <= dst) return i - 1;
    if (dst <= i && i < src) return i + 1;
    return i;
  }

  async copy(src: number, dst: number) {
    await delay(80);
    this.places[dst] = { ...this.places[src], place: 0, origin: 0 };
    this.rigs.delete(dst);
    if (dst === this.index) this.gen++;
  }

  // ---------------------------------------------------------------- the drums screen
  private drumState: DrumState = clone(FIX.drums?.state ?? {
    places: 20, sounds: 13, max_events: 2000, playing_place: null, playing: false, slots: Array(20).fill(null),
  });
  private patterns = new Map<number, DrumPattern>(
    Object.entries(FIX.drums?.patterns ?? {}).map(([k, p]) => [Number(k), clone(p)]));
  private picks = 0;

  async drums() { await delay(20); return clone(this.drumState); }

  async drumPattern(place: number) {
    await delay(40);
    const p = this.patterns.get(place);
    if (!p) throw new Error(`o lugar M${place + 1} está vazio`);
    return clone(p);
  }

  /** the demonstration's MIDI files, one after another */
  async pickMidi() {
    const all = Object.values(FIX.drums?.previews ?? {});
    if (!all.length) throw new Error(NO_FILES);
    return clone(all[this.picks++ % all.length]);
  }

  async midiPreview(path: string, kit: string, once: boolean): Promise<MidiPreview> {
    await delay(60);
    const src = Object.values(FIX.drums?.previews ?? {}).find((p) => p.path === path);
    if (!src) throw new Error('arquivo desconhecido');
    const swap = KIT_SWAP[kit] ?? {};
    return { ...clone(src), kit: kit as MidiPreview['kit'], once,
             hits: src.hits.map(([t, s, v]) => [t, swap[s] ?? s, v] as [number, number, number]) };
  }

  async drumImport(place: number, path: string, name: string, kit: string, once: boolean) {
    const p = await this.midiPreview(path, kit, once);
    const n = name.trim().toUpperCase().slice(0, 16) || 'MIDI';
    this.patterns.set(place, { hits: p.hits, length: p.length, once, name: n, bpm: p.bpm, events: p.events });
    this.drumState.slots[place] = { place, once, bpm: p.bpm, events: p.events, name: n };
    return this.drums();
  }

  async drumRename(place: number, name: string) {
    const s = this.drumState.slots[place], p = this.patterns.get(place);
    const n = name.trim().toUpperCase().slice(0, 16) || 'MIDI';
    if (s) s.name = n;
    if (p) p.name = n;
    return this.drums();
  }

  async drumEmpty(place: number) {
    this.drumState.slots[place] = null;
    this.patterns.delete(place);
    if (this.drumState.playing_place === place) Object.assign(this.drumState, { playing_place: null, playing: false });
    return this.drums();
  }

  async drumPlay(place: number | null, on: boolean) {
    if (place !== null && !this.drumState.slots[place]) throw new Error(`o ritmo ${place + 1} está vazio`);
    this.drumState.playing_place = place;
    this.drumState.playing = on;
    return this.drums();
  }

  // ---------------------------------------------------------------- the effects screen
  private lib: FxLibrary = clone(FIX.fx?.library ?? { capacity: 1060864, used: 0, modules: [] });

  private async slowly(label: string, kb: number) {
    for (let k = 0; k <= kb; k += Math.max(1, Math.ceil(kb / 12))) {
      this.busy = { label, done: k, total: kb, unit: 'KB' };
      await delay(70);
    }
    this.busy = null;
  }

  async library() {
    await this.slowly('Lendo os efeitos', Math.ceil(this.lib.used / 1024));
    return clone(this.lib);
  }

  /** the demonstration's module files: three modules and one that is not */
  async pickModules(): Promise<ModulePick[] | null> { return clone(FIX.fx?.picks ?? []); }

  async installModules(paths: string[]) {
    const add = (FIX.fx?.picks ?? []).filter((p) => paths.includes(p.path) && !p.error) as unknown as FxModule[];
    for (const m of add) this.lib.modules = [...this.lib.modules.filter((x) => x.id !== m.id), clone(m)];
    this.lib.used = this.lib.modules.reduce((n, m) => n + m.size + 256, 0);
    await this.slowly('Gravando os efeitos', Math.ceil(this.lib.used / 1024));
    return clone(this.lib);
  }

  async removeModules(ids: number[]) {
    this.lib.modules = this.lib.modules.filter((m) => !ids.includes(m.id));
    this.lib.used = this.lib.modules.length ? this.lib.modules.reduce((n, m) => n + m.size + 256, 0) : 0;
    await this.slowly('Gravando os efeitos', Math.ceil(this.lib.used / 1024));
    return clone(this.lib);
  }

  // ---------------------------------------------------------------- the control tables
  private ctlWritten = new Map<number, { table: CtlTable; mods: ModSettings[] | null }>();
  private glob: CtlTable | null = FIX.ctl ? clone(FIX.ctl.global) : null;

  async controls(index: number): Promise<ControlsData> {
    await delay(160);
    const at = this.places[index];
    const src = at && at.src >= 0 ? FIX.ctl?.presets[String(at.src)] : undefined;
    if (!src) throw new Error(at && at.src >= 0 ? 'A demonstração não tem os controles deste preset.' : 'Lugar vazio.');
    const off: ModSettings = { type: 'off', shape: 'sine', sync: 0, rate: 1, steps: 8, phase: 0, glide: 0, sens: 0,
                               attack: 10, release: 200, step: [0, 14, 28, 42, 57, 71, 85, 100, 0, 14, 28, 42, 57, 71, 85, 100] };
    const w = this.ctlWritten.get(index);
    return {
      ...clone(src), index, name: at.name, own: src.own || !!w,
      blocks: src.blocks.map((b) => ({ slot: b.slot, menu: b.menu, name: b.name, params: clone(FIX.ctl!.models[b.model]) })),
      table: clone(w?.table ?? src.table),
      mods: clone(w?.mods ?? (src.mods ?? [null, null, null, null]).map((m) => m ?? off)),
      global: clone(this.glob),
    };
  }

  async writeControls(index: number, table: CtlTable, mods: ModSettings[] | null) {
    await delay(500);
    if (table.links.length > 48) throw new Error('a tabela tem mais de 48 ligações');
    const before = this.ctlWritten.get(index);
    this.ctlWritten.set(index, { table: clone(table), mods: clone(mods ?? before?.mods ?? null) });
    const at = this.places[index];
    const d = at && at.src >= 0 ? FIX.details?.[String(at.src)] : undefined;
    if (d) {
      d.controls = true;
      if (mods) d.mods = mods.filter((m) => m.type !== 'off').length;
    }
    return table.links.length;
  }

  async globalControls() {
    await delay(120);
    if (!this.glob) throw new Error('A demonstração não tem os controles globais.');
    return clone(this.glob);
  }

  async writeGlobalControls(table: CtlTable) {
    await delay(400);
    this.glob = clone({ ...table, links: table.links.filter((l) => l.action === 1 || l.action === 4 || l.action === 5) });
    return this.glob.links.length;
  }

  // ---------------------------------------------------------------- the NAM screen
  private slots: CloneSlot[] = clone(FIX.nam?.slots ?? []);
  private namPicks = 0;

  async clones() { await delay(40); return clone(this.slots); }

  async pickNam(): Promise<NamFile | null> {
    const all = FIX.nam?.picks ?? [];
    if (!all.length) throw new Error(NO_FILES);
    return clone(all[this.namPicks++ % all.length]);
  }

  async writeNam(slot: number, path: string, name: string, native: boolean) {
    await delay(400);
    const f = (FIX.nam?.picks ?? []).find((x) => x.path === path);
    const r48 = !!f?.rate48 && !native;
    this.slots[slot] = { slot, kind: r48 ? 3 : 2, kind_text: r48 ? 'NAM a 48 kHz (reamostrado)' : 'NAM a 44,1 kHz',
                         named: true, name: name.trim().slice(0, 16), sample_rate: f?.sample_rate ?? 48000,
                         loudness: f?.loudness ?? -18, crc: 0x5eed0000 + slot };
    return clone(this.slots);
  }

  async writeCloneFit(slot: number, _path: string, name: string) {
    await delay(1200);
    this.slots[slot] = { slot, kind: 1, kind_text: 'captura do Clone original', named: true, name: name.trim().slice(0, 16) };
    return clone(this.slots);
  }

  // ---------------------------------------------------------------- the firmware
  private updating = false;
  private afterUpdate = 0;
  private updates = 0;
  private cancelled = false;

  async pickFirmware() {
    if (!FIX.firmware) throw new Error(NO_FILES);
    return clone(FIX.firmware);
  }

  /** a firmware update played out: the reboot, the four sections in packets, the pedal back (or cancelled) */
  async updateFirmware(path: string): Promise<UpdateDone> {
    this.updating = true;
    this.cancelled = false;
    const t0 = Date.now();
    try {
      this.busy = { label: 'Reiniciando o pedal no modo de atualização', done: 0, total: 0, unit: 'pacotes', cancel: true };
      await delay(1200);
      const secs = FIX.firmware?.sections ?? [];
      const total = secs.reduce((n, s) => n + Math.ceil(s.size / 180), 0);
      let done = 0;
      for (const s of secs) {
        const n = Math.ceil(s.size / 180);
        for (let k = 0; k < n; k += 1200) {
          if (this.cancelled) throw new Error('cancelado');
          done = Math.min(total, done + Math.min(1200, n - k));
          this.busy = { label: `Gravando: ${s.name}`, done, total, unit: 'pacotes', cancel: true };
          await delay(90);
        }
      }
      this.updates++;
      return { seconds: (Date.now() - t0) / 1000, version: FIX.firmware?.version ?? '', log: 'C:/.../update.log' };
    } finally {
      this.busy = null;
      this.updating = false;
      this.afterUpdate = Date.now() + 3000;
      void path;
    }
  }

  async cancelUpdate() { this.cancelled = true; }

  // ---------------------------------------------------------------- user IRs
  private irSlots: IrSlot[] = (FIX.irs?.slots ?? []).map((x, i) => ({
    slot: i, name: x?.name ?? FIX.irs!.names[i], used: !!x, samples: x ? [...x.samples] : null }));

  async irs() { await delay(300); return clone(this.irSlots); }

  async pickIr(): Promise<IrFile | null> {
    if (!FIX.irs) throw new Error(NO_FILES);
    return this.irPreview(FIX.irs.pick.path, false);          // as webapp.pick_ir: at the file's own level
  }

  /** the demo's .wav at the factory cabs' level: its samples scaled (the conversion itself is irs.py's) */
  async irPreview(_path: string, normalize: boolean): Promise<IrFile> {
    const f = clone(FIX.irs!.pick);
    if (normalize === f.normalized) return f;
    const peak = Math.max(...f.samples.map(Math.abs));
    const g = normalize ? 0.988 / peak : 1 / Math.pow(10, f.gain_db / 20);
    return { ...f, normalized: normalize, gain_db: normalize ? 20 * Math.log10(g) : 0, samples: f.samples.map((v) => v * g) };
  }

  async writeIr(slot: number, path: string, name: string, normalize: boolean) {
    await delay(500);
    const f = await this.irPreview(path, normalize);
    this.irSlots[slot] = { slot, name: name.trim().slice(0, 16), used: true, samples: f.samples };
    return clone(this.irSlots);
  }

  async renameIr(slot: number, name: string) {
    await delay(100);
    this.irSlots[slot].name = name.trim().slice(0, 16);
    return this.irSlots.map((x) => x.name);
  }

  // ---------------------------------------------------------------- the GLOBAL screen (protocol 9)
  private sets: SettingsReply | null = FIX.settings ? clone(FIX.settings) : null;
  private tmax: number | null = null;
  private tmin: number | null = null;

  async settings() {
    await delay(120);
    if (!this.sets) throw new Error('A demonstração não tem os ajustes globais.');
    // SISTEMA: a CPU warming slowly around 47 °C (model/sysmon.h; read only)
    const t = Math.round((46.5 + 1.2 * Math.sin(Date.now() / 40000) + 0.3 * Math.sin(Date.now() / 7000)) * 10) / 10;
    this.tmax = Math.max(this.tmax ?? t, t);
    this.tmin = Math.min(this.tmin ?? 41.8, t);
    const ro = (value: number) => ({ value, min: -40, max: 125, step: 0, ro: true });
    return { ...clone(this.sets), cpu_temp: ro(t), cpu_temp_max: ro(this.tmax), cpu_temp_min: ro(this.tmin) };
  }

  /** wv_settings_msgs.c's rules: in order, the first refused one stops (the ones before it stay) */
  async setSettings(changes: SettingChanges) {
    await delay(40);
    const s = this.sets!;
    for (const [name, v] of Object.entries(changes)) {
      if (name === 'eq') {
        for (const [k, x] of Object.entries(v as Record<number, number>)) {
          const e = s.eq[Number(k)];
          if (e) e.value = Math.min(e.max, Math.max(e.min, x));
        }
        continue;
      }
      const e = s[name] as SettingsReply['eq'][number] | undefined;
      const x = v as number;
      if (!e) throw new Error(`o pedal recusou ${name}: ajuste desconhecido`);
      if (e.ro) throw new Error(`o pedal recusou ${name}: só se ajusta no pedal`);
      if (x < e.min - 0.5 || x > e.max + 0.5 || (name === 'tempo' && x > 0 && x < 40))
        throw new Error(`o pedal recusou ${name}: valor fora do que o pedal aceita`);
      e.value = Math.round(x);
    }
    return clone(s);
  }

  // ---------------------------------------------------------------- flash dumps
  private dumpCancelled = false;

  /** a dump played out: KB by KB (fast), cancellable; the 8 MB summary from a real dump */
  async dumpFlash(kind: 'all' | 'boot'): Promise<DumpDone | null> {
    const total = kind === 'all' ? 8192 : 128;
    const label = kind === 'all' ? 'Copiando a flash inteira' : 'Copiando o bootloader';
    this.dumpCancelled = false;
    const t0 = Date.now();
    try {
      for (let done = 0; done <= total; done += kind === 'all' ? 128 : 8) {
        if (this.dumpCancelled) throw new Error('Cópia cancelada: nenhum arquivo foi gravado.');
        this.busy = { label, done, total, unit: 'KB', cancel: true, task: 'dump' };
        await delay(30);
      }
    } finally {
      this.busy = null;
    }
    return { file: kind === 'all' ? 'flash_full.bin' : 'bootloader.bin', size: total * 1024, seconds: (Date.now() - t0) / 1000,
             summary: kind === 'all' ? clone(FIX.flash ?? []) : null };
  }

  async pickDump(): Promise<DumpDone | null> {
    if (!FIX.flash) throw new Error(NO_FILES);
    return { file: 'flash_full.bin', size: 8 << 20, seconds: 0, summary: clone(FIX.flash) };
  }

  async cancelTask() { this.dumpCancelled = true; }

  // ---------------------------------------------------------------- the pedal screen
  async pedalInfo(): Promise<PedalInfo> {
    await delay(30);
    const own = this.places.filter((p) => p.place === 0 && p.origin !== 2).length;
    const original = this.places.filter((p) => p.place === 1 && p.origin !== 2).length;
    const i = FIX.info as unknown as PedalInfo;
    return { ...clone(i), name: 'Matribox II Pro', active: this.index, modified: !!this.rigs.get(this.index)?.dirty,
             own, original, vacant: 240 - own - original };
  }

  private lastLatency: Latency = { status: 0, status_text: 'sem medição', route: 0, runs: 0, total: 8, level_db: 0,
                                   min: 0, max: 0, mean: 0, match: 0, buffers: 128, rate: 44100, ms: 0 };
  private busy: Progress | null = null;

  async latency() { return clone(this.lastLatency); }

  /** what the real pedal measured (2026-11-21): 3.36 ms on a cable, +0.64 ms through the 01A preset */
  async measureLatency(route: number): Promise<Latency> {
    for (let k = 0; k <= 8; k++) {
      this.busy = { label: 'Medindo a latência', done: k, total: 8, unit: 'medidas' };
      await delay(220);
    }
    this.busy = null;
    const mean = route === 3 ? 28.2 : 148.2;
    this.lastLatency = { status: 2, status_text: 'ok', route, runs: 8, total: 8, level_db: route === 3 ? 0 : -18,
                         min: mean - 0.1, max: mean + 0.1, mean, match: 0.99, buffers: 128, rate: 44100,
                         ms: (mean * 1000) / 44100 };
    return clone(this.lastLatency);
  }

  // the Desempenho screen: made-up cycles of the size the pedal measures (amps ~100-200k, drives ~30k ...)
  private static COST = [9000, 26000, 14000, 34000, 150000, 52000, 61000, 12000, 30000, 24000, 70000, 125000, 6000, 3000, 3000, 2500];
  private jitter(seed: number) { const x = Math.sin(seed * 12.9898 + this.gen * 0.37) * 43758.5453; return x - Math.floor(x); }

  async perfProfile(): Promise<Profile> {
    await delay(40);
    const r = this.cur();
    const budget = (600e6 * 64) / 44100;
    const slots = r.order.map((s, k) => {
      const b = r.blocks.find((x) => x.slot === s)!;
      const base = (DemoBackend.COST[b.menu] ?? 20000) * (0.8 + 0.4 * ((b.id >>> 3) % 7) / 7) * (b.on ? 1 : 0.04);
      const mean = Math.round(base * (0.97 + 0.06 * this.jitter(k)));
      return { slot: s, menu: b.menu, id: b.id, blocks: 689, mean, max: Math.round(mean * (1.08 + 0.2 * this.jitter(k + 9))),
               last: Math.round(mean * (0.95 + 0.1 * this.jitter(k + 3))) };
    });
    const chain = slots.reduce((a, s) => a + s.mean, 0) + 1400 + 260 * slots.length;
    const sections = [3200, 900, 200, 1100, 600, chain, 400, 700, 0, 700, 300, 1800, 800, 2900, 9000];
    const total = sections.reduce((a, c) => a + c, 0);
    this.gen++;
    return { core_hz: 600e6, budget, block_ms: (total / 600e6) * 1000, worst_ms: (total * 1.12 / 600e6) * 1000, blocks: 689,
             slots, sections };
  }

  async pickBenchModule(): Promise<BenchModule | null> { throw new Error(NO_FILES); }
  async benchModule(): Promise<BenchModule> { throw new Error(NO_FILES); }
  async benchStamp(): Promise<number> { return 0; }

  async perfBench(target: BenchTarget, opts: BenchOpts): Promise<BenchResult> {
    if (target.kind !== 'catalog') throw new Error(NO_FILES);
    await delay(Math.min(2500, 300 + opts.blocks));
    const it = FIX.catalog[String(target.menu)]?.find((i) => i.index === target.index);
    const seed = (it?.id ?? target.menu * 977 + target.index) >>> 0;
    const base = (DemoBackend.COST[target.menu] ?? 20000) * (0.7 + 0.6 * ((seed >>> 2) % 11) / 11);
    const scale = opts.bypass ? 0.03 : opts.cold ? 1.35 : 1;
    const cycles = Array.from({ length: opts.blocks }, (_, k) =>
      Math.round(base * scale * (0.98 + 0.04 * this.jitter(k)) + (k % 97 === 0 ? base * 0.25 : 0)));
    const sorted = [...cycles].sort((a, b) => a - b);
    const at = (q: number) => sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
    const crc = ((seed * 2654435761) >>> 0) ^ (opts.bypass ? 0x5bd1e995 : 0);
    return {
      status: 0, status_text: 'ok', load: Math.round(base * 0.6 + 40000), overhead: 12, blocks: opts.blocks,
      min: sorted[0], median: at(0.5), p99: at(0.99), max: sorted[sorted.length - 1],
      mean: cycles.reduce((a, c) => a + c, 0) / cycles.length, crc: (crc >>> 0).toString(16).padStart(8, '0'), peak: 0.41,
      pools: { code: 9000 + (seed % 9000), const: 2000 + (seed % 3000), state: 1500 + (seed % 4000), buffer: (seed % 3) * 32768 },
      core_hz: 600e6, budget: (600e6 * 64) / 44100, cold: opts.cold, bypass: opts.bypass, cycles, menu: target.menu, emu: false,
    };
  }

  async progress(): Promise<Progress | null> { return this.busy; }
  async exportPreset(): Promise<string | null> { throw new Error(NO_FILES); }
  async pickPreset(): Promise<PresetFile | null> { throw new Error(NO_FILES); }
  async importPreset() { throw new Error(NO_FILES); }
  async backup(): Promise<BackupInfo | null> { throw new Error(NO_FILES); }
  async pickBackup(): Promise<BackupInfo | null> { throw new Error(NO_FILES); }
  async restore(): Promise<number> { throw new Error(NO_FILES); }

  async load(index: number) {
    await delay(60);
    this.rigs.delete(index);
    this.index = index;
    this.gen++;
  }

  private order(r: Rig) {
    const path: number[] = [];
    const { mode, split, merge } = r.route;
    const end = mode === 2 ? 6 : merge;
    if (mode === 0) for (let c = 0; c < 12; c++) path.push(c);
    else {
      for (let c = 0; c < end; c++) path.push(c);
      for (let c = 6; c < 12; c++) path.push(c);
      for (let c = end; c < 6; c++) path.push(c);
    }
    r.order = path.map((c) => r.blocks.find((b) => b.cell === c)?.slot ?? -1).filter((s) => s >= 0);
  }

  async edit(op: number, a = 0, b = 0, c = 0, value = 0, text?: string): Promise<EditResult> {
    await delay(25);
    const r = this.cur();
    const blk = (s: number) => r.blocks.find((x) => x.slot === s);
    let result = 0;
    let param: Param | undefined;
    const item = (menu: number, index: number) => FIX.catalog[String(menu)]?.find((i) => i.index === index);
    switch (op) {
      case EDIT.PARAM: {
        const p = r.params[a]?.find((x) => x.k === b);
        if (!p) { result = -21; break; }
        p.value = stepped(p, value);
        p.text = valueText(p, p.value);
        param = clone(p);
        break;
      }
      case EDIT.ON: { const x = blk(a); if (x) x.on = b !== 0; else result = -21; break; }
      case EDIT.PLACE: {
        const it = item(b, c);
        const used = new Set(r.blocks.map((x) => x.slot));
        const slot = [...Array(12).keys()].find((s) => !used.has(s));
        if (!it || slot === undefined || r.blocks.some((x) => x.cell === a)) { result = -1; break; }
        const nb: Block = { slot, on: true, menu: b, index: c, id: it.id, cell: a, kind: it.kind, params: 0,
                            name: it.name, type: it.type };
        r.params[slot] = paramsOf(b, it.id);
        nb.params = r.params[slot].length;
        r.blocks.push(nb);
        result = slot;
        break;
      }
      case EDIT.MOVE: {
        const x = blk(a);
        if (!x || x.cell === b) { result = -1; break; }
        const other = r.blocks.find((y) => y.cell === b);
        if (other) other.cell = x.cell;
        x.cell = b;
        break;
      }
      case EDIT.REMOVE: { r.blocks = r.blocks.filter((x) => x.slot !== a); delete r.params[a]; break; }
      case EDIT.REPLACE: {
        const x = blk(a), it = item(b, c);
        if (!x || !it) { result = -1; break; }
        Object.assign(x, { menu: b, index: c, id: it.id, kind: it.kind, name: it.name, type: it.type });
        r.params[a] = paramsOf(b, it.id);
        x.params = r.params[a].length;
        break;
      }
      case EDIT.ROUTE:
        r.route = { ...r.route, mode: a, split: Math.max(0, Math.min(5, b)), merge: Math.max(b + 1, Math.min(6, c)) };
        result = 1;
        break;
      case EDIT.JOINT: r.route.joints[a] = Math.max(0, Math.min(100, b)); break;
      case EDIT.SCENE: r.scene.active = a; break;
      // the scene menu (protocol 10), roughly: the demonstration keeps names and the scene parameter flags
      case EDIT.SCENE_PICK: r.scene.active = a; r.scene.editing = true; break;
      case EDIT.SCENE_COPY: result = a === b ? -21 : 0; break;
      case EDIT.SCENE_REVERT: r.scene.names[a] = `CENA ${a + 1}`; break;
      case EDIT.SCENE_NAME: r.scene.names[a] = (text ?? '').trim().toUpperCase().slice(0, 10) || `CENA ${a + 1}`; break;
      case EDIT.SCENE_PARAM: {
        const p = r.params[a]?.find((x) => x.k === b);
        if (!p) { result = -21; break; }
        p.scene = !p.scene;
        result = p.scene ? 1 : 0;
        break;
      }
      case EDIT.VOLUME: r.volume = a; break;
      case EDIT.BPM: r.tempo = a; break;
      case EDIT.NAME: r.name = (text ?? '').slice(0, 17).trimEnd(); break;
      case EDIT.SAVE: {
        if (a < 0 || a >= 240) { result = -21; break; }
        const was = this.places[this.index];
        this.places[a] = { ...was, name: r.name, place: 0, origin: 0 };
        r.dirty = false;
        if (a !== this.index) {
          this.rigs.delete(this.index);         // the old place keeps what it had stored
          this.places[this.index] = was;
          r.index = a;
          this.rigs.set(a, r);
          this.index = a;
        }
        this.gen++;
        return { result: 0, gen: this.gen };
      }
      default: result = -21;
    }
    if (op >= EDIT.SCENE_COPY && op <= EDIT.SCENE_PARAM && result >= 0) r.dirty = true;
    this.order(r);
    if (result >= 0 && op !== EDIT.SCENE) r.dirty = true;
    this.gen++;
    return { result, gen: this.gen, param };
  }
}
