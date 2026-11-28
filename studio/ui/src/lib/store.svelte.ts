// The interface's state: the edited preset as the pedal reports it, what is selected, and the edits on their way.
//
// The pedal is the truth: every edit goes to it and the next poll brings its preset back (its own changes too:
// a knob turned on the pedal shows here). Edits go one at a time, in order; a knob being dragged sends only its
// latest value once the previous one is answered.

import { connectBackend, errorText, type Backend } from './api';
import { i18n, t, tn, type Choice, type MsgKey, isText } from './i18n.svelte';
import { EDIT } from './types';
import type {
  CatalogItem, CtlTable, Hello, LinkStatus, ModSettings, Param, PresetDetail, PresetEntry, Progress, Rig,
} from './types';
import { stepped, valueText } from './format';

export type Selection =
  | { kind: 'block'; slot: number }
  | { kind: 'cell'; cell: number }
  | { kind: 'joint'; which: 'split' | 'merge' }
  | null;

export type Screen = 'grid' | 'presets' | 'drums' | 'pedal' | 'effects' | 'nam' | 'global' | 'irs' | 'perf';

/** a question in the middle of the window; the answer is the id of the action chosen, null for none */
export interface DialogSpec {
  title: string;
  text: string;
  actions: { id: string; label: string; kind?: 'primary' | 'danger' }[];
}

const POLL_MS = 450;
const LINK_MS = 1500, SEARCH_MS = 700;      // the connection asked: while connected; while waiting for the pedal
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** nothing stored there: an empty place, or one of the original's that the factory left empty */
export const isVacant = (p: PresetEntry | undefined) => !p || p.place === 2 || p.origin === 2;

class Studio {
  screen = $state<Screen>('grid');
  hello = $state<Hello | null>(null);
  rig = $state<Rig | null>(null);
  params = $state<Param[]>([]);
  selection = $state<Selection>(null);
  presets = $state<PresetEntry[]>([]);
  catalogs = $state<Record<number, CatalogItem[]>>({});
  browseMenu = $state(3);
  toast = $state<{ text: string; kind: 'info' | 'error'; at: number; action?: { label: string; run: () => void } } | null>(null);
  dragging = $state(false);           // a knob is held: the poll leaves the parameters alone
  // the presets screen
  picked = $state<number | null>(null);                   // the preset shown in the details
  details = $state<Record<number, PresetDetail>>({});     // stored presets read so far
  placing = $state<{ op: 'move' | 'copy'; src: number } | null>(null);   // "Mover para…": waiting for a place
  renaming = $state(false);                               // the details' name is being edited
  progress = $state<Progress | null>(null);               // a long task (backup, restore)
  dialog = $state<(DialogSpec & { answer: (id: string | null) => void }) | null>(null);
  private prefetching = false;
  private epoch = 0;                  // bumped by every change of stored presets: older reads are dropped

  link = $state<LinkStatus | null>(null);                 // the connection (webapp.Api.status)
  private loadedFor = $state(0);      // the connection whose data are on screen (LinkStatus.connections)
  private connecting = false;
  private polling = false;

  private backend: Backend | null = null;
  private queue: Promise<unknown> = Promise.resolve();
  private pending = 0;
  private ownGen = -1;
  private paramsFor = -1;
  private sending = new Map<string, number>();   // "slot/k" -> the latest value not yet sent

  get selectedBlock() {
    const s = this.selection;
    return s?.kind === 'block' ? this.rig?.blocks.find((b) => b.slot === s.slot) ?? null : null;
  }

  /** the window opened: follow the connection for good (the pedal may come, go and come back) */
  async start() {
    this.backend = await connectBackend();
    const b = this.backend;
    const prefs = await b.prefs().catch(() => ({} as Record<string, unknown>));
    i18n.persist = (choice, lang) => {
      void b.setPrefs({ language: choice }).catch(() => {});
      void b.setLanguage(lang).catch(() => {});
    };
    i18n.set(typeof prefs.language === 'string' ? (prefs.language as Choice) : 'system');
    void this.watchLink();
  }

