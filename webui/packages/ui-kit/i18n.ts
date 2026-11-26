import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { formatParts, parseMessage, type MessageNode } from './icu';

// Shared UI language. Code names a message by a semantic ID ("library.delete.confirm"); each language has a catalog of
// ICU messages for those IDs. zh-CN is the complete source catalog; a missing message falls back to English
// (Traditional Chinese falls back to Simplified), then to zh-CN, then to the ID itself, so the UI never goes blank.
// The tags match the host's (ui_text.h).
export const LANGUAGES = ['zh-CN', 'zh-TW', 'en-US', 'ja-JP', 'ko-KR', 'ru-RU', 'es-ES', 'pt-BR', 'fr-FR'] as const;
export type Language = typeof LANGUAGES[number];
export type Catalog = Record<string, string>;
export type Catalogs = { 'zh-CN': Catalog } & Partial<Record<Language, Catalog>>;
// Each language in its own name, so a reader finds theirs whatever the UI is showing.
export const LANGUAGE_NAMES: Record<Language, string> = {
  'zh-CN': '简体中文', 'zh-TW': '繁體中文', 'en-US': 'English', 'ja-JP': '日本語', 'ko-KR': '한국어',
  'ru-RU': 'Русский', 'es-ES': 'Español', 'pt-BR': 'Português (Brasil)', 'fr-FR': 'Français',
};

let catalogs: Catalogs = { 'zh-CN': {} };
const parsed = new Map<string, MessageNode[]>();
export function registerCatalogs(value: Catalogs): void { catalogs = value; parsed.clear(); }

let current: Language = 'zh-CN';
const listeners = new Set<() => void>();

export function isLanguage(value: unknown): value is Language {
  return typeof value === 'string' && (LANGUAGES as readonly string[]).includes(value);
}
export function language(): Language { return current; }
export function setLanguage(value: unknown): void {
  const next: Language = isLanguage(value) ? value : 'zh-CN';
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

/** The language before any host setting is known, chosen as the host does for a system locale (UiLanguageForLocale):
 *  Traditional Chinese for Hant or TW/HK/MO, other Chinese simplified, the supported languages by their base, and
 *  English for everything else. */
export function navigatorLanguage(locale: string): Language {
  const [base = '', ...rest] = locale.toLowerCase().split(/[-_]/);
  if (base === 'zh') return rest.some(part => ['hant', 'tw', 'hk', 'mo'].includes(part)) ? 'zh-TW' : 'zh-CN';
  return LANGUAGES.find(tag => tag !== 'zh-TW' && tag.split('-')[0] === base) ?? 'en-US';
}

function source(id: string): [string, Language] | undefined {
  const chain: Language[] = [current, current === 'zh-TW' ? 'zh-CN' : 'en-US', 'zh-CN'];
  for (const tag of chain) { const text = catalogs[tag]?.[id]; if (text !== undefined) return [text, tag]; }
  return undefined;
}
/** Whether a message exists for the ID (in any catalog the fallback reaches). */
export function has(id: string): boolean { return source(id) !== undefined; }

type Params = Record<string, unknown>;
function parts(id: string, params: Params = {}): unknown[] {
  const found = source(id);
  if (!found) return [id];
  const [text, tag] = found, cacheKey = `${tag}\u0000${id}`;
  let nodes = parsed.get(cacheKey);
  if (!nodes) { nodes = parseMessage(text); parsed.set(cacheKey, nodes); }
  return formatParts(nodes, params, tag);
}
export function t(id: string, params?: Record<string, string | number>): string {
  return parts(id, params).join('');
}
/** Like t(), but arguments may be JSX (bold counts, code paths); returns nodes in message order. */
export function tj(id: string, params: Record<string, ComponentChildren>): ComponentChildren[] {
  return parts(id, params).filter(part => part !== '') as ComponentChildren[];
}
