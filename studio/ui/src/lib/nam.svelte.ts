// The NAM screen's state: the 10 Clone slots (protocol 6) and a .nam on its way into one of them.

import { errorText } from './api';
import { studio } from './store.svelte';
import type { CloneSlot, NamFile } from './types';

export const KIND = { EMPTY: 0, ORIGINAL: 1, NAM: 2, NAM48: 3, BROKEN: 4 };

/** engine: 'nam' the A2-Lite network on waveod's NAM engine, 'clone' converted for the original's Clone */
export interface NamDraft { slot: number; file: NamFile; name: string; native: boolean; engine: 'nam' | 'clone'; busy: boolean }

class Nam {
  slots = $state<CloneSlot[] | null>(null);
  picked = $state(0);
  draft = $state<NamDraft | null>(null);

  get slot() { return this.slots?.[this.picked] ?? null; }

  async enter() {
    try {
      this.slots = await studio.call((b) => b.clones());
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** a .nam for the picked slot: what it is, before anything is written */
  async choose() {
    try {
      const f = await studio.call((b) => b.pickNam());
      if (!f) return;
      if (!f.lite && !f.convert) {
        studio.say(`${f.file} (${f.arch}) não tem a rede A2-Lite, e o conversor para o Clone não está compilado`, 'error');
        return;
      }
      this.draft = { slot: this.picked, file: f, name: f.name.slice(0, 16), native: false,
                     engine: f.lite ? 'nam' : 'clone', busy: false };
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  async write() {
    const d = this.draft;
    if (!d || !d.name.trim()) return;
    d.busy = true;
    try {
      this.slots = d.engine === 'clone'
        ? await studio.callLong(`Convertendo ${d.file.file} para o Clone`, (b) => b.writeCloneFit(d.slot, d.file.path, d.name))
        : await studio.call((b) => b.writeNam(d.slot, d.file.path, d.name, d.native));
      this.draft = null;
      studio.say(`Slot ${d.slot + 1}: ${d.name.trim()} (${this.slots[d.slot]?.kind_text ?? ''})`);
    } catch (e) {
      d.busy = false;
      studio.say(errorText(e), 'error');
    }
  }
}

export const nam = new Nam();
