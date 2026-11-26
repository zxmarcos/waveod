// The control table (waveod_studio/controls.py, the pedal's model/control.c): links "key -> what" per footswitch
// mode, and a name and LED colour per footswitch. The same rules as the classic editor (ui/controlsview.py), so a
// table edited here is the one the pedal's CONTROLES / PARA ONDE screens would make.

import type { CtlBlock, CtlLink, CtlParam, CtlTable, ModSettings } from './types';

// keys
export const A = 0, B = 1, C = 2, D = 3, BANK_DOWN = 4, BANK_UP = 5;
export const COMBO = 6;                                  // .. +3: A+B, B+C, C+D, BANK- + BANK+
export const AGAIN = 10, TOE = 11, EXP1A = 12, EXP1B = 13, EXP2 = 14, MOD1 = 15;
export const KEY_COUNT = MOD1 + 4;
export const MIDI_KEY = 0x80;
export const FOOTSWITCH_KEYS = 6;
// modes (bits), when, actions, curves, flags
export const PRESET = 1, STOMP = 2, SCENE = 4, ALL = 7;
export const PRESS = 0, HOLD = 1, MOMENT = 2;
export const NONE = 0, FN = 1, BLOCK = 2, PARAM = 3, SCENE_ACT = 4, MIDI = 5, NOTHING = 6;
export const LINEAR = 0, SLOW = 1, FAST = 2;
export const CF_ORDER = 1, CF_PC = 2;
export const SCENE_NEXT = 0xfe, SCENE_PREV = 0xfd;
export const SCENE_COUNT = 4, CONTROL_MAX = 48, LABEL_LEN = 8, KNOBS = 15;

export const KEY_NAMES = ['A', 'B', 'C', 'D', 'BANK −', 'BANK +', 'A + B', 'B + C', 'C + D', 'BANK − E +', 'PRESET ATIVO',
                          'BICO DO EXP 1', 'EXP 1A', 'EXP 1B', 'EXP 2', 'MOD 1', 'MOD 2', 'MOD 3', 'MOD 4'];
export const MODES = [{ mode: PRESET, label: 'Modo preset', name: 'PRESET' }, { mode: STOMP, label: 'Modo stomp', name: 'STOMP' },
                      { mode: SCENE, label: 'Modo cenas', name: 'CENAS' }];
export const MODE_NAMES: Record<number, string> = { [PRESET]: 'PRESET', [STOMP]: 'STOMP', [SCENE]: 'CENAS' };
export const WHEN_NAMES = ['APERTAR', 'SEGURAR', 'PISANDO'];
export const CURVE_NAMES = ['LINEAR', 'LENTA', 'RÁPIDA'];
export const COLOUR_NAMES = ['AUTOMÁTICA', 'VERMELHO', 'VERDE', 'AMARELO', 'AZUL', 'MAGENTA', 'CIANO', 'BRANCO'];
/** the LED colours as the page draws them (0: the block's own colour, picked by the pedal) */
export const COLOUR_CSS = ['transparent', '#ff4d4a', '#39d36b', '#ffd53d', '#3d8bff', '#ff4dd2', '#3de0ff', '#f2f2f2'];

// footswitch functions (enum fs_function), in the order the pedal offers them (ui_functions in ui/ui.c)
export const FUNCTIONS: [number, string][] = [
  [8, 'TAP'], [7, 'AFINADOR'], [5, 'LOOPER'], [6, 'BATERIA'], [19, 'RITMO -'], [18, 'RITMO +'], [20, 'EXP A/B'],
  [12, 'PRESET -'], [11, 'PRESET +'], [10, 'BANCO -'], [9, 'BANCO +'], [17, 'BANCOS'], [13, 'PRESET A'],
  [14, 'PRESET B'], [15, 'PRESET C'], [16, 'PRESET D'], [23, 'MODO'],
];
const FN_NAMES: Record<number, string> = Object.fromEntries([
  ...FUNCTIONS, [0, '-'], [1, 'CTRL 1'], [2, 'CTRL 2'], [3, 'CTRL 3'], [4, 'CTRL 4'],
]);