  get online() { return this.link?.phase === 'connected'; }
  /** connected at least once in this window: the screens stay, dimmed, while the pedal is away */
  get everConnected() { return this.loadedFor > 0; }

  /** where the connection stands, asked again and again (it never waits for the pedal) */
  private async watchLink() {
    for (;;) {
      await this.checkLink();
      await sleep(this.online ? LINK_MS : SEARCH_MS);
    }
  }

  private async checkLink() {
    let s: LinkStatus;
    try {
      s = await this.backend!.status();
    } catch {
      return;
    }
    const wasOnline = this.online;
    this.link = s;
    if (s.phase === 'connected' && s.connections !== this.loadedFor && !this.connecting) {
      this.connecting = true;
      try {
        await this.connected(this.loadedFor > 0);
        this.loadedFor = s.connections;
      } catch (e) {
        this.say(errorText(e), 'error');
      } finally {
        this.connecting = false;
      }
    } else if (wasOnline && s.phase !== 'connected') {
      this.dialog?.answer(null);
      this.dragging = false;
    }
  }

  /** a pedal (again): everything read from it anew */
  private async connected(again: boolean) {
    const s = this.link!;
    this.hello = { connected: true, transport: s.transport, name: s.name, version: s.version, protocol: s.protocol };
    if (again) {                        // it may have changed while away (another firmware, the pedal's own edits)
      this.details = {};
      this.catalogs = {};
      this.selection = null;
      this.params = [];
      this.paramsFor = -1;
      this.rig = null;
    }
    await this.refresh(true);
    this.presets = await this.backend!.presets();
    if (this.screen === 'presets') this.show('presets');    // opened before the list came: read it now
    void this.loadCatalogs();
    if (!this.polling) {
      this.polling = true;
      this.poll();
    }
    if (again) this.say(t('store.reconnected'));
    else this.demoSetup();
  }

  /** the demonstration takes ?preset=N&select=SLOT&route=MODE&screen=…&pick=N (design reviews, screenshots) */
  private async demoSetup() {
    try {
      const q = new URLSearchParams(location.search);
      if (this.hello?.transport === 'demonstração') {
        const sc = q.get('screen');
        if (sc === 'presets' || sc === 'drums' || sc === 'pedal' || sc === 'effects' || sc === 'nam' || sc === 'global' || sc === 'irs' || sc === 'perf') this.show(sc);
        if (q.has('pick')) this.pick(Number(q.get('pick')));
        if (q.has('preset')) await this.load(Number(q.get('preset')));
        if (q.has('route')) await this.route(Number(q.get('route')));
        if (q.has('select')) await this.select({ kind: 'block', slot: Number(q.get('select')) });
        if (q.get('joint') === 'split' || q.get('joint') === 'merge') this.selection = { kind: 'joint', which: q.get('joint') as 'split' | 'merge' };
      }
    } catch (e) {
      this.say(errorText(e), 'error');
    }
  }

  private async loadCatalogs() {
    for (const m of [this.browseMenu, 4, 5, 8, 9, 10, 0, 1, 2, 6, 7, 11, 12, 13, 14, 15]) await this.ensureCatalog(m);
  }

  /** a menu's models, read once */
  async ensureCatalog(menu: number) {
    if (this.catalogs[menu] || !this.backend) return;
    try {
      const items = await this.run(() => this.backend!.catalog(menu));
      this.catalogs = { ...this.catalogs, [menu]: items };
    } catch { /* shown as still reading; the next selection tries again */ }
  }

  /** menus whose items changed on the pedal (an IR or a capture written or renamed, the module library replaced;
   *  all of them when not given) read again, the old list shown until the new one comes; then the chain, whose
   *  blocks may carry the names that changed */
  async reloadCatalogs(menus: number[] = [...Array(16).keys()]) {
    if (!this.backend) return;
    for (const m of menus) {
      try {
        const items = await this.run(() => this.backend!.catalog(m));
        this.catalogs = { ...this.catalogs, [m]: items };
      } catch { /* the old list stays */ }
    }
    try { await this.refresh(true); } catch { /* shown when an edit fails */ }
  }

