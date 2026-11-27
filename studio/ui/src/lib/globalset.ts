// The GLOBAL screen's settings as the pedal shows them (src/ui/screens/scr_settings.c: its cards, names and
// values) and the global EQ's response (src/dsp/global_eq.c and biquad.c, the same designs).

import { decimal, LANGUAGES, t, type MsgKey } from './i18n.svelte';
import type { IconName } from './menus';

export type Control =
  | { kind: 'choice'; options: string[] }                       // a value 0..n-1 by name
  | { kind: 'switch'; on: string; off: string }                 // 0 / 1
  | { kind: 'range'; unit?: string; signed?: boolean; fmt?: (v: number) => string }
  | { kind: 'list'; options: [number, string][] }               // values that are not 0..n-1
  | { kind: 'tempo' }                                           // 0 off, else 40..300 BPM
  | { kind: 'exp'; pedal: 1 | 2 };                              // the calibration (read only)

/** optional: shown only when the pedal reports the setting (an older waveod may not have it) */
export interface Row { name: string; title: string; hint?: string; control: Control; optional?: boolean }
export interface Card { id: string; title: string; icon: IconName; rows: Row[]; note?: string }

// the texts are the catalog's (i18n/studio-system.json): getters, so the page shows them in the language now. A text
// written '=...' is not translated (product terms, the pedal's own labels).
type Txt = MsgKey | `=${string}`;
const word = (k: Txt) => (k.startsWith('=') ? k.slice(1) : t(k as MsgKey));
const choice = (...keys: Txt[]): Control => ({ kind: 'choice', get options() { return keys.map(word); } });
const toggle = (on: Txt, off: Txt): Control => ({ kind: 'switch', get on() { return word(on); }, get off() { return word(off); } });
const list = (items: [number, Txt][]): Control => ({ kind: 'list', get options() { return items.map(([v, k]) => [v, word(k)] as [number, string]); } });
const row = (name: string, title: Txt, control: Control, hint?: MsgKey): Row =>
  ({ name, get title() { return word(title); }, get hint() { return hint && t(hint); }, control });
const card = (id: string, title: Txt, icon: IconName, rows: Row[], note?: MsgKey): Card =>
  ({ id, get title() { return word(title); }, icon, rows, get note() { return note && t(note); } });

/** the footswitch functions of the jack's footswitches, in the pedal's order (model/assign.c functions[]) */
const EXT_FUNCTIONS: [number, Txt][] = [
  [0, '=—'], [1, '=CTRL 1'], [2, '=CTRL 2'], [3, '=CTRL 3'], [4, '=CTRL 4'], [8, 'shared.fn.tap'], [5, 'shared.fn.looper'],
  [6, 'shared.fn.drum'], [19, 'shared.fn.drum_prev'], [18, 'shared.fn.drum_next'], [7, 'shared.fn.tuner'], [20, 'shared.fn.exp_ab'],
  [12, 'shared.fn.preset_down'], [11, 'shared.fn.preset_up'], [10, 'shared.fn.bank_down'], [9, 'shared.fn.bank_up'],
  [13, '=PRESET A'], [14, '=PRESET B'], [15, '=PRESET C'], [16, '=PRESET D'], [23, 'shared.fn.mode'],
];
const CHANNELS: [number, string][] = [[0, 'OMNI'], ...Array.from({ length: 16 }, (_, i) => [i + 1, `${i + 1}`] as [number, string])];
const signedDb = (v: number) => `${v > 0 ? '+' : ''}${v} dB`;
/** the pedal's language: the catalog's languages, in the order the pedal stores them (setting 40) */
const language: Row = { name: 'language', get title() { return t('global.pedal_language'); }, optional: true,
                        control: { kind: 'choice', options: LANGUAGES.map((l) => l.name) } };

