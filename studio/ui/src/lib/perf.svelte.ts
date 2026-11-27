// The Desempenho screen's state (protocol 11, waveod_studio/bench.py, docs/bench.md): the preset playing timed slot
// by slot in the audio interrupt (the profile, read live), and effects measured on their own (the bench), kept as
// runs to compare: cycles against a reference run, the CRC of the output telling whether two runs computed the same.

import { errorText } from './api';
import { t, type MsgKey } from './i18n.svelte';
import { studio } from './store.svelte';
import { CHAIN_SECTION } from './types';
import type { BenchModule, BenchOpts, BenchResult, BenchTarget, Profile } from './types';

export interface Run {
  id: number;
  label: string;        // the model's name, or the module file's
  detail: string;       // where it came from: the menu and the kind, or the file
  target: BenchTarget;
  result: BenchResult;
  at: number;           // Date.now()
}

/** what one profile reading says about the block: the slots, the firmware around them, the room left */
export function blockUse(p: Profile) {
  const slots = p.slots.reduce((a, s) => a + s.mean, 0);
  const whole = p.sections.length ? p.sections.reduce((a, c) => a + c, 0) : slots;
  const between = p.sections.length > CHAIN_SECTION ? Math.max(0, p.sections[CHAIN_SECTION] - slots) : 0;
  const budget = p.budget || (600e6 * 64) / 44100;
  return { slots, firmware: Math.max(0, whole - slots), between, whole, budget, worst: (p.worst_ms / 1.4512) * budget };
}

/** the name of a section of the audio interrupt (types.ts BENCH_SECTIONS's order), in the language now */
const SECTIONS: MsgKey[] = ['perf.sec_convert', 'perf.sec_in_filter', 'perf.sec_usb_play', 'perf.sec_buffers', 'perf.sec_before',
  'perf.sec_chain', 'perf.sec_after', 'perf.sec_level', 'perf.sec_eq', 'perf.sec_drums', 'perf.sec_aux', 'perf.sec_out_filter',
  'perf.sec_send_filter', 'perf.sec_dac', 'perf.sec_latency'];
export const sectionName = (k: number) => (SECTIONS[k] ? t(SECTIONS[k]) : t('perf.section', { n: k }));

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const HISTORY = 90;

class Perf {
  profile = $state<Profile | null>(null);
  history = $state<number[]>([]);       // the whole block's share (0..1) of the last readings
  live = $state(true);
  reading = $state(false);
  private loop = 0;                      // the running live loop's ticket (a new one ends the old)

  // the bench
  source = $state<'catalog' | 'file'>('catalog');
  menu = $state(4);
  index = $state<number | null>(null);
  module = $state<BenchModule | null>(null);
  opts = $state<BenchOpts>({ blocks: 1000, skip: 50, cold: false, bypass: false });
  runs = $state<Run[]>([]);
  ref = $state<number | null>(null);    // the run the others are compared with
  shown = $state<number | null>(null);  // the run whose blocks the chart draws
  private next = 1;

  get supported() { return (studio.hello?.protocol ?? 0) >= 11 || this.demo; }
  get demo() { return studio.link?.transport === 'demonstração'; }
  get emu() { return studio.link?.transport === 'emulador'; }

  /** the screen opened: the profile read, live unless in the emulator (its time runs only when asked) */
  enter() {
    if (this.emu) this.live = false;
    if (this.supported) {
      if (this.live) void this.run();
      else void this.readOnce();
    }
    if (this.emu && this.opts.blocks > 100) this.opts = { ...this.opts, blocks: 50, skip: 10 };   // slow there
    if (this.demo && !this.runs.length && new URLSearchParams(location.search).has('bench')) void this.demoRuns();
  }

  /** the demonstration's ?bench: a few runs to look at (design reviews, screenshots) */
  private async demoRuns() {
    await studio.ensureCatalog(4);
    const amps = studio.catalogs[4] ?? [];
    for (const [k, cold] of [[0, false], [0, true], [1, false]] as const) {
      if (!amps[k]) continue;
      this.source = 'catalog';
      this.menu = 4;
      this.index = amps[k].index;
      this.opts = { ...this.opts, cold };
      await this.measure();
    }
    this.opts = { ...this.opts, cold: false };
  }

  leave() { this.loop++; }

  setLive(on: boolean) {
    this.live = on;
    if (on) void this.run();
    else this.loop++;
  }

  private async tick() {
    // reset with every read: each answer covers the time since the previous one (the first, since the last reset)
    const p = await studio.call((b) => b.perfProfile(true, this.emu ? 0.5 : 0));
    this.profile = p;
    if (p.slots.length || p.sections.length) {
      const u = blockUse(p);
      this.history = [...this.history, u.whole / u.budget].slice(-HISTORY);
    }
  }

  private async run() {
    const ticket = ++this.loop;
    try {
      while (ticket === this.loop && this.live && studio.screen === 'perf' && studio.online) {
        await this.tick();
        await sleep(1000);
      }
    } catch (e) {
      studio.say(errorText(e), 'error');
      this.live = false;
    }
  }