export const midiCcOk = (cc: number) => Number.isInteger(cc) && cc > 0 && cc < 128 && cc !== 68 && cc !== 69;
export const isMidi = (key: number) => key >= MIDI_KEY;
export const isPedal = (key: number) => key >= EXP1A && key <= EXP2;
export const isMod = (key: number) => key >= MOD1 && key < MOD1 + 4;
/** a pedal, a MIDI CC or a modulator: its links follow a position (heel / toe values, a curve) */
export const sweeps = (key: number) => isPedal(key) || isMidi(key) || isMod(key);
/** the toe switch, the pedals, the modulators and MIDI work in every mode */
export const everyMode = (key: number) => key >= TOE;
export const keyName = (key: number) =>
  isMidi(key) ? `MIDI CC ${key - MIDI_KEY}` : KEY_NAMES[key] ?? '?';
export const fnName = (fn: number) => FN_NAMES[fn] ?? `FUNÇÃO ${fn}`;
export const isPc = (l: CtlLink) => l.action === MIDI && !!(l.flags & CF_PC);
/** a link may be in the global table: nothing tied to a preset's blocks */
export const globalOk = (l: CtlLink) => l.action === FN || l.action === SCENE_ACT || l.action === MIDI;

export function link(key: number, modes: number, when: number, action: number, slot = -1, arg = 0): CtlLink {
  return { key, modes, when, action, slot, arg, curve: LINEAR, flags: 0, off: 0, on: 0 };
}
export const copyLink = (l: CtlLink): CtlLink => ({ ...l });
export const copyTable = (t: CtlTable): CtlTable =>
  ({ links: t.links.map(copyLink), colour: [...t.colour], label: [...t.label] });

// ---------------------------------------------------------------------- queries
/** table indexes of key's links in `mode` (every mode for the keys that have no modes) */
export function linksOf(t: CtlTable, key: number, mode: number): number[] {
  const out: number[] = [];
  t.links.forEach((l, i) => { if (l.key === key && (everyMode(key) || l.modes & mode)) out.push(i); });
  return out;
}
export const midiKeys = (t: CtlTable) => [...new Set(t.links.filter((l) => isMidi(l.key)).map((l) => l.key))].sort((a, b) => a - b);

/** a key's group in `mode`: the press (and while down) links, or the long press ones */
export const group = (t: CtlTable, key: number, mode: number, hold: boolean) =>
  linksOf(t, key, mode).filter((i) => (t.links[i].when === HOLD) === hold);

/** indexes in the global table of key's links that run in a preset with `t`: the groups it has no link in */
export function inherited(t: CtlTable, glob: CtlTable | null, key: number, mode: number): number[] {
  if (!glob) return [];
  const out: number[] = [];
  for (const hold of [false, true]) if (!group(t, key, mode, hold).length) out.push(...group(glob, key, mode, hold));
  return out;
}

/** [link, fromGlobal] key does in `mode` (NOTHING links left out) */
export function runs(t: CtlTable, glob: CtlTable | null, key: number, mode: number): [CtlLink, boolean][] {
  const out: [CtlLink, boolean][] = linksOf(t, key, mode).map((i) => [t.links[i], false]);
  for (const i of inherited(t, glob, key, mode)) out.push([glob!.links[i], true]);
  return out.filter(([l]) => l.action !== NOTHING);
}

// ---------------------------------------------------------------------- changes (in place)
export class TableFull extends Error {
  constructor() { super(`Tabela de controles cheia (${CONTROL_MAX} ligações)`); }
}

/** link i made `mode`'s own: a link shared with other modes splits (the copy right after it); its index */
export function own(t: CtlTable, i: number, mode: number): number {
  const l = t.links[i];
  if (everyMode(l.key) || l.modes === mode) return i;
  if (t.links.length >= CONTROL_MAX) throw new TableFull();
  const c = { ...l, modes: mode };
  l.modes &= ~mode;
  t.links.splice(i + 1, 0, c);
  return i + 1;
}

export function add(t: CtlTable, l: CtlLink): number {
  if (t.links.length >= CONTROL_MAX) throw new TableFull();
  t.links.push(l);
  return t.links.length - 1;
}

/** the link out of `mode` (out of the table when that was its only mode) */
export function remove(t: CtlTable, i: number, mode: number) {
  const l = t.links[i];
  if (!everyMode(l.key) && l.modes !== mode && l.modes & mode) l.modes &= ~mode;
  else t.links.splice(i, 1);
}

