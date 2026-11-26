// The pedal screen's state: what the pedal is and holds, and its latency test (protocol 4).

import { errorText } from './api';
import { studio } from './store.svelte';
import type { DumpDone, FirmwareFile, Latency, PedalInfo } from './types';

export const ROUTES = [
  { route: 0, label: 'Saída L', cable: 'um cabo da saída L na entrada (INPUT)' },
  { route: 1, label: 'Saída R', cable: 'um cabo da saída R na entrada (INPUT)' },
  { route: 2, label: 'Send → Return', cable: 'um cabo do FX SEND no FX RETURN' },
  { route: 3, label: 'Preset (sem cabo)', cable: '' },
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
      studio.ask({ title: 'Arquivo de firmware', text: errorText(e), actions: [{ id: 'ok', label: 'Fechar' }] });
    }
  }

  /** the firmware into the pedal; the result is told in a dialog (the pedal is away meanwhile: no toasts) */
  async update() {
    const f = this.fw;
    if (!f || !this.agree) return;
    this.fw = null;
    try {
      const r = await studio.callLong('Gravando o firmware', (b) => b.updateFirmware(f.path));
      const m = Math.floor(r.seconds / 60), s = Math.round(r.seconds % 60);
      await studio.ask({
        title: 'Firmware gravado',
        text: `${f.file} (${r.version}) gravado em ${m ? `${m} min ${s} s` : `${s} s`}. O pedal reinicia com ele e o Studio ` +
              `conecta sozinho quando ele voltar. Log: ${r.log}`,
        actions: [{ id: 'ok', label: 'Fechar', kind: 'primary' }],
      });
    } catch (e) {
      const id = await studio.ask({
        title: 'A gravação não terminou',
        text: `${errorText(e)}. O pedal deve estar no modo de atualização (o bootloader nunca é regravado): dá para ` +
              'tentar de novo. Se ele não responder, ligue-o segurando a combinação de footswitches do modo de atualização.',
        actions: [{ id: 'retry', label: 'Tentar de novo', kind: 'primary' }, { id: 'close', label: 'Fechar' }],
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
        title: 'Medir a latência?',
        text: `Ligue ${r.cable}. O teste toca um clique nessa saída: desligue o amplificador e o fone antes.`,
        actions: [{ id: 'go', label: 'Medir', kind: 'primary' }, { id: 'cancel', label: 'Cancelar' }],
      });
      if (id !== 'go') return;
    }
    try {
      this.latency = await studio.callLong('Medindo a latência', (b) => b.measureLatency(r.route));
    } catch (e) {
      studio.say(errorText(e), 'error');
    }
  }
  /** a read-only copy of the flash into a file (the 8 MB, or the bootloader); a whole one is summarised */
  async makeDump() {
    const kind = this.dumpKind;
    try {
      const r = await studio.callLong(kind === 'all' ? 'Copiando a flash inteira' : 'Copiando o bootloader',
                                      (b) => b.dumpFlash(kind));
      if (!r) return;
      if (r.summary) this.dump = r;
      studio.say(`${r.file}: ${Math.round(r.size / 1024)} KB em ${Math.round(r.seconds)} s`);
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
      studio.ask({ title: 'Cópia da flash', text: errorText(e), actions: [{ id: 'ok', label: 'Fechar' }] });
    }
  }
}

export const pedal = new Pedal();