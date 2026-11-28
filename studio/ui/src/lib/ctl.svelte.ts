// The controls editor's state: a preset's control table (or the pedal's global one) as a draft, edited like the
// pedal's CONTROLES / PARA ONDE screens (lib/controls.ts); nothing reaches the pedal before "Gravar no pedal".

import { errorText } from './api';
import * as C from './controls';
import { t, tn } from './i18n.svelte';
import { presetLabel, studio, type Screen } from './store.svelte';
import type { CtlBlock, CtlLink, CtlTable, ModSettings } from './types';

export interface CtlOpen {
  kind: 'preset' | 'global';
  index: number;                    // the preset (-1 for the global table)
  name: string;
  own: boolean;                     // the preset stores a table of its own
  from: Screen;                     // the screen the editor replaces while open (Presets, or the GRID's live preset)
}

const snapshot = (t: CtlTable, mods: ModSettings[] | null) => JSON.stringify([
  t.links.filter((l) => !C.unset(l)), t.colour, t.label.map((s) => s.toUpperCase()), mods,
]);

class Ctl {
  open = $state<CtlOpen | null>(null);
  loading = $state(false);
  busy = $state(false);
  table = $state<CtlTable>({ links: [], colour: [], label: [] });
  mods = $state<ModSettings[] | null>(null);
  glob = $state<CtlTable | null>(null);
  blocks = $state<CtlBlock[]>([]);
  knobs = $state<Record<string, number[]>>({});
  scenes = $state<string[]>(['', '', '', '']);
  bpm = $state(120);                  // the preset's tempo: the modulators' synced rates
  mode = $state(C.STOMP);
  key = $state(C.A);
  sel = $state<number | null>(null);
  extra = $state<number[]>([]);       // MIDI keys added here that have no link yet
  private base = '';

  get isGlobal() { return this.open?.kind === 'global'; }
  get dirty() { return !!this.open && snapshot(this.table, this.mods) !== this.base; }
  get link(): CtlLink | null { return this.sel === null ? null : this.table.links[this.sel] ?? null; }

  // ---------------------------------------------------------------- open, close, write
  /** the editor shows in place of this screen */
  showsOn(screen: Screen) { return this.open?.from === screen; }

  async openPreset(index: number, from: Screen = 'presets') {
    if (this.open && !(await this.close())) return;     // one opened on another screen, with changes
    const r = studio.rig;
    if (r?.index === index && r.dirty) {
      const id = await studio.ask({
        title: t('ctl.unsaved_title', { label: presetLabel(index) }),
        text: t('ctl.unsaved_text'),
        actions: [{ id: 'save', label: t('ctl.save_continue'), kind: 'primary' }, { id: 'go', label: t('ctl.continue') },
                  { id: 'cancel', label: t('common.cancel') }],
      });
      if (id === 'save') await studio.save();
      else if (id !== 'go') return;
    }
    this.reset({ kind: 'preset', index, name: studio.presets[index]?.name ?? '', own: false, from });
    this.loading = true;
    try {
      const d = await studio.call((b) => b.controls(index));
      if (this.open?.index !== index) return;
      this.open = { kind: 'preset', index, name: d.name, own: d.own, from };
      this.bpm = d.bpm && d.bpm > 0 ? d.bpm : 120;
      this.fill(d.table, d.mods, d.global, d.blocks, d.knobs, d.scenes);
    } catch (e) {
      this.open = null;
      studio.say(errorText(e), 'error');
    } finally {
      this.loading = false;
    }
  }

  async openGlobal() {
    if ((studio.hello?.protocol ?? 0) < 3 && studio.link?.transport !== 'demonstração') {
      studio.say(t('ctl.no_global_fw'), 'error');
      return;
    }
    if (this.open && !(await this.close())) return;
    this.reset({ kind: 'global', index: -1, name: '', own: true, from: 'presets' });
    this.loading = true;
    try {
      const t = await studio.call((b) => b.globalControls());
      if (this.open?.kind !== 'global') return;
      this.fill(t, null, null, [], {}, ['', '', '', '']);
    } catch (e) {
      this.open = null;
      studio.say(errorText(e), 'error');
    } finally {
      this.loading = false;
    }
  }