  /** the progress box's Cancelar: a flash dump stops at once (nothing is lost); a firmware update is asked
   *  about first, then the backend stops at the next packet */
  async cancelLong() {
    if (this.progress?.task === 'dump') {
      await this.backend?.cancelTask();
      return;
    }
    const id = await this.ask({
      title: t('store.cancel_update_title'),
      text: t('store.cancel_update_text'),
      actions: [{ id: 'go', label: t('store.cancel_update_go'), kind: 'danger' }, { id: 'no', label: t('store.cancel_update_keep') }],
    });
    if (id === 'go') await this.backend?.cancelUpdate();
  }

  /** look for the pedal now (the waiting screen's button) */
  async retryLink() {
    await this.backend?.retry();
    await sleep(250);
    await this.checkLink();
  }

  private poll() {
    setTimeout(async () => {
      if (this.pending === 0 && !document.hidden && this.online && !this.connecting) {
        try { await this.refresh(false); } catch { /* shown when an edit fails */ }
      }
      this.poll();
    }, POLL_MS);
  }

  /** the preset again; the selected block's parameters when something else changed them */
  private async refresh(force: boolean) {
    if (!this.backend) return;
    const before = this.rig;
    const r = await this.run(() => this.backend!.rig());
    if (!force && before && r.gen === before.gen && r.loads === before.loads) return;
    const loaded = !before || r.loads !== before.loads || r.index !== before.index;
    this.rig = r;
    const sel = this.selection;
    if (sel?.kind === 'block' && !r.blocks.some((b) => b.slot === sel.slot)) this.selection = null;
    if (loaded) {
      this.selection = null;
      this.params = [];
      this.paramsFor = -1;
      if (before) this.presets = await this.run(() => this.backend!.presets());
    }
    if (before && before.dirty && !r.dirty && r.index === before.index) this.forget([r.index]);   // saved on the pedal
    const blk = this.selectedBlock;
    if (blk && !this.dragging && (force || loaded || r.gen !== this.ownGen || this.paramsFor !== blk.slot))
      await this.fetchParams(blk.slot);
  }

  private async fetchParams(slot: number) {
    const ps = await this.run(() => this.backend!.block(slot));
    if (this.selectedBlock?.slot === slot) {
      this.params = ps;
      this.paramsFor = slot;
    }
  }

  /** a call to the pedal from another screen's store, in the same queue as every other */
  call<T>(fn: (b: Backend) => Promise<T>): Promise<T> {
    if (!this.backend) return Promise.reject(new Error(t('store.not_connected')));
    const b = this.backend;
    return this.run(() => fn(b));
  }

  /** a long call from another screen's store, with the pedal's progress shown over the window */
  callLong<T>(label: string, fn: (b: Backend) => Promise<T>): Promise<T> {
    if (!this.backend) return Promise.reject(new Error(t('store.not_connected')));
    const b = this.backend;
    return this.withProgress(label, () => fn(b));
  }

  /** calls to the pedal one at a time */
  private run<T>(fn: () => Promise<T>): Promise<T> {
    this.pending++;
    const p = this.queue.then(fn, fn);
    this.queue = p.catch(() => undefined);
    return p.finally(() => this.pending--);
  }

  private async edit(op: number, a = 0, b = 0, c = 0, v = 0, quiet = false, text?: string) {
    if (!this.backend) return null;
    try {
      const r = await this.run(() => this.backend!.edit(op, a, b, c, v, text));
      this.ownGen = r.gen;
      if (r.result < 0 && !quiet) this.say(t(EDIT_ERRORS[op] ?? 'store.edit_refused'), 'error');
      return r;
    } catch (e) {
      this.say(errorText(e), 'error');
      return null;
    }
  }

