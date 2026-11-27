// What a modulator does over time, computed as the pedal's engine does it (src/engine/modulators.c): the LFO shapes
// (the random ones from the same generator and seed), the sequencer's steps and glides, the envelope follower's
// attack / release / sensitivity, the synced rates. The drawings of the controls editor (ModWave.svelte) use it.

import { divisions } from './controls';
import { num, t } from './i18n.svelte';
import type { ModSettings } from './types';

export const RATE = 44100;
export const BLOCK = 64;

/** beats of the note divisions, sync 1..11 (engine/modulators.c division_beats) */
export const DIVISION_BEATS = [4, 2, 3, 4 / 3, 1, 1.5, 2 / 3, 0.5, 0.75, 1 / 3, 0.25];

/** seconds of an LFO cycle or a SEQ step at the tempo `bpm` */
export function period(m: ModSettings, bpm: number): number {
  if (m.sync >= 1 && m.sync <= 11) {
    const beatMs = (60000 / Math.max(bpm, 1)) & 0xffff;
    const ms = beatMs * DIVISION_BEATS[m.sync - 1];
    return ms > 1 ? ms / 1000 : 1;
  }
  return 1 / Math.min(Math.max(m.rate, 0.01), 50);
}

/** the same xorshift as the engine's random01 */
export class Random {
  private x: number;
  constructor(seed = 0x1234567) { this.x = seed >>> 0 || 0x9e3779b9; }
  next(): number {
    let x = this.x;
    x ^= x << 13; x >>>= 0;
    x ^= x >>> 17;
    x ^= x << 5; x >>>= 0;
    this.x = x;
    return (x >>> 8) / 16777216;
  }
}

/** the random values of cycles 0..n-1 after a load: [from, to] of each (the engine's mod_reset, then a new `to` per
 *  cycle) */
export function randomCycles(n: number): [number, number][] {
  const r = new Random();
  let from = r.next(), to = r.next();
  const out: [number, number][] = [];
  for (let i = 0; i < n; i++) {
    out.push([from, to]);
    from = to;
    to = r.next();
  }
  return out;
}

/** an LFO's value at p (0..1) of a cycle whose random values are [from, to] */
export function lfoAt(shape: string, p: number, rnd: [number, number] = [0, 0]): number {
  switch (shape) {
    case 'triangle': return p < 0.5 ? 2 * p : 2 - 2 * p;
    case 'square': return p < 0.5 ? 1 : 0;
    case 'saw up': return p;
    case 'saw down': return 1 - p;
    case 'random': return rnd[1];
    case 'smooth': return rnd[0] + (rnd[1] - rnd[0]) * (0.5 - 0.5 * Math.cos(Math.PI * p));
    default: return 0.5 - 0.5 * Math.cos(2 * Math.PI * p);             // a sine from its bottom
  }
}

export const isRandom = (shape: string) => shape === 'random' || shape === 'smooth';

/** points (x 0..1 across, y 0..1 up) of `cycles` cycles of an LFO, the jumps upright */
export function lfoPoints(shape: string, cycles: number, n = 240): [number, number][] {
  const rnd = randomCycles(cycles);
  const corners: Record<string, number[]> = {
    triangle: [0, 0, 0.5, 1, 1, 0], square: [0, 1, 0.5, 1, 0.5, 0, 1, 0], 'saw up': [0, 0, 1, 1], 'saw down': [0, 1, 1, 0],
  };
  const out: [number, number][] = [];
  const c = corners[shape];
  if (c) {
    for (let k = 0; k < cycles; k++)
      for (let i = 0; i < c.length; i += 2) out.push([(k + c[i]) / cycles, c[i + 1]]);
    return out;
  }
  if (shape === 'random') {
    for (let k = 0; k < cycles; k++) out.push([k / cycles, rnd[k][1]], [(k + 1) / cycles, rnd[k][1]]);
    return out;
  }
  for (let i = 0; i <= n; i++) {
    const t = (i / n) * cycles, k = Math.min(Math.floor(t), cycles - 1);
    out.push([i / n, lfoAt(shape, t - k, rnd[k])]);
  }
  return out;
}