  private reset(o: CtlOpen) {
    this.open = o;
    this.table = { links: [], colour: [0, 0, 0, 0, 0, 0], label: ['', '', '', '', '', ''] };
    this.mods = null;
    this.glob = null;
    this.blocks = [];
    this.mode = C.STOMP;
    this.key = C.A;
    this.sel = null;
    this.extra = [];
    this.base = snapshot(this.table, null);
  }

  private fill(t: CtlTable, mods: ModSettings[] | null, glob: CtlTable | null, blocks: CtlBlock[],
               knobs: Record<string, number[]>, scenes: string[]) {
    this.table = C.copyTable(t);
    this.mods = mods ? mods.map((m) => ({ ...m, step: [...m.step] })) : null;
    this.glob = glob;
    this.blocks = blocks;
    this.knobs = knobs;
    this.scenes = scenes;
    this.base = snapshot(this.table, this.mods);
  }

  /** close, asking first when there are changes; true when closed */
  async close(): Promise<boolean> {
    if (this.busy) return false;
    if (this.dirty) {
      const id = await studio.ask({
        title: t('ctl.discard_title'),
        text: t('ctl.discard_text'),
        actions: [{ id: 'drop', label: t('common.discard'), kind: 'danger' }, { id: 'cancel', label: t('common.keep_editing') }],
      });
      if (id !== 'drop') return false;
    }
    this.open = null;
    return true;
  }

  async write() {
    const o = this.open;
    if (!o || this.busy) return;
    const table: CtlTable = {
      links: this.table.links.filter((l) => !C.unset(l) && (o.kind === 'preset' || C.globalOk(l))).map(C.copyLink),
      colour: [...this.table.colour],
      label: this.table.label.map((s) => s.toUpperCase().slice(0, C.LABEL_LEN)),
    };
    this.busy = true;
    try {
      if (o.kind === 'global') {
        const n = await studio.call((b) => b.writeGlobalControls(table));
        studio.say(tn('ctl.global_saved', n));
        this.open = null;
      } else if (await studio.writeControls(o.index, table, this.mods ? $state.snapshot(this.mods) : null)) {
        this.open = null;
      }
    } catch (e) {
      studio.say(errorText(e), 'error');
    } finally {
      this.busy = false;
    }
  }

  // ---------------------------------------------------------------- keys and modes
  setMode(mode: number) {
    this.mode = mode;
    this.sel = null;
    if (this.key === C.AGAIN && mode !== C.PRESET) this.key = C.A;
  }

  setKey(key: number) {
    this.key = key;
    this.sel = null;
  }

  /** the keys the list shows, in the pedal's order, by group */
  get keyGroups(): { title: string; keys: number[] }[] {
    const fs = [C.A, C.B, C.C, C.D, C.BANK_DOWN, C.BANK_UP, C.COMBO, C.COMBO + 1, C.COMBO + 2, C.COMBO + 3];
    if (this.mode === C.PRESET && !this.isGlobal) fs.push(C.AGAIN);
    const out = [{ title: t('ctl.group_fs'), keys: fs }, { title: t('ctl.group_pedals'), keys: [C.TOE, C.EXP1A, C.EXP1B, C.EXP2] }];
    if (this.mods) out.push({ title: t('ctl.group_mods'), keys: [C.MOD1, C.MOD1 + 1, C.MOD1 + 2, C.MOD1 + 3] });
    const midi = new Set([...C.midiKeys(this.table), ...(this.glob ? C.midiKeys(this.glob) : []), ...this.extra]);
    if (C.isMidi(this.key)) midi.add(this.key);
    out.push({ title: t('ctl.group_midi'), keys: [...midi].sort((a, b) => a - b) });
    return out;
  }

  /** what a key does in the mode, for the rail: its first link in a word or two, whether that is a global one, how
   *  many more, and whether a NOTHING link silences the global ones */
  summary(key: number): { text: string; global: boolean; more: number; silenced: boolean } {
    const run = C.runs(this.table, this.glob, key, this.mode).filter(([l]) => !C.unset(l));
    const silenced = !run.length && C.linksOf(this.table, key, this.mode).some((i) => this.table.links[i].action === C.NOTHING);
    return { text: run.length ? C.brief(run[0][0], this.blocks) : '', global: !!run[0]?.[1], more: Math.max(0, run.length - 1), silenced };
  }