  say(text: string, kind: 'info' | 'error' = 'info', action?: { label: string; run: () => void }) {
    if (kind === 'error' && (!this.online || ['py.pedal_disconnected', 'py.pedal_gone', 'py.link_lost'].some((k) => isText(k as MsgKey, text)))) {
      void this.checkLink();            // the waiting screen says what happened; no toast for each failed call
      return;
    }
    this.toast = { text, kind, at: Date.now(), action };
  }

  // ---------------------------------------------------------------- selection
  async select(sel: Selection) {
    this.selection = sel;
    if (sel?.kind === 'block') {
      const blk = this.selectedBlock;
      if (blk) {
        this.browseMenu = blk.menu;
        void this.ensureCatalog(blk.menu);
        if (this.paramsFor !== blk.slot) this.params = [];
        await this.fetchParams(blk.slot);
      }
    }
  }

  // ---------------------------------------------------------------- edits
  /** a knob moved: shown at once, sent coalesced */
  setParam(k: number, value: number) {
    const blk = this.selectedBlock;
    const p = this.params.find((x) => x.k === k);
    if (!blk || !p) return;
    const v = stepped(p, value);
    if (v === p.value) return;
    p.value = v;
    p.text = valueText(p, v);
    const key = `${blk.slot}/${k}`;
    const inFlight = this.sending.has(key);
    this.sending.set(key, v);
    if (!inFlight) void this.flushParam(blk.slot, k, key);
  }

  private async flushParam(slot: number, k: number, key: string) {
    while (this.sending.has(key)) {
      const v = this.sending.get(key)!;
      const r = await this.edit(EDIT.PARAM, slot, k, 0, v);
      if (this.sending.get(key) === v) this.sending.delete(key);
      const p = this.params.find((x) => x.k === k);
      if (r?.param && p && this.paramsFor === slot && !this.sending.has(key)) Object.assign(p, r.param);
    }
  }

  resetParam(k: number) {
    const p = this.params.find((x) => x.k === k);
    if (p) this.setParam(k, p.default);
  }

  async toggle(slot: number) {
    const blk = this.rig?.blocks.find((b) => b.slot === slot);
    if (!blk) return;
    blk.on = !blk.on;                       // shown at once; the poll confirms it
    await this.edit(EDIT.ON, slot, blk.on ? 1 : 0);
    await this.refresh(true);
  }

  async remove(slot: number) {
    await this.edit(EDIT.REMOVE, slot);
    this.selection = null;
    await this.refresh(true);
  }

  async move(slot: number, cell: number) {
    const blk = this.rig?.blocks.find((b) => b.slot === slot);
    if (!blk || blk.cell === cell) return;
    const other = this.rig!.blocks.find((b) => b.cell === cell);
    if (other) other.cell = blk.cell;
    blk.cell = cell;
    await this.edit(EDIT.MOVE, slot, cell);
    await this.refresh(true);
  }

  /** a model chosen in the browser: into the selected block, or the selected (or given) empty cell */
  async choose(menu: number, index: number, cell?: number) {
    const sel = this.selection;
    if (cell === undefined && sel?.kind === 'block') {
      await this.edit(EDIT.REPLACE, sel.slot, menu, index);
      await this.refresh(true);
      return;
    }
    const target = cell ?? (sel?.kind === 'cell' ? sel.cell : this.firstFreeCell());
    if (target < 0) {
      this.say(t('store.chain_full'), 'error');
      return;
    }
    const r = await this.edit(EDIT.PLACE, target, menu, index);
    await this.refresh(true);
    if (r && r.result >= 0) await this.select({ kind: 'block', slot: r.result });
  }

  async replaceAt(slot: number, menu: number, index: number) {
    await this.edit(EDIT.REPLACE, slot, menu, index);
    await this.refresh(true);
  }

  private firstFreeCell() {
    const used = new Set(this.rig?.blocks.map((b) => b.cell));
    for (let c = 0; c < 12; c++) if (!used.has(c)) return c;
    return -1;
  }

