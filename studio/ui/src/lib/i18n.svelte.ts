// The Studio's texts in the chosen language (docs/i18n.md). The dictionaries come from the shared catalog
// (i18n/<language>/*.json at the repository's root) through tools/i18n.py gen (lib/i18n/strings.ts, which also
// types the keys: a key that does not exist fails `npm run check`).
//
// The language is the system's by default (the first of navigator.languages the catalog has, by its code or its
// first part: pt -> pt-BR), or one chosen in the top bar; the choice is kept by the backend (webapp prefs, the
// demonstration's localStorage) and told to it, so its messages come in the same language.

import { LANGUAGES, STRINGS, type Lang, type MsgKey } from './i18n/strings';

export type { Lang, MsgKey };
export { LANGUAGES };

export type Choice = 'system' | Lang;
const CODES = LANGUAGES.map((l) => l.code) as Lang[];
const REFERENCE: Lang = CODES[0];

/** the catalog's language for a BCP 47 tag, or null */
function match(tag: string): Lang | null {
  const t = tag.toLowerCase();
  return CODES.find((c) => c.toLowerCase() === t) ?? CODES.find((c) => c.toLowerCase().split('-')[0] === t.split('-')[0]) ?? null;
}

export function systemLang(): Lang {
  const tags = typeof navigator === 'undefined' ? [] : navigator.languages?.length ? navigator.languages : [navigator.language];
  for (const tag of tags) {
    const l = tag && match(tag);
    if (l) return l;
  }
  return REFERENCE;
}

class I18n {
  choice = $state<Choice>('system');
  lang = $derived<Lang>(this.choice === 'system' ? systemLang() : this.choice);
  /** where the choice goes (the backend: prefs and its own messages) */
  persist: ((choice: Choice, lang: Lang) => void) | null = null;

  set(choice: Choice) {
    this.choice = choice === 'system' || CODES.includes(choice) ? choice : 'system';
    document.documentElement.lang = this.lang;
    this.persist?.(this.choice, this.lang);
  }
}

export const i18n = new I18n();

/** a text in the language now; {name} filled from params */
export function t(key: MsgKey, params?: Record<string, string | number>): string {
  const s = STRINGS[i18n.lang]?.[key] ?? STRINGS[REFERENCE][key] ?? key;
  return params ? s.replace(/\{(\w+)\}/g, (m, k: string) => (k in params ? String(params[k]) : m)) : s;
}

/** whether text has key's text in any of the catalog's languages (a message from the backend, which may have been
 *  written in another language than the page's, perhaps after a prefix) */
export function isText(key: MsgKey, text: string): boolean {
  return LANGUAGES.some((l) => {
    const s = (STRINGS[l.code as Lang] as Record<string, string>)[key];
    return !!s && text.includes(s);
  });
}

/** whether text is key's text, params filled, in any of the catalog's languages (a default the pedal writes in its
 *  own language, like a scene's name) */
export function isAny(key: MsgKey, text: string, params?: Record<string, string | number>): boolean {
  return LANGUAGES.some((l) => {
    let s = (STRINGS[l.code as Lang] as Record<string, string>)[key];
    if (s && params) s = s.replace(/\{(\w+)\}/g, (m, k: string) => (k in params ? String(params[k]) : m));
    return s === text;
  });
}

/** a text that depends on a count: base + '_one' / '_other' (the language's plural rule; other when it has no
 *  text for the rule's category), with {n} and params filled. Zero takes base + '_zero' when there is one, else
 *  '_other' ("0 modules": CLDR files 0 under "one" in Portuguese, which reads wrong in a count) */
export function tn(base: string, n: number, params?: Record<string, string | number>): string {
  const dict = STRINGS[i18n.lang] as Record<string, string>;
  const cat = n === 0 ? (`${base}_zero` in dict ? 'zero' : 'other') : new Intl.PluralRules(i18n.lang).select(n);
  const key = (`${base}_${cat}` in dict ? `${base}_${cat}` : `${base}_other`) as MsgKey;
  return t(key, { n, ...params });
}

/** a number already written with a point ("1.5", a parameter's text) with the language's decimal separator */
export function decimal(s: string): string {
  const sep = (1.5).toLocaleString(i18n.lang).charAt(1);
  return sep === '.' ? s : s.replace('.', sep);
}

/** a number as the language writes it (decimal comma in Portuguese) */
export function num(v: number, decimals = 0): string {
  return v.toLocaleString(i18n.lang, { minimumFractionDigits: decimals, maximumFractionDigits: decimals });
}