/** a sequence's value in step i at p (0..1) of it; `first`: the very first step after a load (no glide into it) */
export function seqAt(m: ModSettings, i: number, p: number, first = false): number {
  const n = Math.min(Math.max(m.steps, 1), m.step.length);
  const k = ((i % n) + n) % n, prev = (k + n - 1) % n;
  const v = Math.min(m.step[k], 100) / 100, g = Math.min(m.glide, 100) / 100;
  if (g > 0 && p < g && !first) {
    const u = Math.min(m.step[prev], 100) / 100;
    return u + (v - u) * (p / g);
  }
  return v;
}

/** points of one pass through the sequence (x 0..1, y 0..1), glides as ramps */
export function seqPoints(m: ModSettings): [number, number][] {
  const n = Math.min(Math.max(m.steps, 1), m.step.length), g = Math.min(m.glide, 100) / 100;
  const out: [number, number][] = [];
  for (let i = 0; i < n; i++) {
    const u = Math.min(m.step[(i + n - 1) % n], 100) / 100, v = Math.min(m.step[i], 100) / 100;
    out.push([i / n, g > 0 ? u : v]);
    if (g > 0) out.push([(i + g) / n, v]);
    out.push([(i + 1) / n, v]);
  }
  return out;
}

/** The guitar the envelope drawing follows over `seconds`: a strong note and a soft one, each muted after a while
 *  (so the release shows), as the level of each 64-sample block (0..1 of full scale). */
export function pluck(seconds: number): number[] {
  const blocks = Math.round((seconds * RATE) / BLOCK), out: number[] = [];
  const notes = [{ at: 0.1, peak: 0.5, tau: 0.6, mute: 0.5 }, { at: 0.85, peak: 0.08, tau: 0.6, mute: 1.2 }];
  for (let b = 0; b < blocks; b++) {
    const t = (b * BLOCK) / RATE;
    let a = 0;
    for (const n of notes) {
      if (t < n.at) continue;
      const muted = t > n.mute ? Math.exp(-(t - n.mute) / 0.004) : 1;
      a = Math.max(a, n.peak * Math.exp(-(t - n.at) / n.tau) * muted);
    }
    out.push(a);
  }
  return out;
}

/** the envelope modulator's value for each block of `levels` (engine/modulators.c mod_step, MOD_ENV) */
export function envelope(m: ModSettings, levels: number[]): number[] {
  let env = 0;
  return levels.map((peak) => {
    const ms = peak > env ? m.attack : m.release;
    const c = ms > 0 ? Math.exp(-(1000 * BLOCK) / (ms * RATE)) : 0;
    env = peak + (env - peak) * c;
    const db = (env > 1e-9 ? 20 * Math.log10(env) : -180) + m.sens;
    return Math.min(1, Math.max(0, (db + 60) / 60));
  });
}

/** the level 0..1 a guitar level drawn on the envelope's 0..1 scale stands for (-60..0 dB, as the modulator reads it
 *  without sensitivity) */
export const levelPos = (a: number) => Math.min(1, Math.max(0, ((a > 1e-9 ? 20 * Math.log10(a) : -180) + 60) / 60));

/** "1.00 Hz · cycle of 1.00 s", "1/8 at 120 BPM · 4.00 Hz · step of 250 ms" (in the page's language) */
export function rateText(m: ModSettings, bpm: number): string {
  const s = period(m, bpm), hz = 1 / s;
  const what = t(m.type === 'seq' ? 'mod.word_step' : 'mod.word_cycle');
  const len = s >= 1 ? `${num(s, 2)} s` : `${Math.round(s * 1000)} ms`;
  return m.sync ? t('mod.rate_synced', { div: divisions()[m.sync], bpm, hz: num(hz, 2), what, len })
    : t('mod.rate_free', { hz: num(hz, 2), what, len });
}

/** an SVG path through points scaled into a w x h box (y up) */
export function path(pts: [number, number][], w: number, h: number, x0 = 0, y0 = 0): string {
  return pts.map(([x, y], i) => `${i ? 'L' : 'M'}${(x0 + x * w).toFixed(1)} ${(y0 + (1 - y) * h).toFixed(1)}`).join(' ');
}