  async route(mode: number, split?: number, merge?: number) {
    const r = this.rig?.route;
    if (!r) return;
    await this.edit(EDIT.ROUTE, mode, split ?? r.split, merge ?? r.merge, 0, true);
    await this.refresh(true);
  }

  async joint(k: number, value: number) {
    if (!this.rig) return;
    this.rig.route.joints[k] = Math.round(value);
    await this.edit(EDIT.JOINT, k, Math.round(value));
  }

  /** the scene menu's edits (protocol 10; the demonstration has them too) */
  get canEditScenes() { return (this.hello?.protocol ?? 0) >= 10 || this.link?.transport === 'demonstração'; }

  /** a scene button: with the scene menu, the scene plays and edits go to it (as picking it in the pedal's menu) */
  async scene(n: number) {
    await this.edit(this.canEditScenes ? EDIT.SCENE_PICK : EDIT.SCENE, n);
    await this.refresh(true);
  }

  async sceneCopy(from: number, to: number) {
    const r = await this.edit(EDIT.SCENE_COPY, from, to);
    if (r && r.result >= 0) this.say(t('store.scene_copied', { from: from + 1, to: to + 1 }));
    await this.refresh(true);
  }

  async sceneRename(n: number, name: string) {
    await this.edit(EDIT.SCENE_NAME, n, 0, 0, 0, false, name.trim().toUpperCase().slice(0, 10));
    await this.refresh(true);
  }

  async sceneRevert(n: number) {
    const r = await this.edit(EDIT.SCENE_REVERT, n, 0, 0, 0, true);
    this.say(r && r.result >= 0 ? t('store.scene_reverted', { n: n + 1 }) : t('store.scene_not_saved'), r && r.result >= 0 ? 'info' : 'error');
    await this.refresh(true);
  }

  /** parameter k of slot: a value per scene, or the preset's again */
  async sceneParam(slot: number, k: number) {
    const r = await this.edit(EDIT.SCENE_PARAM, slot, k, 0, 0, true);
    if (r && r.result < 0) this.say(t('store.scene_param_full'), 'error');
    await this.refresh(true);
    await this.fetchParams(slot);
  }


  /** the edited preset's name goes with EDIT_SAVE (protocol 12; the demonstration has it too) */
  get canName() { return (this.hello?.protocol ?? 0) >= 12 || this.link?.transport === 'demonstração'; }

  /** the pedal's SAVE: the edited preset into `target` (its own place by default), under `name` when given (the
   *  save screen's NOME); it becomes the active one. True when stored. */
  async save(target?: number, name?: string) {
    if (!this.rig) return false;
    const from = this.rig.index;
    const i = target ?? from;
    const n = name?.trim().slice(0, 17);
    const renamed = n !== undefined && n !== this.rig.name.trim();
    if (renamed && this.canName) {
      const r = await this.edit(EDIT.NAME, 0, 0, 0, 0, false, n);
      if (!r || r.result < 0) return false;
    }
    const r = await this.edit(EDIT.SAVE, i);
    const ok = !!r && r.result >= 0;
    if (ok && renamed && !this.canName) {
      // an older firmware: the stored preset renamed after the save (the pedal reloads it; nothing unsaved is lost)
      await this.run(() => this.backend!.rename(i, n!)).catch((e) => this.say(errorText(e), 'error'));
    }
    if (ok) this.say(renamed ? t('store.saved_as', { label: presetLabel(i), name: n || t('ctl.unnamed') })
                             : t('store.saved', { label: presetLabel(i) }));
    this.forget([i, from]);
    await this.refresh(true);
    await this.reloadList();
    return ok;
  }

  async load(index: number) {
    if (!this.backend || index < 0 || index >= 240) return;
    await this.run(() => this.backend!.load(index));
    await this.refresh(true);
    this.presets = await this.run(() => this.backend!.presets());
  }

  // ---------------------------------------------------------------- screens and questions
  show(screen: Screen) {
    this.screen = screen;
    if (screen === 'presets') {
      if (this.picked === null && this.rig) this.pick(this.rig.index);
      void this.prefetch();
    }
  }

