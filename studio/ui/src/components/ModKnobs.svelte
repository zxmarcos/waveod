<!--
  A modulator's settings as the grid's knobs (Knob.svelte), the ones its type uses, in the pedal's order:
    LFO        Sincronia (LIVRE or a division: a list), Velocidade, Fase inicial
    ENVELOPE   Sensibilidade, Ataque, Retorno
    SEQUÊNCIA  Sincronia, Velocidade, Passos, Deslize
  Velocidade, Ataque and Retorno turn on a log scale (0,05..20 Hz, 1..2000 / 10..5000 ms: a linear one would spend
  the knob on the slow end). While synced, Velocidade shows the rate the division gives at the preset's tempo, dimmed;
  turning it frees the rate from there. Double click = the pedal's starting value (modDefault).
-->
<script lang="ts">
  import Knob from './Knob.svelte';
  import * as C from '../lib/controls';
  import * as W from '../lib/modwave';
  import type { ModSettings, Param } from '../lib/types';

  let { m, bpm }: { m: ModSettings; bpm: number } = $props();

  const D = C.modDefault('lfo');
  const LOG = 1000;                   // a log knob's positions
  const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
  const comma = (v: number, d: number) => v.toFixed(d).replace('.', ',');

  function param(name: string, value: number, min: number, max: number, step: number, def: number, text: string,
                 options: string[] = []): Param {
    return { k: 0, knob: 0, synced: false, scene: false, min, max, step, default: def, value, name, text, options };
  }
  /** a log knob: the position of v in lo..hi, and back */
  const pos = (v: number, lo: number, hi: number) => (LOG * Math.log(clamp(v, lo, hi) / lo)) / Math.log(hi / lo);
  const val = (u: number, lo: number, hi: number) => lo * Math.pow(hi / lo, clamp(u, 0, LOG) / LOG);

  const hz = (v: number) => (v < 10 ? `${comma(v, 2)} Hz` : `${comma(v, 1)} Hz`);
  const ms = (v: number) => (v >= 1000 ? `${comma(v / 1000, 2)} s` : `${Math.round(v)} ms`);
  /** a rate in Hz as the pedal keeps it: hundredths */
  const rate = (v: number) => Math.round(clamp(v, 0.05, 20) * 100) / 100;
  const synced = $derived(m.sync > 0);

  const knobs = $derived.by(() => {
    const out: { p: Param; set: (v: number) => void; reset: () => void; dim?: boolean }[] = [];
    const sync = () => out.push({
      p: param('Sincronia', m.sync, 0, C.DIVISIONS.length - 1, 1, 0, C.DIVISIONS[m.sync] ?? '?', C.DIVISIONS),
      set: (v) => (m.sync = Math.round(clamp(v, 0, C.DIVISIONS.length - 1))), reset: () => (m.sync = 0),
    });
    const speed = () => {
      const now = synced ? 1 / W.period(m, bpm) : m.rate;
      out.push({
        p: param('Velocidade', pos(now, 0.05, 20), 0, LOG, 1, pos(D.rate, 0.05, 20), hz(now)),
        set: (u) => { m.rate = rate(val(u, 0.05, 20)); m.sync = 0; },
        reset: () => { m.rate = D.rate; m.sync = 0; },
        dim: synced,
      });
    };
    if (m.type === 'lfo') {
      sync();
      speed();
      out.push({ p: param('Fase inicial', m.phase, 0, 95, 1, 0, `${m.phase}%`),
                 set: (v) => (m.phase = Math.round(clamp(v, 0, 95))), reset: () => (m.phase = 0) });
    } else if (m.type === 'env') {
      out.push(
        { p: param('Sensibilidade', m.sens, -20, 40, 1, D.sens, `${m.sens > 0 ? '+' : ''}${m.sens} dB`),
          set: (v) => (m.sens = Math.round(clamp(v, -20, 40))), reset: () => (m.sens = D.sens) },
        { p: param('Ataque', pos(m.attack, 1, 2000), 0, LOG, 1, pos(D.attack, 1, 2000), ms(m.attack)),
          set: (u) => (m.attack = Math.round(val(u, 1, 2000))), reset: () => (m.attack = D.attack) },
        { p: param('Retorno', pos(m.release, 10, 5000), 0, LOG, 1, pos(D.release, 10, 5000), ms(m.release)),
          set: (u) => (m.release = Math.round(val(u, 10, 5000))), reset: () => (m.release = D.release) },
      );
    } else if (m.type === 'seq') {
      sync();
      speed();
      out.push(
        { p: param('Passos', m.steps, 1, C.MOD_STEPS, 1, D.steps, `${m.steps}`),
          set: (v) => (m.steps = Math.round(clamp(v, 1, C.MOD_STEPS))), reset: () => (m.steps = D.steps) },
        { p: param('Deslize', m.glide, 0, 100, 1, 0, `${m.glide}%`),
          set: (v) => (m.glide = Math.round(clamp(v, 0, 100))), reset: () => (m.glide = 0) },
      );
    }
    return out;
  });
</script>

{#if knobs.length}
  <div class="knobs">
    {#each knobs as k (k.p.name)}
      <div class="slot" class:dim={k.dim} title={k.dim ? 'A sincronia manda: girar solta a velocidade' : undefined}>
        <Knob param={k.p} color={k.dim ? 'var(--dim)' : 'var(--accent)'} onchange={k.set} onreset={k.reset} onhold={() => {}} />
      </div>
    {/each}
  </div>
{/if}

<style>
  .knobs { display: flex; flex-wrap: wrap; gap: 4px 6px; }
  .slot.dim :global(.text) { color: var(--off); }
</style>
