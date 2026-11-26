// The GLOBAL tab's state: the pedal's settings and global EQ (protocol 9). A change shows at once and goes to the
// pedal coalesced (one call at a time, the newest value of each setting); the pedal answers with all of them.

import { errorText } from './api';
import { EQ_LEVEL, STAGES } from './globalset';
import { studio } from './store.svelte';
import type { Setting, SettingChanges, SettingsReply } from './types';

class Globals {
  values = $state<Record<string, Setting> | null>(null);
  eq = $state<Setting[]>([]);
  error = $state('');                    // why there is nothing to show (an older waveod)
  stage = $state(2);                     // the EQ stage picked
  section = $state('eq');                // the section shown (the sidebar): 'eq' or a card's id
  sending = $state(false);
  private pending: Record<string, number> = {};         // name, or 'eq.k'
  private flat = $state<number[] | null>(null);         // the EQ before FLAT (VOLTAR puts it back)
  get canUndoFlat() { return this.flat !== null; }

  async enter() {
    try {
      this.take(await studio.call((b) => b.settings()));
      this.error = '';
    } catch (e) {
      this.error = errorText(e);
    }
  }

  private take(r: SettingsReply) {
    const { eq, ...rest } = r;
    const values = rest as Record<string, Setting>;
    for (const k of Object.keys(this.pending)) {         // still on its way: what the user set stays
      if (k.startsWith('eq.')) eq[Number(k.slice(3))].value = this.pending[k];
      else if (values[k]) values[k].value = this.pending[k];
    }
    this.values = values;
    this.eq = eq;
  }

  value(name: string) { return this.values?.[name]?.value ?? 0; }

  // ---------------------------------------------------------------- SISTEMA: the CPU's temperature (model/sysmon.h)
  /** degrees C now, highest and lowest since the pedal started; null = unknown (no reading, or an older waveod) */
  temp(name: 'cpu_temp' | 'cpu_temp_max' | 'cpu_temp_min'): number | null {
    const v = this.values?.[name]?.value as number | null | undefined;
    return typeof v === 'number' ? v : null;
  }
  get hasTemp() { return !!this.values && 'cpu_temp' in this.values; }
  history = $state<{ t: number; v: number }[]>([]);     // readings while SISTEMA is open (this window)
  private watching = 0;

  /** SISTEMA open: the settings read again every two seconds (the temperature moves; the rest stays as it is) */
  async watchSystem() {
    const ticket = ++this.watching;
    while (ticket === this.watching && this.section === 'system' && studio.screen === 'global' && studio.online) {
      if (!this.sending && !Object.keys(this.pending).length) {
        try {
          this.take(await studio.call((b) => b.settings()));
          const t = this.temp('cpu_temp');
          if (t !== null) this.history = [...this.history, { t: Date.now(), v: t }].slice(-900);    // 30 min
        } catch { /* the next try */ }
      }
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
  stopSystem() { this.watching++; }

  set(name: string, v: number) {
    const s = this.values?.[name];
    if (!s || s.ro) return;
    v = Math.min(s.max, Math.max(s.min, Math.round(v)));
    if (s.value === v && !(name in this.pending)) return;
    s.value = v;
    this.queue(name, v);
  }

  setEq(k: number, v: number) {
    const s = this.eq[k];
    if (!s) return;
    v = Math.min(s.max, Math.max(s.min, v));
    v = s.step >= 1 ? Math.round(v) : Math.round(v * 100) / 100;
    if (s.value === v) return;
    s.value = v;
    this.flat = null;
    this.queue(`eq.${k}`, v);
  }

  /** FLAT: every gain to 0 dB, both cuts off, the level to 50 (scr_eq.c); VOLTAR puts the EQ back */
  flatten() {
    const before = this.eq.map((s) => s.value);
    for (const st of STAGES) {
      if (st.g >= 0) this.setEq(st.g, 0);
      if (st.kind === 'lowcut' || st.kind === 'highcut') this.setEq(st.on, 0);
    }
    this.setEq(EQ_LEVEL, 50);
    this.flat = before;
  }

  undoFlat() {
    const f = this.flat;
    if (!f) return;
    f.forEach((v, k) => this.setEq(k, v));
    this.flat = null;
  }

  private queue(key: string, v: number) {
    this.pending[key] = v;
    if (!this.sending) void this.flush();
  }

  private async flush() {
    this.sending = true;
    try {
      while (Object.keys(this.pending).length) {
        const batch = this.pending;
        this.pending = {};
        const changes: SettingChanges = {};
        const eq: Record<number, number> = {};
        for (const [k, v] of Object.entries(batch)) {
          if (k.startsWith('eq.')) eq[Number(k.slice(3))] = v;
          else changes[k] = v;
        }
        if (Object.keys(eq).length) changes.eq = eq;
        try {
          this.take(await studio.call((b) => b.setSettings(changes)));
        } catch (e) {
          studio.say(errorText(e), 'error');
          await this.enter();                           // what the pedal really has
        }
      }
    } finally {
      this.sending = false;
    }
  }
}

export const globals = new Globals();