  ask(spec: DialogSpec): Promise<string | null> {
    this.dialog?.answer(null);
    return new Promise((resolve) => {
      this.dialog = { ...spec, answer: (id) => { this.dialog = null; resolve(id); } };
    });
  }

  /** the active preset has edits the pedal has not stored: what to do with them before `what` */
  private async keepEdits(what: string): Promise<boolean> {
    const r = this.rig;
    if (!r?.dirty) return true;
    const id = await this.ask({
      title: t('ctl.unsaved_title', { label: presetLabel(r.index) }),
      text: r.name ? t('store.unsaved_text_named', { label: presetLabel(r.index), name: r.name, what })
                   : t('store.unsaved_text', { label: presetLabel(r.index), what }),
      actions: [{ id: 'save', label: t('ctl.save_continue'), kind: 'primary' }, { id: 'discard', label: t('store.discard_continue') },
                { id: 'cancel', label: t('common.cancel') }],
    });
    if (id === 'save') await this.save();
    return id === 'save' || id === 'discard';
  }

  // ---------------------------------------------------------------- the presets screen
  pick(index: number) {
    if (index < 0 || index >= 240) return;
    this.picked = index;
    void this.prefetch();                 // reads the picked one first (and goes on with the rest)
  }

  /** a stored block's model name, from its menu's catalog (by id, else by its catalog index) */
  modelName(menu: number, id: number, index: number): string | null {
    const c = this.catalogs[menu];
    return c?.find((i) => i.id === id)?.name ?? c?.find((i) => i.index === index)?.name ?? null;
  }

  /** a stored preset's details, read once (forget() drops them when the preset changes) */
  async detail(index: number) {
    if (this.details[index] || !this.backend || isVacant(this.presets[index])) return;
    const epoch = this.epoch;
    try {
      const d = await this.run(() => this.backend!.preset(index));
      if (epoch === this.epoch) this.details = { ...this.details, [index]: d };   // else read before a change
    } catch (e) {
      if (index === this.picked) this.say(errorText(e), 'error');
    }
  }

  /** the details read so far, where a MOVE from src to dst put their presets */
  private slideDetails(src: number, dst: number) {
    const at = (i: number) =>
      i === src ? dst : src < i && i <= dst ? i - 1 : dst <= i && i < src ? i + 1 : i;
    const out: Record<number, PresetDetail> = {};
    for (const [k, d] of Object.entries(this.details)) {
      const i = at(Number(k));
      out[i] = { ...d, index: i };
    }
    this.details = out;
  }

  private forget(indexes: number[] | 'all') {
    if (indexes === 'all') { this.details = {}; return; }
    const d = { ...this.details };
    for (const i of indexes) delete d[i];
    this.details = d;
  }

  /** every stored preset's details in the background (the cards' chains), one at a time, when nothing else waits */
  private async prefetch() {
    if (this.prefetching) return;
    this.prefetching = true;
    try {
      while (this.screen === 'presets' && this.backend) {
        const want = (p: PresetEntry | undefined) => !!p && !isVacant(p) && !this.details[p.index];
        const picked = this.picked === null ? undefined : this.presets[this.picked];
        const next = want(picked) ? picked : this.presets.find(want);       // the one shown first
        if (!next) break;
        if (this.pending > 0 || this.dialog || this.progress) { await sleep(120); continue; }
        await this.detail(next.index);
      }
    } finally {
      this.prefetching = false;
    }
  }

  private async reloadList() {
    if (!this.backend) return;
    this.presets = await this.run(() => this.backend!.presets());
    if (this.screen === 'presets') void this.prefetch();
  }

  /** a pedal call that changes stored presets: its error shown, true when it went through */
  private async change(fn: () => Promise<unknown>) {
    this.epoch++;
    try {
      await this.run(fn);
      return true;
    } catch (e) {
      this.say(errorText(e), 'error');
      return false;
    } finally {
      await this.refresh(true).catch(() => undefined);
      await this.reloadList().catch(() => undefined);
    }
  }