export const CARDS: Card[] = [
  card('io', 'global.card_io', 'io', [
    row('input_mode', 'global.input', choice('global.input_acoustic', 'global.input_guitar', 'global.input_line')),
    row('input_level', 'global.input_level', { kind: 'range', fmt: signedDb }),
    row('cabs', 'global.cabs', choice('global.cabs_lr', 'global.cabs_r', 'global.cabs_l', 'global.cabs_none'), 'global.cabs_hint'),
    row('aux', 'global.aux', { kind: 'range' }),
  ]),
  card('tuner', 'global.card_tuner', 'tuner', [
    row('a4', 'global.a4', { kind: 'range', unit: 'Hz' }),
    row('tuner_mute', 'global.tuner_mute', toggle('global.muted', 'global.on')),
    row('tuner_strobe', 'global.tuner_display', toggle('=Strobe', 'global.needle')),
  ]),
  card('usb', 'global.card_usb', 'usb', [
    row('usb_mode', 'global.usb_mode', choice('=6 In / 4 Out', '=2 In / 2 Out'), 'global.usb_mode_hint'),
    row('usb_rec', 'global.usb_rec', { kind: 'range', fmt: signedDb }),
    row('usb_mon', 'global.usb_mon', { kind: 'range', fmt: signedDb }),
    row('usb_wet', 'global.usb_wet', choice('global.wet_dd', 'global.wet_pd', 'global.wet_dp', 'global.wet_pp')),
    row('usb_aux', 'global.usb_aux', toggle('global.on', 'global.off')),
  ]),
  card('jack', 'global.card_jack', 'fs', [
    row('exp2_jack', 'global.exp2_jack', choice('global.jack_pedal', 'global.jack_fs1', 'global.jack_fs2')),
    row('ext1', 'global.ext1', list(EXT_FUNCTIONS)),
    row('ext2', 'global.ext2', list(EXT_FUNCTIONS)),
  ]),
  card('midi', '=MIDI', 'midi', [
    row('midi_source', 'global.input', choice('global.trs_only', 'global.usb_only', 'global.trs_usb')),
    row('midi_trs', 'global.midi_trs', { kind: 'list', options: CHANNELS }),
    row('midi_usb', 'global.midi_usb', { kind: 'list', options: CHANNELS }),
    row('clock_source', 'global.clock_source', list([[0, 'global.clock_internal'], [1, 'global.clock_trs'], [2, 'global.clock_usb'],
                                                    [3, 'global.clock_external'], [4, 'global.clock_both']])),
    row('clock_out', 'global.clock_out', choice('global.off', '=TRS', '=USB', 'global.trs_usb')),
  ]),
  card('presets', 'tab.presets', 'presets', [
    row('tempo', 'global.tempo', { kind: 'tempo' }, 'global.tempo_hint'),
    row('spill', 'global.spill', {
      kind: 'choice', get options() { return [t('global.off'), ...[2, 5, 10, 20].map((n) => t('global.up_to', { n }))]; },
    }, 'global.spill_hint'),
    row('scene_manual', 'global.scene_knobs', toggle('global.manual', 'global.auto'), 'global.scene_knobs_hint'),
    row('cab_original', 'global.cab_engine', toggle('global.original', '=waveod'), 'global.cab_engine_hint'),
  ]),
  card('screen', 'global.card_screen', 'screen', [
    row('brightness', 'global.brightness', { kind: 'range', unit: '%' }),
    row('cursor', 'global.cursor', toggle('global.on_f', 'global.off_f')),
    language,
  ]),
  card('exp', 'global.card_exp', 'pedal', [
    row('exp1', '=EXP 1', { kind: 'exp', pedal: 1 }),
    row('exp2', '=EXP 2', { kind: 'exp', pedal: 2 }),
  ], 'global.exp_note'),
];

// ---------------------------------------------------------------------- the global EQ (fw/globals_format.h)
export const EQ_LEVEL = 30;
export interface Stage { short: string; name: string; on: number; f: number; q: number; g: number; kind: 'lowcut' | 'lowshelf' | 'peak' | 'highshelf' | 'highcut' }
const stage = (short: string, name: MsgKey, on: number, f: number, q: number, g: number, kind: Stage['kind'], n?: number): Stage =>
  ({ short, get name() { return t(name, n === undefined ? undefined : { n }); }, on, f, q, g, kind });
export const STAGES: Stage[] = [
  stage('L.CUT', 'eq.stage_lowcut', 0, 1, 2, -1, 'lowcut'),
  stage('L.SHF', 'eq.stage_lowshelf', 3, 4, 5, 6, 'lowshelf'),
  stage('B1', 'eq.stage_band', 7, 8, 9, 10, 'peak', 1),
  stage('B2', 'eq.stage_band', 11, 12, 13, 14, 'peak', 2),
  stage('B3', 'eq.stage_band', 15, 16, 17, 18, 'peak', 3),
  stage('B4', 'eq.stage_band', 19, 20, 21, 22, 'peak', 4),
  stage('H.SHF', 'eq.stage_highshelf', 23, 24, 25, 26, 'highshelf'),
  stage('H.CUT', 'eq.stage_highcut', 27, 28, 29, -1, 'highcut'),
];

export const hzText = (hz: number) => (hz >= 1000 ? `${decimal((hz / 1000).toFixed(hz >= 10000 ? 1 : 2).replace(/\.?0+$/, ''))} kHz` : `${Math.round(hz)} Hz`);

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
