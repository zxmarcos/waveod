// A parameter's value while a knob moves, before the pedal answers with its own text.

import type { Param } from './types';

/** the value the pedal would keep: in range, on the step (an option index for named values) */
export function stepped(p: Param, v: number): number {
  const st = p.options.length ? 1 : p.step > 0 ? p.step : (p.max - p.min) / 1000;
  const q = Math.round((v - p.min) / st) * st + p.min;
  return Math.min(p.max, Math.max(p.min, Number(q.toFixed(6))));
}

/** the value's text, keeping the unit of the pedal's last text ("120 ms", "35%") */
export function valueText(p: Param, v: number): string {
  if (p.options.length) return p.options[Math.round(v)] ?? String(Math.round(v));
  const decimals = p.step > 0 && p.step < 1 ? Math.min(2, Math.ceil(-Math.log10(p.step) - 1e-9)) : 0;
  const unit = p.text.replace(/^[-+]?[\d.,]+\s*/, '');
  const num = v.toFixed(decimals);
  if (!unit || unit === p.text) return num;
  return /^[%°]/.test(unit) ? num + unit : `${num} ${unit}`;
}

/** 0..1 along the knob's travel */
export function fraction(p: Param): number {
  return p.max > p.min ? (p.value - p.min) / (p.max - p.min) : 0;
}