  /** make `index` the pedal's preset; edits of the active one are dealt with first */
  async activate(index: number) {
    if (this.rig?.index === index) return true;
    if (!(await this.keepEdits(t('store.activate_loses', { label: presetLabel(index) })))) return false;
    await this.load(index);
    return true;
  }

  async renamePreset(index: number, name: string) {
    const n = name.trim();
    const p = this.presets[index];
    if (!this.backend || !p || n === p.name.trim()) return;
    if (this.rig?.index === index && this.rig.dirty &&
        !(await this.keepEdits(t('store.rename_reloads')))) return;
    let got = n;
    const ok = await this.change(async () => { got = await this.backend!.rename(index, n); });
    this.forget([index]);
    if (ok) this.say(t('store.renamed', { label: presetLabel(index), name: got }));
  }

  /** a preset's edited control table (and modulators) into the stored preset; the pedal reloads it when it is the
   *  active one, so its unsaved edits go: asked first. True when written. */
  async writeControls(index: number, table: CtlTable, mods: ModSettings[] | null) {
    if (!this.backend) return false;
    if (this.rig?.index === index && this.rig.dirty) {
      const id = await this.ask({
        title: t('ctl.unsaved_title', { label: presetLabel(index) }),
        text: t('store.controls_text'),
        actions: [{ id: 'go', label: t('store.write_anyway'), kind: 'danger' }, { id: 'cancel', label: t('common.cancel') }],
      });
      if (id !== 'go') return false;
    }
    let n = 0;
    const ok = await this.change(async () => { n = await this.backend!.writeControls(index, table, mods); });
    this.forget([index]);
    if (ok) this.say(tn('store.controls_saved', n, { label: presetLabel(index) }));
    return ok;
  }

  /** the pedal's MOVE: out of src, into dst, the presets between slide one place (undone by moving it back) */
  async movePreset(src: number, dst: number, undo = false) {
    if (!this.backend || src === dst) return;
    const name = this.presets[src]?.name || t('ctl.unnamed');
    const other = this.presets[dst]?.name || t('ctl.unnamed');
    const ok = await this.change(() => this.backend!.move(src, dst));
    if (!ok) {
      this.forget('all');
      return;
    }
    this.slideDetails(src, dst);
    this.picked = dst;
    if (undo) {
      this.say(t('store.moved_back', { name, label: presetLabel(dst) }));
      return;
    }
    const between = Math.abs(dst - src) === 1 ? t('store.move_swapped', { name: other })
      : src < dst ? t('store.move_slid_later', { from: presetLabel(src), to: presetLabel(dst - 1) })
                  : t('store.move_slid_earlier', { from: presetLabel(dst + 1), to: presetLabel(src) });
    this.say(t('store.moved', { name, from: presetLabel(src), to: presetLabel(dst), between }), 'info',
             { label: t('store.undo'), run: () => void this.movePreset(dst, src, true) });
  }

  async copyPreset(src: number, dst: number) {
    if (!this.backend || src === dst) return;
    const to = this.presets[dst];
    if (!isVacant(to)) {
      const live = this.rig?.index === dst && this.rig.dirty ? t('store.copy_live_lost') : '';
      const id = await this.ask({
        title: t('store.copy_over_title', { label: presetLabel(dst) }),
        text: t('store.copy_over_text', { dst: presetLabel(dst), dst_name: to.name || t('ctl.unnamed'), src: presetLabel(src),
                                          src_name: this.presets[src]?.name || t('ctl.unnamed'), live }),
        actions: [{ id: 'go', label: t('store.replace'), kind: 'danger' }, { id: 'cancel', label: t('common.cancel') }],
      });
      if (id !== 'go') return;
    }
    const ok = await this.change(() => this.backend!.copy(src, dst));
    this.forget([dst]);
    if (ok) {
      this.picked = dst;
      this.say(t('store.copied', { src: presetLabel(src), dst: presetLabel(dst) }));
    }
  }