  /** a MIDI CC key (1-127 but 68 / 69, the tempo); false when it cannot be one */
  addCc(cc: number): boolean {
    if (!C.midiCcOk(cc)) return false;
    const key = C.MIDI_KEY + cc;
    if (!this.extra.includes(key)) this.extra = [...this.extra, key];
    this.setKey(key);
    return true;
  }

  // ---------------------------------------------------------------- links
  get own() { return C.linksOf(this.table, this.key, this.mode); }
  get inherited() { return C.inherited(this.table, this.glob, this.key, this.mode); }
  private keyModes() { return C.everyMode(this.key) ? C.ALL : this.mode; }

  private guard(fn: () => void) {
    try {
      fn();
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** a link picked: one shared by several modes becomes this mode's (the pedal's links screen does the same) */
  select(i: number) {
    this.guard(() => { this.sel = C.own(this.table, i, this.mode); });
  }

  add() {
    this.guard(() => {
      const g = this.glob;
      if (g && !C.group(this.table, this.key, this.mode, false).length && C.group(g, this.key, this.mode, false).length) {
        this.copyGroup(C.group(g, this.key, this.mode, false)[0], false);   // the new one joins the global ones
      }
      this.sel = C.add(this.table, C.newLink(this.key, this.mode));
      this.extra = this.extra.filter((k) => k !== this.key);
    });
  }

  /** a link out: of this mode on a preset; of every mode on the global table (a function off the key everywhere) */
  remove(i: number) {
    if (this.isGlobal) C.removeAlike(this.table, i);
    else C.remove(this.table, i, this.mode);
    this.sel = null;
  }

  /** the global links of gi's group copied into the preset (which then has that group); the copy of gi selected */
  copyGroup(gi: number, select = true) {
    const g = this.glob;
    if (!g) return;
    const hold = g.links[gi].when === C.HOLD;
    let at: number | null = null;
    this.guard(() => {
      for (const i of C.group(g, this.key, this.mode, hold)) {
        const n = C.add(this.table, { ...g.links[i], modes: this.keyModes() });
        if (i === gi) at = n;
      }
    });
    if (select) this.sel = at;
  }

  /** the key does nothing here in this preset (a NOTHING link hides the global ones of its group) */
  nothing(gi: number) {
    const hold = this.glob?.links[gi].when === C.HOLD;
    this.guard(() => { C.add(this.table, C.link(this.key, this.keyModes(), hold ? C.HOLD : C.PRESS, C.NOTHING)); });
    this.sel = null;
  }

  // ---------------------------------------------------------------- the selected link
  get dests() { return C.dests(this.key, this.blocks); }

  choicesOf(destId: string): C.Choice[] {
    const d = this.dests.find((x) => x.id === destId);
    return d ? C.choices(this.key, d, this.scenes, this.knobs) : [];
  }

  pickDest(destId: string) {
    const first = this.choicesOf(destId).find((x) => x.id !== C.REMOVE_CHOICE);
    if (first && this.link) first.apply(this.link);
  }

  pickChoice(destId: string, choiceId: string) {
    if (choiceId === C.REMOVE_CHOICE) {
      if (this.sel !== null) this.remove(this.sel);
      return;
    }
    const c = this.choicesOf(destId).find((x) => x.id === choiceId);
    if (c && this.link) c.apply(this.link);
  }

  setWhen(v: number) {
    const l = this.link;
    if (!l) return;
    if (C.sweeps(l.key)) l.curve = v;
    else l.when = v;
  }

  // ---------------------------------------------------------------- the footswitch cards
  setColour(c: number) { this.table.colour[this.key] = c; }
  setLabel(text: string) { this.table.label[this.key] = text.toUpperCase().slice(0, C.LABEL_LEN); }

  // ---------------------------------------------------------------- modulators
  get mod(): ModSettings | null { return this.mods && C.isMod(this.key) ? this.mods[this.key - C.MOD1] : null; }

  setModType(type: ModSettings['type']) {
    const m = this.mod;
    if (!m || !this.mods) return;
    if (m.type === 'off' && type !== 'off') this.mods[this.key - C.MOD1] = C.modDefault(type);
    else m.type = type;
  }
}

export const ctl = new Ctl();