/** a link that does nothing yet (the pedal's + NOVA LIGAÇÃO): the editor gives it a destination */
export const newLink = (key: number, mode: number) => link(key, everyMode(key) ? ALL : mode, PRESS, FN, -1, 0);
/** a link still without a destination (left out when the table is written) */
export const unset = (l: CtlLink) => l.action === FN && l.arg === 0;

// ---------------------------------------------------------------------- parameters
export function paramFmt(p: CtlParam, v: number): string {
  const step = p.step > 0 ? p.step : 1;
  if (Math.abs(step - Math.round(step)) < 1e-6 && Math.abs(p.min - Math.round(p.min)) < 1e-6) return `${Math.round(v)}`;
  return v.toFixed(step >= 0.1 - 1e-6 ? 1 : 2);
}
export function quantize(p: CtlParam, v: number): number {
  const step = p.step > 0 ? p.step : 1;
  v = Math.min(Math.max(v, p.min), p.max);
  const n = Math.floor((v - p.min) / step + 0.5);
  return Math.min(p.min + n * step, p.max);
}
export const blockOf = (blocks: CtlBlock[], slot: number) => blocks.find((b) => b.slot === slot);

/** the parameter a parameter link moves */
export function linkParam(l: CtlLink, blocks: CtlBlock[]): CtlParam | undefined {
  const blk = blockOf(blocks, l.slot);
  if (!blk || l.action !== PARAM) return undefined;
  if (l.flags & CF_ORDER) return blk.params[l.arg];
  return blk.params.find((p) => p.knob === l.arg);
}

/** a parameter link with the pedal's defaults: a pedal or a MIDI CC sweeps the whole range, a key goes from the
 *  value now (the slot's stored knob) to the other end */
export function setParam(l: CtlLink, blk: CtlBlock, p: CtlParam, knobs?: number[]) {
  l.action = PARAM; l.slot = blk.slot; l.arg = p.knob;
  l.flags &= ~(CF_ORDER | CF_PC);
  if (sweeps(l.key)) {
    l.off = p.min; l.on = p.max;
  } else {
    const now = knobs && p.knob < knobs.length ? knobs[p.knob] : p.min;
    l.off = now;
    l.on = now < p.max ? p.max : p.min;
  }
}

/** a MIDI out link with the pedal's defaults (control_set_midi) */
export function setMidi(l: CtlLink, program: boolean) {
  const channel = l.action === MIDI ? l.slot & 15 : 0;
  l.action = MIDI; l.slot = channel;
  l.arg = program ? 0 : isMidi(l.key) ? l.key - MIDI_KEY : 80;
  l.flags = program ? CF_PC : 0;
  l.off = 0; l.on = 127;
}

/** a block, scene or function link */
export function setSimple(l: CtlLink, action: number, slot: number, arg: number) {
  l.action = action; l.slot = action === BLOCK ? slot : -1; l.arg = arg; l.flags = 0;
}

// ---------------------------------------------------------------------- text (the pedal's words)
export const sceneText = (arg: number, names?: string[]) =>
  arg === SCENE_NEXT ? 'PRÓXIMA CENA' : arg === SCENE_PREV ? 'CENA ANTERIOR'
    : names?.[arg] && names[arg].toUpperCase() !== `CENA ${arg + 1}` ? `CENA ${arg + 1} · ${names[arg].toUpperCase()}` : `CENA ${arg + 1}`;

export const blockChoiceText = (l: CtlLink) =>
  isPedal(l.key) ? (l.arg ? 'LIGA AO MOVER' : 'COM O GRUPO') : (l.arg ? 'DESLIGA O BLOCO' : 'LIGA O BLOCO');

