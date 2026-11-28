// The IRs tab's state: the 20 user IR slots (what the pedal's flash holds) and a .wav on its way into one.

import { errorText } from './api';
import { t } from './i18n.svelte';
import { studio } from './store.svelte';
import type { IrFile, IrSlot } from './types';

const MENU_IR = 6;                          // the browser's IR menu, which lists the slots by name

export interface IrDraft { slot: number; file: IrFile; name: string; normalize: boolean; busy: boolean }

class Irs {
  slots = $state<IrSlot[] | null>(null);
  picked = $state(0);
  draft = $state<IrDraft | null>(null);
  renaming = $state(false);

  get slot() { return this.slots?.[this.picked] ?? null; }

  async enter() {
    try {
      this.slots = await studio.call((b) => b.irs());
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** a .wav for the picked slot: converted as the slot would hold it, nothing written yet */
  async choose() {
    try {
      const f = await studio.call((b) => b.pickIr());
      if (f) this.draft = { slot: this.picked, file: f, name: f.name.slice(0, 16), normalize: false, busy: false };
    } catch (e) {
      studio.ask({ title: t('irs.file_error_title'), text: errorText(e), actions: [{ id: 'ok', label: t('irs.close') }] });
    }
  }

  /** normalisation changed: converted again */
  async normalize(on: boolean) {
    const d = this.draft;
    if (!d || d.normalize === on) return;
    d.normalize = on;
    try {
      const f = await studio.call((b) => b.irPreview(d.file.path, on));
      if (this.draft === d) d.file = f;
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  async write() {
    const d = this.draft;
    if (!d || !d.name.trim()) return;
    d.busy = true;
    try {
      this.slots = await studio.call((b) => b.writeIr(d.slot, d.file.path, d.name, d.normalize));
      this.draft = null;
      studio.say(t('irs.written', { n: d.slot + 1, name: d.name.trim() }));
      void studio.reloadCatalogs([MENU_IR]);
    } catch (e) {
      d.busy = false;
      studio.say(errorText(e), 'error');
    }
  }

  async rename(name: string) {
    const s = this.slot;
    this.renaming = false;
    if (!s || !name.trim() || name.trim() === s.name) return;
    try {
      const names = await studio.call((b) => b.renameIr(s.slot, name));
      if (this.slots) this.slots.forEach((x, i) => { x.name = names[i] ?? x.name; });
      studio.say(t('irs.renamed', { n: s.slot + 1, name: names[s.slot] }));
      void studio.reloadCatalogs([MENU_IR]);
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }
}

export const irs = new Irs();
