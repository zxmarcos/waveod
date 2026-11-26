// The GLOBAL screen's settings as the pedal shows them (src/ui/screens/scr_settings.c: its cards, names and
// values) and the global EQ's response (src/dsp/global_eq.c and biquad.c, the same designs).

import type { IconName } from './menus';

export type Control =
  | { kind: 'choice'; options: string[] }                       // a value 0..n-1 by name
  | { kind: 'switch'; on: string; off: string }                 // 0 / 1
  | { kind: 'range'; unit?: string; signed?: boolean; fmt?: (v: number) => string }
  | { kind: 'list'; options: [number, string][] }               // values that are not 0..n-1
  | { kind: 'tempo' }                                           // 0 off, else 40..300 BPM
  | { kind: 'exp'; pedal: 1 | 2 };                              // the calibration (read only)

export interface Row { name: string; title: string; hint?: string; control: Control }
export interface Card { id: string; title: string; icon: IconName; rows: Row[]; note?: string }

/** the footswitch functions of the jack's footswitches, in the pedal's order (model/assign.c functions[]) */
export const EXT_FUNCTIONS: [number, string][] = [
  [0, '—'], [1, 'CTRL 1'], [2, 'CTRL 2'], [3, 'CTRL 3'], [4, 'CTRL 4'], [8, 'TAP'], [5, 'LOOPER'], [6, 'BATERIA'],
  [19, 'RITMO -'], [18, 'RITMO +'], [7, 'AFINADOR'], [20, 'EXP A/B'], [12, 'PRESET -'], [11, 'PRESET +'],
  [10, 'BANCO -'], [9, 'BANCO +'], [13, 'PRESET A'], [14, 'PRESET B'], [15, 'PRESET C'], [16, 'PRESET D'], [23, 'MODO'],
];
const CHANNELS: [number, string][] = [[0, 'OMNI'], ...Array.from({ length: 16 }, (_, i) => [i + 1, `${i + 1}`] as [number, string])];
const signedDb = (v: number) => `${v > 0 ? '+' : ''}${v} dB`;

export const CARDS: Card[] = [
  { id: 'io', title: 'In / Out', icon: 'io', rows: [
    { name: 'input_mode', title: 'Entrada', control: { kind: 'choice', options: ['Violão', 'Guitarra', 'Linha'] } },
    { name: 'input_level', title: 'Nível In', control: { kind: 'range', fmt: signedDb } },
    { name: 'cabs', title: 'Cab nas saídas', hint: 'onde os blocos Cab / IR tocam',
      control: { kind: 'choice', options: ['L e R', 'Só em R', 'Só em L', 'Nenhuma'] } },
    { name: 'aux', title: 'Nível AUX In', control: { kind: 'range' } },
  ] },
  { id: 'tuner', title: 'Afinador', icon: 'tuner', rows: [
    { name: 'a4', title: 'Referência A4', control: { kind: 'range', unit: 'Hz' } },
    { name: 'tuner_mute', title: 'Som ao afinar', control: { kind: 'switch', on: 'Mudo', off: 'Ligado' } },
    { name: 'tuner_strobe', title: 'Visor', control: { kind: 'switch', on: 'Strobe', off: 'Agulha' } },
  ] },
  { id: 'usb', title: 'Áudio USB', icon: 'usb', rows: [
    { name: 'usb_mode', title: 'Canais', hint: 'o computador vê o pedal de novo com a outra configuração',
      control: { kind: 'choice', options: ['6 In / 4 Out', '2 In / 2 Out'] } },
    { name: 'usb_rec', title: 'Nível de gravação', control: { kind: 'range', fmt: signedDb } },
    { name: 'usb_mon', title: 'Retorno do PC', control: { kind: 'range', fmt: signedDb } },
    { name: 'usb_wet', title: 'Gravação 1 / 2',
      control: { kind: 'choice', options: ['Seca / Seca', 'Proc. / Seca', 'Seca / Proc.', 'Proc. / Proc.'] } },
    { name: 'usb_aux', title: 'AUX na gravação', control: { kind: 'switch', on: 'Ligado', off: 'Desligado' } },
  ] },
  { id: 'jack', title: 'EXP 2 / FS externos', icon: 'fs', rows: [
    { name: 'exp2_jack', title: 'Jack EXP 2', control: { kind: 'choice', options: ['Pedal', '1 footswitch', '2 footswitches'] } },
    { name: 'ext1', title: 'FS externo 1', control: { kind: 'list', options: EXT_FUNCTIONS } },
    { name: 'ext2', title: 'FS externo 2', control: { kind: 'list', options: EXT_FUNCTIONS } },
  ] },
  { id: 'midi', title: 'MIDI', icon: 'midi', rows: [
    { name: 'midi_source', title: 'Entrada', control: { kind: 'choice', options: ['Só TRS', 'Só USB', 'TRS e USB'] } },
    { name: 'midi_trs', title: 'Canal do TRS', control: { kind: 'list', options: CHANNELS } },
    { name: 'midi_usb', title: 'Canal do USB', control: { kind: 'list', options: CHANNELS } },
    { name: 'clock_source', title: 'Fonte do tempo',
      control: { kind: 'list', options: [[0, 'Interno'], [1, 'Clock do TRS'], [2, 'Clock do USB'], [3, 'Clock externo'], [4, 'Interno e clock']] } },
    { name: 'clock_out', title: 'Enviar clock', control: { kind: 'choice', options: ['Desligado', 'TRS', 'USB', 'TRS e USB'] } },
  ] },
  { id: 'presets', title: 'Presets', icon: 'presets', rows: [
    { name: 'tempo', title: 'Tempo global', hint: 'ligado, os efeitos seguem este tempo em vez do de cada preset', control: { kind: 'tempo' } },
    { name: 'spill', title: 'Rastro na troca', hint: 'delays e reverbs soando por cima do próximo preset',
      control: { kind: 'choice', options: ['Desligado', 'Até 2 s', 'Até 5 s', 'Até 10 s', 'Até 20 s'] } },
    { name: 'scene_manual', title: 'Knobs na cena', hint: 'Auto: um knob girado com as cenas em uso entra nelas',
      control: { kind: 'switch', on: 'Manual', off: 'Auto' } },
    { name: 'cab_original', title: 'Motor Cab / IR', hint: 'o preset recarrega no outro motor',
      control: { kind: 'switch', on: 'Original', off: 'waveod' } },
  ] },
  { id: 'screen', title: 'Tela', icon: 'screen', rows: [
    { name: 'brightness', title: 'Brilho', control: { kind: 'range', unit: '%' } },
    { name: 'cursor', title: 'Marca de toque', control: { kind: 'switch', on: 'Ligada', off: 'Desligada' } },
  ] },
  { id: 'exp', title: 'Pedais EXP', icon: 'pedal', note: 'Calibre no pedal: GLOBAL › PEDAIS EXP › CALIBRAR.', rows: [
    { name: 'exp1', title: 'EXP 1', control: { kind: 'exp', pedal: 1 } },
    { name: 'exp2', title: 'EXP 2', control: { kind: 'exp', pedal: 2 } },
  ] },
];

