import { navigatorLanguage, registerCatalogs, setLanguage, t } from '../../packages/ui-kit/i18n';
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
export { language, navigatorLanguage, setLanguage, useLanguage, t, tj } from '../../packages/ui-kit/i18n';
setLanguage(navigatorLanguage(typeof navigator === 'undefined' ? '' : navigator.language));
export function updateDocumentLanguage() {
  document.title = t('controls.documentTitle');
}
