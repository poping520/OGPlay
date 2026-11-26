import { isLanguage, language, navigatorLanguage, registerCatalogs, t, type Language } from '../../../packages/ui-kit/i18n';
import { messages as zhCN } from './messages/zh-CN';
import { messages as zhTW } from './messages/zh-TW';
import { messages as enUS } from './messages/en-US';
import { messages as jaJP } from './messages/ja-JP';
import { messages as koKR } from './messages/ko-KR';
import { messages as ruRU } from './messages/ru-RU';
import { messages as esES } from './messages/es-ES';
import { messages as ptBR } from './messages/pt-BR';
import { messages as frFR } from './messages/fr-FR';
export const catalogs = { 'zh-CN': zhCN, 'zh-TW': zhTW, 'en-US': enUS, 'ja-JP': jaJP, 'ko-KR': koKR, 'ru-RU': ruRU, 'es-ES': esES, 'pt-BR': ptBR, 'fr-FR': frFR };
registerCatalogs(catalogs);
export { has, isLanguage, language, LANGUAGE_NAMES, LANGUAGES, setLanguage, useLanguage, t, tj, type Language } from '../../../packages/ui-kit/i18n';

/** A message the host sends: its catalog ID and arguments, or only diagnostic text the page shows as it is. */
export interface UiText { text: string; id?: string; params?: Record<string, string> }
export function uiText(value: UiText): string { return value.id ? t(value.id, value.params) : value.text; }

export function resolvedLanguage(value: unknown, system: Language): Language {
  return value === 'system' ? system : isLanguage(value) ? value : 'zh-CN';
}

/** Before settings load: the last language the host decided, else the system's, so a first run on an English system
 *  does not start in Chinese. */
export function initialLanguage(remembered: unknown, locale: string): Language {
  return isLanguage(remembered) ? remembered : navigatorLanguage(locale);
}

/** A game's name in the launcher language: exact locale ("en-US"), then the bare language ("en"), then another region
 *  of it ("zh-TW" for zh-CN), then the APK's default label. Mirrors LocalizedDisplayName on the host. */
export function gameName(item: { display_name: string; localized_names: Record<string, string> },
                         lang: Language = language()): string {
  const names = item.localized_names;
  const base = lang.split('-')[0];
  const variant = Object.keys(names).sort().find(key => key.startsWith(`${base}-`));
  return names[lang] ?? names[base] ?? (variant === undefined ? item.display_name : names[variant]);
}
