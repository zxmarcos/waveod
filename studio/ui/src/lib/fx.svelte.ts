// The effects screen's state: waveod's effect library on the pedal (protocol 5), the module shown, and module
// files on their way in.

import { errorText } from './api';
import { num, t } from './i18n.svelte';
import { studio } from './store.svelte';
import type { FxLibrary, FxModule, ModulePick } from './types';

class Effects {
  lib = $state<FxLibrary | null>(null);
  picked = $state<number | null>(null);         // module id
  picks = $state<ModulePick[] | null>(null);    // files chosen, before installing
  private readFor = -1;                         // the connection the library was read on

  get module(): FxModule | null {
    return this.lib?.modules.find((m) => m.id === this.picked) ?? null;
  }

  /** the screen is shown: the library read once per connection (it changes only through this screen) */
  async enter() {
    const c = studio.link?.connections ?? 0;
    if (this.lib && this.readFor === c) return;
    try {
      this.take(await studio.callLong(t('fx.reading'), (b) => b.library()));
      this.readFor = c;
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  private take(lib: FxLibrary) {
    if (this.lib) studio.catalogs = {};       // the grid's browser lists the modules: read its menus again
    this.lib = lib;
    if (!lib.modules.some((m) => m.id === this.picked)) this.picked = lib.modules[0]?.id ?? null;
  }

  async pick() {
    try {
      const p = await studio.call((b) => b.pickModules());
      if (p?.length) this.picks = p;
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** the module installed with the same id, if any (a file replaces it) */
  installed(id: number | undefined) {
    return id === undefined ? null : this.lib?.modules.find((m) => m.id === id) ?? null;
  }

  async install() {
    const good = (this.picks ?? []).filter((p) => !p.error);
    if (!good.length) return;
    this.picks = null;
    try {
      this.take(await studio.callLong(t('fx.writing'), (b) => b.installModules(good.map((p) => p.path))));
      this.picked = good[good.length - 1].id ?? this.picked;
      studio.say(t('fx.installed', { names: good.map((p) => p.name).join(', ') }));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  async remove(m: FxModule) {
    const id = await studio.ask({
      title: t('fx.remove_title', { name: m.name }),
      text: t('fx.remove_text', { name: m.name }),
      actions: [{ id: 'go', label: t('common.remove'), kind: 'danger' }, { id: 'cancel', label: t('common.cancel') }],
    });
    if (id !== 'go') return;
    try {
      this.take(await studio.callLong(t('fx.writing'), (b) => b.removeModules([m.id])));
      studio.say(t('fx.removed', { name: m.name }));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }
}

/** a value as the pedal writes it: the parameter's printf format ('%.1f dB', 'x%.2f', '%.0f%%') */
export function fxValue(unit: string, v: number) {
  if (!unit) return Number.isInteger(v) ? String(v) : v.toFixed(2);
  return unit.replace(/%\.?(\d*)f/, (_, n) => v.toFixed(n === '' ? 6 : Number(n))).replace('%%', '%');
}

export function kb(n: number) {
  return n < 1024 ? `${n} B` : n < 100 * 1024 ? `${num(n / 1024, 1)} KB` : `${Math.round(n / 1024)} KB`;
}

export const effects = new Effects();
