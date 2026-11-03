import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { en } from './i18n-en';

// GUI-14: the Chinese source text is the key. zh-CN shows it as written; en-US looks it up and falls back to
// Chinese for anything untranslated (host-provided details included), so a missing entry never blanks the UI.
export type Language = 'zh-CN' | 'en-US';
let current: Language = 'zh-CN';
const listeners = new Set<() => void>();

export function language(): Language { return current; }
export function setLanguage(value: unknown): void {
  const next: Language = value === 'en-US' ? 'en-US' : 'zh-CN';
  if (next === current) return;
  current = next;
  if (typeof document !== 'undefined') document.documentElement.lang = next;
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

/** A game's name in the launcher language: exact locale ("en-US"), then the bare language ("en"), then another region
 *  of it ("zh-TW" for zh-CN), then the APK's default label. Mirrors LocalizedDisplayName on the host. */
export function gameName(item: { display_name: string; localized_names: Record<string, string> },
                         lang: Language = current): string {
  const names = item.localized_names;
  const base = lang.split('-')[0];
  const variant = Object.keys(names).sort().find(key => key.startsWith(`${base}-`));
  return names[lang] ?? names[base] ?? (variant === undefined ? item.display_name : names[variant]);
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