/** what a link does (controls.link_text) */
export function linkText(l: CtlLink, blocks: CtlBlock[], scenes?: string[]): string {
  const blk = blockOf(blocks, l.slot);
  const bname = blk ? blk.name : `bloco ${l.slot + 1}`;
  if (l.action === BLOCK) return `${bname} · ${blockChoiceText(l)}`;
  if (l.action === PARAM) {
    const p = linkParam(l, blocks);
    if (!p) return `${bname} · parâmetro ?`;
    let text = `${bname} · ${p.name.toUpperCase()} ${paramFmt(p, l.off)} » ${paramFmt(p, l.on)}`;
    if (sweeps(l.key) && l.curve !== LINEAR) text += ` · ${CURVE_NAMES[l.curve % 3]}`;
    return text;
  }
  if (l.action === SCENE_ACT) return sceneText(l.arg, scenes);
  if (l.action === MIDI) {
    if (isPc(l)) return `MIDI PC ${l.arg} · CANAL ${(l.slot & 15) + 1}`;
    let text = `MIDI CC ${l.arg} ${Math.round(l.off)} » ${Math.round(l.on)} · CANAL ${(l.slot & 15) + 1}`;
    if (sweeps(l.key) && l.curve !== LINEAR) text += ` · ${CURVE_NAMES[l.curve % 3]}`;
    return text;
  }
  if (l.action === NOTHING) return 'NADA (SEM AS GLOBAIS)';
  if (l.action === FN && l.arg === 0) return '(nada ainda)';
  return fnName(l.arg);
}

/** a link in a word or two (controls.brief) */
export function brief(l: CtlLink, blocks: CtlBlock[]): string {
  const blk = blockOf(blocks, l.slot);
  if (l.action === BLOCK) return blk ? blk.name : '?';
  if (l.action === PARAM) return linkParam(l, blocks)?.name ?? '?';
  if (l.action === MIDI) return `» ${isPc(l) ? 'PC' : 'CC'} ${l.arg}`;
  return linkText(l, blocks);
}

/** the press kind of a footswitch link; '' for the keys that have none */
export const whenText = (l: CtlLink) => (everyMode(l.key) ? '' : WHEN_NAMES[l.when % 3]);

/** the names of a link's two values */
export function ends(key: number): [string, string] {
  if (isPedal(key)) return ['CALCANHAR', 'PONTA'];
  if (isMidi(key)) return ['EM 0', 'EM 127'];
  if (isMod(key)) return ['MOD 0%', 'MOD 100%'];
  return ['OFF', 'ON'];
}

// ---------------------------------------------------------------------- where a link goes (PARA ONDE / O QUÊ)
export type DestKind = 'block' | 'scenes' | 'fn' | 'midi';
export interface Dest { id: string; label: string; kind: DestKind; block?: CtlBlock }
export interface Choice { id: string; label: string; apply: (l: CtlLink) => void }

/** the destinations of a key's links: the preset's blocks (numbered when two share a name), scenes, functions, MIDI */
export function dests(key: number, blocks: CtlBlock[]): Dest[] {
  const out: Dest[] = [];
  const seen: Record<string, number> = {};
  for (const b of blocks) {
    seen[b.name] = (seen[b.name] ?? 0) + 1;
    out.push({ id: `b${b.slot}`, label: seen[b.name] === 1 ? b.name : `${b.name} (${seen[b.name]})`, kind: 'block', block: b });
  }
  if (isMod(key)) return out;                              // a modulator moves parameters only
  if (!isPedal(key)) out.push({ id: 'scenes', label: 'Cenas', kind: 'scenes' }, { id: 'fn', label: 'Funções', kind: 'fn' });
  out.push({ id: 'midi', label: 'MIDI', kind: 'midi' });
  return out;
}