  async exportPreset(index: number) {
    if (!this.backend) return;
    try {
      const file = await this.run(() => this.backend!.exportPreset(index));
      if (file) this.say(t('store.exported', { label: presetLabel(index), file }));
    } catch (e) {
      this.say(errorText(e), 'error');
    }
  }

  async importPreset(index: number) {
    if (!this.backend) return;
    let f;
    try {
      f = await this.backend.pickPreset();
    } catch (e) {
      this.say(errorText(e), 'error');
      return;
    }
    if (!f) return;
    const to = this.presets[index];
    if (!isVacant(to) || (this.rig?.index === index && this.rig.dirty)) {
      const id = await this.ask({
        title: t('store.import_over_title', { label: presetLabel(index) }),
        text: t('store.import_over_text', { name: f.name || t('ctl.unnamed'), file: f.file, label: presetLabel(index),
                                            old: to?.name || t('ctl.unnamed'),
                                            live: this.rig?.index === index && this.rig.dirty ? t('store.import_live_lost') : '' }),
        actions: [{ id: 'go', label: t('store.import'), kind: 'danger' }, { id: 'cancel', label: t('common.cancel') }],
      });
      if (id !== 'go') return;
    }
    const ok = await this.change(() => this.backend!.importPreset(index, f.path));
    this.forget([index]);
    if (ok) this.say(t('store.imported', { name: f.name, label: presetLabel(index) }));
  }

  /** a long call with the pedal's progress shown (the backend's progress() runs beside it) */
  private async withProgress<T>(label: string, fn: () => Promise<T>): Promise<T> {
    this.progress = { label, done: 0, total: 0 };
    let on = true;
    void (async () => {
      while (on) {
        await sleep(200);
        const p = await this.backend!.progress().catch(() => null);
        if (on && p) this.progress = p;
      }
    })();
    try {
      return await this.run(fn);
    } finally {
      on = false;
      this.progress = null;
    }
  }

  async backup() {
    if (!this.backend) return;
    try {
      const r = await this.withProgress(t('store.backup'), () => this.backend!.backup());
      if (r) this.say(tn(r.global ? 'store.backup_done_global' : 'store.backup_done', r.presets, { file: r.file }));
    } catch (e) {
      this.say(errorText(e), 'error');
    }
  }

  async restore() {
    if (!this.backend) return;
    let b;
    try {
      b = await this.backend.pickBackup();
    } catch (e) {
      this.say(errorText(e), 'error');
      return;
    }
    if (!b?.path) return;
    const when = b.created ? t('store.restore_when', { date: b.created.replace('T', ' ') }) : '';
    const id = await this.ask({
      title: t('store.restore_title'),
      text: tn('store.restore_text', b.presets, { file: b.file, when }) +
            (b.global ? t('store.restore_global') : t('store.restore_no_global')),
      actions: [{ id: 'go', label: t('store.restore'), kind: 'danger' }, { id: 'cancel', label: t('common.cancel') }],
    });
    if (id !== 'go') return;
    const path = b.path;
    this.epoch++;
    try {
      const n = await this.withProgress(t('store.restoring'), () => this.backend!.restore(path));
      this.say(tn(b.global ? 'store.restored_global' : 'store.restored', n));
    } catch (e) {
      this.say(errorText(e), 'error');
    }
    this.forget('all');
    await this.refresh(true).catch(() => undefined);
    await this.reloadList().catch(() => undefined);
  }
}

const EDIT_ERRORS: Record<number, MsgKey> = {
  [EDIT.PLACE]: 'store.err_place',
  [EDIT.REPLACE]: 'store.err_replace',
  [EDIT.MOVE]: 'store.err_move',
  [EDIT.SAVE]: 'store.err_save',
  [EDIT.NAME]: 'store.err_name',
};

export function presetLabel(i: number) {
  return `${String(Math.floor(i / 4) + 1).padStart(2, '0')}${'ABCD'[i % 4]}`;
}

export const studio = new Studio();
