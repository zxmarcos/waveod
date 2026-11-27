// The pedal screen's state: what the pedal is and holds, and its latency test (protocol 4).

import { errorText } from './api';
import { t } from './i18n.svelte';
import { studio } from './store.svelte';
import type { DumpDone, FirmwareFile, Latency, PedalInfo } from './types';

// getters: the texts follow the language
export const ROUTES = [
  { route: 0, get label() { return t('pedal.route_l'); }, get cable() { return t('pedal.cable_l'); } },
  { route: 1, get label() { return t('pedal.route_r'); }, get cable() { return t('pedal.cable_r'); } },
  { route: 2, label: 'Send → Return', get cable() { return t('pedal.cable_send'); } },
  { route: 3, get label() { return t('pedal.route_preset'); }, cable: '' },
];

class Pedal {
  info = $state<PedalInfo | null>(null);
  latency = $state<Latency | null>(null);
  route = $state(0);
  fw = $state<FirmwareFile | null>(null);        // a firmware picked, its sheet open
  agree = $state(false);
  dumpKind = $state<'all' | 'boot'>('all');
  dump = $state<DumpDone | null>(null);          // the last dump made (or opened) in this window

  /** a firmware .bin, checked; opens the sheet (also from the waiting screen: the pedal may be in its bootloader) */
  async pickFirmware() {
    try {
      const f = await studio.call((b) => b.pickFirmware());
      if (f) {
        this.fw = f;
        this.agree = false;
      }
    } catch (e) {
      studio.ask({ title: t('pedal.fw_file'), text: errorText(e), actions: [{ id: 'ok', label: t('pedal.close') }] });
    }
  }

  /** the firmware into the pedal; the result is told in a dialog (the pedal is away meanwhile: no toasts) */
  async update() {
    const f = this.fw;
    if (!f || !this.agree) return;
    this.fw = null;
    try {
      const r = await studio.callLong(t('pedal.writing_fw'), (b) => b.updateFirmware(f.path));
      const m = Math.floor(r.seconds / 60), s = Math.round(r.seconds % 60);
      await studio.ask({
        title: t('pedal.fw_done'),
        text: t('pedal.fw_done_text', { file: f.file, version: r.version, time: m ? `${m} min ${s} s` : `${s} s`, log: r.log }),
        actions: [{ id: 'ok', label: t('pedal.close'), kind: 'primary' }],
      });
    } catch (e) {
      const id = await studio.ask({
        title: t('pedal.fw_failed'),
        text: t('pedal.fw_failed_text', { error: errorText(e) }),
        actions: [{ id: 'retry', label: t('pedal.retry'), kind: 'primary' }, { id: 'close', label: t('pedal.close') }],
      });
      if (id === 'retry') {
        this.fw = f;
        this.agree = true;
        await this.update();
      }
    }
  }

  async enter() {
    try {
      this.info = await studio.call((b) => b.pedalInfo());
      const l = await studio.call((b) => b.latency());
      if (l.status !== 0) {
        this.latency = l;
        this.route = l.route;
      }
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** a cable route plays a click on that output: asked first */
  async measure() {
    const r = ROUTES[this.route];
    if (r.cable) {
      const id = await studio.ask({
        title: t('pedal.measure_q'),
        text: t('pedal.measure_text', { cable: r.cable }),
        actions: [{ id: 'go', label: t('pedal.measure'), kind: 'primary' }, { id: 'cancel', label: t('common.cancel') }],
      });
      if (id !== 'go') return;
    }
    try {
      this.latency = await studio.callLong(t('pedal.measuring'), (b) => b.measureLatency(r.route));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }
  /** a read-only copy of the flash into a file (the 8 MB, or the bootloader); a whole one is summarised */
  async makeDump() {
    const kind = this.dumpKind;
    try {
      const r = await studio.callLong(t(kind === 'all' ? 'pedal.copying_all' : 'pedal.copying_boot'),
                                      (b) => b.dumpFlash(kind));
      if (!r) return;
      if (r.summary) this.dump = r;
      studio.say(t('pedal.dumped', { file: r.file, kb: Math.round(r.size / 1024), s: Math.round(r.seconds) }));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }

  /** a dump saved before, summarised (no pedal needed) */
  async openDump() {
    try {
      const r = await studio.call((b) => b.pickDump());
      if (r) this.dump = r;
    } catch (e) {
      studio.ask({ title: t('pedal.dump_title'), text: errorText(e), actions: [{ id: 'ok', label: t('pedal.close') }] });
    }
  }
}

export const pedal = new Pedal();