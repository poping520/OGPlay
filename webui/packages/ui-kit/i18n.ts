import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
let en: Record<string, string> = {};
export function registerEnglish(table: Record<string, string>): void { en = table; }

// Shared UI language: the Chinese source text is the key. zh-CN shows it as written; en-US looks it up and falls back to
// Chinese for anything untranslated, so a missing entry never blanks the UI.
export type Language = 'zh-CN' | 'en-US';
let current: Language = 'zh-CN';
const listeners = new Set<() => void>();

export function language(): Language { return current; }
export function setLanguage(value: unknown): void {
  const next: Language = value === 'en-US' ? 'en-US' : 'zh-CN';
  if (typeof document !== 'undefined') document.documentElement.lang = next;
  if (next === current) return;
  current = next;
  listeners.forEach(listener => listener());
}
/** Re-renders the calling component (the app root) whenever the language changes. */
export function useLanguage(): Language {
  const [, bump] = useState(0);
  useEffect(() => {
    const listener = () => bump(value => value + 1);
    listeners.add(listener);
    return () => { listeners.delete(listener); };
  }, []);
  return current;
}

/** The language before any host setting is known: Chinese variants read Chinese, everything else English. */
export function navigatorLanguage(locale: string): Language {
  return locale.toLowerCase().startsWith('zh') ? 'zh-CN' : 'en-US';
}

type Params = Record<string, string | number>;
export function t(text: string, params?: Params): string {
  const template = current === 'en-US' ? en[text] ?? text : text;
  return params ? template.replace(/\{(\w+)\}/g, (match, key: string) => key in params ? String(params[key]) : match) : template;
}
/** Like t(), but placeholders may be JSX (bold counts, code paths); returns nodes in template order. */
export function tj(text: string, params: Record<string, ComponentChildren>): ComponentChildren[] {
  const template = current === 'en-US' ? en[text] ?? text : text;
  return template.split(/(\{\w+\})/).map(part => {
    const key = /^\{(\w+)\}$/.exec(part)?.[1];
    return key !== undefined && key in params ? params[key] : part;
  }).filter(part => part !== '');
}