// ---------------------------------------------------------------------- the global EQ (fw/globals_format.h)
export const EQ_LEVEL = 30;
export interface Stage { short: string; name: string; on: number; f: number; q: number; g: number; kind: 'lowcut' | 'lowshelf' | 'peak' | 'highshelf' | 'highcut' }
export const STAGES: Stage[] = [
  { short: 'L.CUT', name: 'Corte de graves', on: 0, f: 1, q: 2, g: -1, kind: 'lowcut' },
  { short: 'L.SHF', name: 'Shelf de graves', on: 3, f: 4, q: 5, g: 6, kind: 'lowshelf' },
  { short: 'B1', name: 'Banda 1', on: 7, f: 8, q: 9, g: 10, kind: 'peak' },
  { short: 'B2', name: 'Banda 2', on: 11, f: 12, q: 13, g: 14, kind: 'peak' },
  { short: 'B3', name: 'Banda 3', on: 15, f: 16, q: 17, g: 18, kind: 'peak' },
  { short: 'B4', name: 'Banda 4', on: 19, f: 20, q: 21, g: 22, kind: 'peak' },
  { short: 'H.SHF', name: 'Shelf de agudos', on: 23, f: 24, q: 25, g: 26, kind: 'highshelf' },
  { short: 'H.CUT', name: 'Corte de agudos', on: 27, f: 28, q: 29, g: -1, kind: 'highcut' },
];

export const hzText = (hz: number) => (hz >= 1000 ? `${(hz / 1000).toFixed(hz >= 10000 ? 1 : 2).replace('.', ',').replace(/,?0+$/, '')} kHz` : `${Math.round(hz)} Hz`);

const FS = 44100;

interface Coef { b0: number; b1: number; b2: number; a0: number; a1: number; a2: number }

/** dsp/biquad.c's designs (w = hz * 2 pi / fs) */
function design(kind: Stage['kind'], hz: number, q: number, db: number): Coef {
  const w = (hz * 2 * Math.PI) / FS, alpha = Math.sin(w) / (q * 2), cs = Math.cos(w);
  if (kind === 'lowcut') return { b0: (cs + 1) / 2, b1: -1 - cs, b2: (cs + 1) / 2, a0: alpha + 1, a1: -2 * cs, a2: 1 - alpha };
  if (kind === 'highcut') return { b0: (1 - cs) / 2, b1: 1 - cs, b2: (1 - cs) / 2, a0: alpha + 1, a1: -2 * cs, a2: 1 - alpha };
  const a = Math.pow(10, db / 40);
  if (kind === 'peak') return { b0: 1 + alpha * a, b1: -2 * cs, b2: 1 - alpha * a, a0: 1 + alpha / a, a1: -2 * cs, a2: 1 - alpha / a };
  const k = 2 * Math.sqrt(a) * alpha, p = a + 1, m = a - 1;
  if (kind === 'lowshelf')
    return { b0: (p - m * cs + k) * a, b1: 2 * a * (m - p * cs), b2: (p - m * cs - k) * a, a0: p + m * cs + k, a1: -2 * (m + p * cs), a2: p + m * cs - k };
  return { b0: (p + m * cs + k) * a, b1: -2 * a * (m + p * cs), b2: (p + m * cs - k) * a, a0: p - m * cs + k, a1: 2 * (m - p * cs), a2: p - m * cs - k };
}

function magDb(c: Coef, w: number) {
  const c1 = Math.cos(w), c2 = Math.cos(2 * w), s1 = Math.sin(w), s2 = Math.sin(2 * w);
  const nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
  const dr = c.a0 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
  const den = dr * dr + di * di;
  return den > 0 ? 10 * Math.log10(Math.max((nr * nr + ni * ni) / den, 1e-12)) : 0;
}

/** the EQ's response at hz in dB (global_eq_response_db): the stages that are on, and the level (50 = 0 dB) */
export function responseDb(p: number[], hz: number, stage?: number): number {
  const w = (hz * 2 * Math.PI) / FS;
  let db = stage === undefined ? 20 * Math.log10(Math.max(p[EQ_LEVEL] * 0.02, 1e-6)) : 0;
  STAGES.forEach((s, i) => {
    if ((stage !== undefined && stage !== i) || !p[s.on]) return;
    db += magDb(design(s.kind, p[s.f], p[s.q], s.g >= 0 ? p[s.g] : 0), w);
  });
  return db;
}