  /** one reading: counted afresh, then a second of the preset playing (the emulator plays it) */
  async readOnce() {
    if (this.reading) return;
    this.reading = true;
    try {
      if (!this.emu) {
        await studio.call((b) => b.perfProfile(true, 0));
        await sleep(1000);
      }
      await this.tick();
    } catch (e) {
      studio.say(errorText(e), 'error');
    } finally {
      this.reading = false;
    }
  }

  // ---------------------------------------------------------------- the bench
  /** a .elf, or a block's .c (built by the SDK now, and again at every measurement) */
  async pickModule() {
    try {
      const m = await studio.callLong(t('perf.reading_module'), (b) => b.pickBenchModule());
      if (m) {
        this.module = m;
        this.source = 'file';
        this.build = m.error ? { error: m.error, log: m.log } : null;
        this.seen = m.modified;
      }
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  // the module's file watched: saved again -> built again (a .c) and measured again, as the last run of it was
  build = $state<{ error: string; log: string } | null>(null);   // the last build that failed: the compiler's words
  watching = $state(false);
  private seen = 0;                       // the file's time last measured (or picked)
  private watchTicket = 0;

  setWatch(on: boolean) {
    this.watching = on;
    const ticket = ++this.watchTicket;
    if (on) void this.watchLoop(ticket);
  }

  private async watchLoop(ticket: number) {
    while (ticket === this.watchTicket && this.watching && studio.screen === 'perf') {
      await sleep(800);
      const m = this.module;
      if (!m || this.source !== 'file' || studio.progress) continue;
      let t = 0;
      try { t = await studio.call((b) => b.benchStamp(m.path)); } catch { continue; }
      if (t && t !== this.seen && ticket === this.watchTicket) {
        this.seen = t;
        await sleep(250);                   // let the editor finish writing
        const last = this.runs.find((r) => r.target.kind === 'file' && r.target.path === m.path);
        await this.measure(last);
      }
    }
    if (ticket === this.watchTicket) this.watching = false;
  }

  private targetNow(): { target: BenchTarget; label: string; detail: string } | null {
    if (this.source === 'file') {
      const m = this.module;
      return m ? { target: { kind: 'file', path: m.path }, label: m.name ?? m.file, detail: m.file } : null;
    }
    const it = studio.catalogs[this.menu]?.find((i) => i.index === this.index);
    return it ? { target: { kind: 'catalog', menu: this.menu, index: it.index }, label: it.name, detail: it.kind } : null;
  }

  get ready() { return !!this.targetNow(); }

  /** measure the target picked now, or one measured before again (a file is read from the disk again) */
  async measure(again?: Run) {
    const tg = again ? { target: again.target, label: again.label, detail: again.detail } : this.targetNow();
    if (!tg) return;
    const opts = again ? { ...this.opts, cold: again.result.cold, bypass: again.result.bypass } : { ...this.opts };
    try {
      const file = tg.target.kind === 'file' ? tg.target.path : null;
      const result = await studio.callLong(t(file?.toLowerCase().endsWith('.c') ? 'perf.building' : 'perf.measuring', { name: tg.label }),
                                           (b) => b.perfBench(tg.target, opts));
      if (file && result.modified) this.seen = result.modified;
      if (result.status === -100) {                 // the source did not build: its words stay on screen
        this.build = { error: result.error ?? t('perf.not_built'), log: result.log ?? '' };
        studio.say(`${tg.detail}: ${t('perf.not_built')}`, 'error');
        return;
      }
      if (file) this.build = null;
      if (result.status !== 0) {
        studio.say(`${tg.label}: ${result.status_text}`, 'error');
        return;
      }
      const label = result.name ?? tg.label;
      if (file && this.module?.path === file && result.name) {
        this.module = { ...this.module, name: result.name, menu: result.menu, size: result.size, modified: result.modified ?? this.module.modified, error: '' };
      }
      const run: Run = { id: this.next++, label, detail: tg.detail, target: tg.target, result, at: Date.now() };
      this.runs = [run, ...this.runs];
      if (this.ref === null) this.ref = run.id;
      this.shown = run.id;
      const ref = this.refRun;
      if (ref && ref.id !== run.id && ref.result.crc !== result.crc && ref.result.bypass === result.bypass &&
          sameTarget(ref, run))
        studio.say(t('perf.crc_changed', { name: tg.label, from: ref.result.crc, to: result.crc }));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  get refRun() { return this.runs.find((r) => r.id === this.ref) ?? null; }

  remove(id: number) {
    this.runs = this.runs.filter((r) => r.id !== id);
    if (this.ref === id) this.ref = this.runs.at(-1)?.id ?? null;
    if (this.shown === id) this.shown = this.runs[0]?.id ?? null;
  }

  clear() {
    this.runs = [];
    this.ref = this.shown = null;
  }
}

export function sameTarget(a: Run, b: Run) {
  return JSON.stringify(a.target) === JSON.stringify(b.target);
}

export const perf = new Perf();
