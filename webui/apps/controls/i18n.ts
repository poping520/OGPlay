import { navigatorLanguage, registerEnglish, setLanguage, t } from '../../packages/ui-kit/i18n';
import { en } from './i18n-en';
registerEnglish(en);
export { language, navigatorLanguage, setLanguage, useLanguage, t, tj } from '../../packages/ui-kit/i18n';
setLanguage(navigatorLanguage(typeof navigator === 'undefined' ? '' : navigator.language));
export function updateDocumentLanguage() {
  document.title = t('OGPlay · 按键设置');
}
