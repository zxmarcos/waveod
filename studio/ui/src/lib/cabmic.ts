// The virtual mic of waveod's Cab / IR engine (src/engine/cabmic.c, the pedal's MIC screen ui/screens/scr_mic.c):
// the knobs (POSITION -100..100, DISTANCE -100..100, ANGLE 0..45, TYPE 0 = as captured), the steps they stand for,
// and the change in the response they make, computed as the firmware computes it.

import { CABMIC } from './cabmic_tables';
import { t } from './i18n.svelte';

export const POSITIONS = 4, DISTANCES = 6, REF = 1, MAX_DB = 18;
export const TYPES = CABMIC.typeNames.length;

export interface Mic { position: number; distance: number; angle: number; type: number }

/** -100 -> step 0, 0 -> the step of the IR as captured, +100 -> the last step (cabmic_step) */
export function step(knob: number, steps: number): number {
  const s = knob < 0 ? REF + (knob / 100) * REF : REF + (knob / 100) * (steps - 1 - REF);
  return Math.max(0, Math.min(steps - 1, s));
}

/** the knob of a step (cabmic_knob) */
export function knob(s: number, steps: number): number {
  const k = s < REF ? ((s - REF) / REF) * 100 : ((s - REF) / (steps - 1 - REF)) * 100;
  return Math.max(-100, Math.min(100, k));
}

export const micType = (t: number) => { const i = t > 0 ? Math.floor(t + 0.5) : 0; return i > TYPES ? 0 : i; };

function rowAt(rows: number[][], steps: number, s: number, band: number) {
  const i = Math.floor(s);
  if (i >= steps - 1) return rows[steps - 1][band];
  const t = s - i;
  return rows[i][band] * (1 - t) + rows[i + 1][band] * t;
}

/** the change the mic makes, dB per band (cabmic_curve) */
export function curve(m: Mic): number[] {
  const sp = step(m.position, POSITIONS), sd = step(m.distance, DISTANCES);
  const an = Math.max(0, Math.min(1, m.angle / 45));
  const t = micType(m.type);
  const pos = t ? CABMIC.typePosition[t - 1] : CABMIC.position;
  const dist = t ? CABMIC.typeDistance[t - 1] : CABMIC.distance;
  const ang = t ? CABMIC.typeAngle[t - 1] : CABMIC.angle;
  return CABMIC.bands.map((_, k) => {
    const v = rowAt(pos, POSITIONS, sp, k) - pos[REF][k] + rowAt(dist, DISTANCES, sd, k) - dist[REF][k] +
              an * ang[1][k] + (t ? CABMIC.typeCurve[t - 1][k] : 0);
    return Math.max(-MAX_DB, Math.min(MAX_DB, v));
  });
}

/** the change at hz (the bands a third of an octave apart, interpolated: cabmic_db_at) */
export function dbAt(db: number[], hz: number): number {
  const b = CABMIC.bands;
  if (hz <= b[0]) return db[0];
  if (hz >= b[b.length - 1]) return db[db.length - 1];
  const pos = 3 * Math.log2(hz / b[0]), i = Math.floor(pos);
  if (i >= b.length - 1) return db[db.length - 1];
  const t = pos - i;
  return db[i] * (1 - t) + db[i + 1] * t;
}

/** the words of the pedal's MIC screen */
export const positionWord = (st: number) =>
  t(st < 0.5 ? 'mic.pos_center' : st < 1.5 ? 'mic.pos_cap' : st < 2.5 ? 'mic.pos_cone' : 'mic.pos_edge');
export const distanceWord = (sd: number) =>
  t(sd < 0.5 ? 'mic.dist_touching' : sd < 1.5 ? 'mic.dist_close' : sd < 3 ? 'mic.dist_medium' : 'mic.dist_far');

/** the speaker's proportions (scr_mic.c): the dust cap ends at step 1, the cone at the last step */
export const R_SURROUND = 70, R_CONE = 62, R_CAP = 20;
export const stepRadius = (st: number) => (st <= 1 ? st * R_CAP : R_CAP + (st - 1) * 0.5 * (R_CONE - R_CAP));
export function radiusStep(r: number) {
  const st = r <= R_CAP ? r / R_CAP : 1 + (r - R_CAP) / (0.5 * (R_CONE - R_CAP));
  return Math.max(0, Math.min(POSITIONS - 1, st));
}
