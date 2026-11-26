// The drums screen's state: MEUS RITMOS, the 20 places of the pedal's own drum patterns (protocol 7), what each
// plays (read once per change), and a MIDI file on its way in.

import { errorText, type Backend } from './api';
import { studio } from './store.svelte';
import type { DrumPattern, DrumState, MidiPreview } from './types';

const POLL_MS = 1500;               // the pedal's own buttons start and stop the drums too

/** the 13 sounds of the pedal's bank (drums.py SOUNDS), top to bottom as a drum score has them */
export const SOUNDS = [
  'Bumbo eletrônico', 'Bumbo', 'Aro / clique', 'Caixa eletrônica', 'Palmas', 'Caixa', 'Chimbal fechado',
  'Chimbal aberto', 'Prato de ataque', 'Ride', 'Splash', 'Cowbell', 'Pandeirola',
];
export const ROW_ORDER = [8, 10, 9, 7, 6, 12, 11, 4, 2, 5, 3, 1, 0];
/** a colour per family: cymbals, hats, percussion, snares, kicks */
export const SOUND_COLOR = (s: number) =>
  s === 8 || s === 9 || s === 10 ? '#73BDFF' : s === 6 || s === 7 ? '#36D2BA' : s === 11 || s === 12 ? '#C4A4FE'
  : s === 0 || s === 1 ? '#FF958D' : '#F2AF48';

export interface ImportDraft {
  place: number;
  preview: MidiPreview;
  name: string;
  busy: boolean;
}

class Drums {
  state = $state<DrumState | null>(null);
  picked = $state(0);
  patterns = $state<Record<number, DrumPattern>>({});
  draft = $state<ImportDraft | null>(null);
  renaming = $state(false);
  private polling = false;

  get slot() { return this.state?.slots[this.picked] ?? null; }

  /** the screen is shown: read the places, keep them current while it is */
  async enter() {
    await this.read();
    if (this.polling) return;
    this.polling = true;
    const tick = async () => {
      if (studio.screen !== 'drums') { this.polling = false; return; }
      if (!this.draft && !studio.dialog && !document.hidden && studio.online) await this.read(true);
      setTimeout(tick, POLL_MS);
    };
    setTimeout(tick, POLL_MS);
  }

  private async read(quiet = false) {
    try {
      this.take(await studio.call((b) => b.drums()));
    } catch (e) {
      if (!quiet) studio.say(errorText(e), 'error');
    }
  }

  /** a new state from the pedal; the patterns of places that changed are read again */
  private take(st: DrumState) {
    const before = this.state;
    if (before) {
      const keep = { ...this.patterns };
      st.slots.forEach((s, i) => {
        const o = before.slots[i];
        if (!s || !o || s.name !== o.name || s.events !== o.events || s.once !== o.once || s.bpm !== o.bpm) delete keep[i];
      });
      this.patterns = keep;
    }
    this.state = st;
    void this.pattern(this.picked);
  }

  pick(i: number) {
    if (i < 0 || i >= (this.state?.places ?? 20)) return;
    this.picked = i;
    this.renaming = false;
    void this.pattern(i);
  }

  async pattern(i: number) {
    if (this.patterns[i] || !this.state?.slots[i]) return;
    try {
      const p = await studio.call((b) => b.drumPattern(i));
      if (this.state?.slots[i]?.name === p.name) this.patterns = { ...this.patterns, [i]: p };
    } catch (e) {
      if (i === this.picked) studio.say(errorText(e), 'error');
    }
  }

  private async change(fn: (b: Backend) => Promise<DrumState>, done?: string) {
    try {
      this.take(await studio.call(fn));
      if (done) studio.say(done);
      return true;
    } catch (e) {
      studio.say(errorText(e), 'error');
      return false;
    }
  }

  // ---------------------------------------------------------------- playing
  isPlaying(i: number) { return !!this.state?.playing && this.state.playing_place === i; }

  async toggle(i: number) {
    const on = !this.isPlaying(i);
    await this.change((b) => b.drumPlay(i, on));
  }

  async stop() {
    const st = this.state;
    if (!st?.playing) return;
    await this.change((b) => b.drumPlay(st.playing_place, false));
  }

  // ---------------------------------------------------------------- changes
  async rename(i: number, name: string) {
    const s = this.state?.slots[i];
    if (!s || !name.trim() || name.trim().toUpperCase() === s.name) return;
    await this.change((b) => b.drumRename(i, name), `M${i + 1} renomeado`);
  }

  async empty(i: number) {
    const s = this.state?.slots[i];
    if (!s) return;
    const id = await studio.ask({
      title: `Esvaziar M${i + 1}?`,
      text: `Apaga do pedal o ritmo “${s.name}”. Para tê-lo de volta é preciso importar o MIDI de novo.`,
      actions: [{ id: 'go', label: 'Esvaziar', kind: 'danger' }, { id: 'cancel', label: 'Cancelar' }],
    });
    if (id === 'go') await this.change((b) => b.drumEmpty(i), `M${i + 1} vazio`);
  }

  /** a MIDI file picked for place i (the picked one, or the first empty when the picked is in use and `free`) */
  async importFor(i: number) {
    let preview: MidiPreview | null;
    try {
      preview = await studio.call((b) => b.pickMidi());
    } catch (e) {
      studio.say(errorText(e), 'error');
      return;
    }
    if (preview) this.draft = { place: i, preview, name: preview.name, busy: false };
  }

  /** the draft converted again with another kit or ending */
  async reconvert(kit: string, once: boolean) {
    const d = this.draft;
    if (!d) return;
    d.busy = true;
    try {
      const p = await studio.call((b) => b.midiPreview(d.preview.path, kit, once));
      if (this.draft === d) d.preview = p;
    } catch (e) {
      studio.say(errorText(e), 'error');
    } finally {
      d.busy = false;
    }
  }

  async commit() {
    const d = this.draft;
    if (!d) return;
    d.busy = true;
    const { place, preview: p, name } = d;
    const ok = await this.change((b) => b.drumImport(place, p.path, name, p.kit, p.once),
                                 `M${place + 1}: ${name.trim().toUpperCase() || p.name} (${p.bars} ${p.bars === 1 ? 'compasso' : 'compassos'})`);
    if (ok) {
      this.draft = null;
      this.pick(place);
    } else {
      d.busy = false;
    }
  }
}

/** what the pedal's font shows: upper-case ASCII, at most 16 characters (drums.clean_name) */
export function pedalName(s: string) {
  return s.normalize('NFKD').replace(/[̀-ͯ]/g, '').toUpperCase().replace(/[^A-Z0-9 .,+\-#/()&]/g, ' ')
    .replace(/\s+/g, ' ').trimStart().slice(0, 16);
}

export const drums = new Drums();
