import { language, navigatorLanguage, registerEnglish, type Language } from '../../../packages/ui-kit/i18n';
import { en } from './i18n-en';
registerEnglish(en);
export { language, setLanguage, useLanguage, t, tj, type Language } from '../../../packages/ui-kit/i18n';

export function resolvedLanguage(value: unknown, system: Language): Language {
  return value === 'system' ? system : value === 'en-US' ? 'en-US' : 'zh-CN';
}

/** Before settings load: the last language the host decided, else the system's, so a first run on an English system
 *  does not start in Chinese. */
export function initialLanguage(remembered: unknown, locale: string): Language {
  return remembered === 'zh-CN' || remembered === 'en-US' ? remembered : navigatorLanguage(locale);
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