/** what of a destination (the pedal's O QUÊ list) */
export function choices(key: number, d: Dest, scenes: string[], knobs?: Record<string, number[]>): Choice[] {
  if (d.kind === 'scenes') {
    const out: Choice[] = scenes.map((_, k) => ({ id: `s${k}`, label: sceneText(k, scenes), apply: (l) => setSimple(l, SCENE_ACT, -1, k) }));
    return [...out,
      { id: 'next', label: 'PRÓXIMA CENA', apply: (l) => setSimple(l, SCENE_ACT, -1, SCENE_NEXT) },
      { id: 'prev', label: 'CENA ANTERIOR', apply: (l) => setSimple(l, SCENE_ACT, -1, SCENE_PREV) }];
  }
  if (d.kind === 'fn') return FUNCTIONS.map(([fn, name]) => ({ id: `f${fn}`, label: name, apply: (l) => setSimple(l, FN, -1, fn) }));
  if (d.kind === 'midi') {
    const out: Choice[] = [{ id: 'cc', label: 'ENVIAR CC', apply: (l) => setMidi(l, false) }];
    if (!isPedal(key)) out.push({ id: 'pc', label: 'ENVIAR PC', apply: (l) => setMidi(l, true) });
    return out;
  }
  const blk = d.block!;
  const out: Choice[] = [];
  if (isPedal(key)) {
    out.push({ id: 'on1', label: 'LIGA AO MOVER', apply: (l) => setSimple(l, BLOCK, blk.slot, 1) });
    if (key !== EXP2) out.push({ id: 'on0', label: 'COM O GRUPO', apply: (l) => setSimple(l, BLOCK, blk.slot, 0) });
  } else if (!isMod(key)) {
    out.push({ id: 'on0', label: 'LIGA O BLOCO', apply: (l) => setSimple(l, BLOCK, blk.slot, 0) },
             { id: 'on1', label: 'DESLIGA O BLOCO', apply: (l) => setSimple(l, BLOCK, blk.slot, 1) });
  }
  for (const p of blk.params) {
    if (p.knob < KNOBS) out.push({ id: `k${p.knob}`, label: p.name.toUpperCase(), apply: (l) => setParam(l, blk, p, knobs?.[String(blk.slot)]) });
  }
  return out;
}

/** [destination id, choice id] a link has now ('' when it has none yet) */
export function current(l: CtlLink, blocks: CtlBlock[]): [string, string] {
  if (l.action === BLOCK) return [`b${l.slot}`, `on${l.arg ? 1 : 0}`];
  if (l.action === PARAM) {
    const p = linkParam(l, blocks);
    return [`b${l.slot}`, p ? `k${p.knob}` : ''];
  }
  if (l.action === SCENE_ACT) return ['scenes', l.arg === SCENE_NEXT ? 'next' : l.arg === SCENE_PREV ? 'prev' : `s${l.arg}`];
  if (l.action === MIDI) return ['midi', isPc(l) ? 'pc' : 'cc'];
  if (l.action === FN && l.arg) return ['fn', `f${l.arg}`];
  return ['', ''];
}

/** the QUANDO (footswitch) or CURVA (pedal, MIDI, modulator) choice of a link: [title, names, value] or null */
export function whenChoice(l: CtlLink): [string, string[], number] | null {
  if (sweeps(l.key)) {
    if (l.action === PARAM || (l.action === MIDI && !isPc(l))) return ['CURVA', CURVE_NAMES, l.curve % 3];
    return null;
  }
  if (l.key === TOE) return null;
  return ['QUANDO', WHEN_NAMES, l.when % 3];
}

// ---------------------------------------------------------------------- modulators (docs/modulators.md)
export const MOD_TYPES: { id: ModSettings['type']; label: string }[] = [
  { id: 'off', label: 'Desligado' }, { id: 'lfo', label: 'LFO' }, { id: 'env', label: 'Envelope' }, { id: 'seq', label: 'Sequência' },
];
export const MOD_SHAPES: { id: string; label: string }[] = [
  { id: 'sine', label: 'Senoide' }, { id: 'triangle', label: 'Triângulo' }, { id: 'square', label: 'Quadrada' },
  { id: 'saw up', label: 'Dente subindo' }, { id: 'saw down', label: 'Dente descendo' }, { id: 'random', label: 'Aleatória' },
  { id: 'smooth', label: 'Aleatória suave' },
];
export const DIVISIONS = ['Livre', '1/1', '1/2', '1/2D', '1/2T', '1/4', '1/4D', '1/4T', '1/8', '1/8D', '1/8T', '1/16'];
export const MOD_STEPS = 16;

/** a modulator as the pedal starts one of that type (wvpreset.mod_default) */
export function modDefault(type: ModSettings['type']): ModSettings {
  return { type, shape: 'sine', sync: 0, rate: 1, steps: 8, phase: 0, glide: 0, sens: 0, attack: 10, release: 200,
           step: Array.from({ length: MOD_STEPS }, (_, i) => Math.floor(((i % 8) * 100) / 7)) };
}
