import logo from './assets/logo.png';
import { t } from './shared/i18n';
import '../../packages/ui-kit/theme.css';
// The build removes this import entirely for hosts that use their system fonts.
if (__OGPLAY_SN_PRO__) void import('./assets/fonts/fonts.css');
import { Key } from '../../packages/ui-kit/key';

// Launcher-only markup on the ui-kit controls: these need the logo asset or i18n, so they stay out of the kit's
// asset-free components (Glyph, Key). Importing this module first (app.tsx does) puts the kit stylesheet ahead of
// the page stylesheets, so pages only add layout and contextual sizes on top of it.
// Wordmark for the library and settings top bars; OG takes the splash yellow.
export function Brand() { return <div class="brand"><img src={logo} alt="" /><b><span>OG</span>Play</b></div>; }
// Typing targets keep their own keys and context menu.
export const editable = (target: EventTarget | null) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName));
// Launcher skin from the global settings_style: the ui-kit restyles a view whose root carries `skin-glass`.
export type Skin = 'console' | 'glass';
export const skinOf = (style: unknown): Skin => style === 'glass' ? 'glass' : 'console';
// Way back to the library, first after the brand on every full-screen view (settings pages, cartridge viewer).
export function Back({ disabled, onClick }: { disabled?: boolean; onClick: () => void }) {
  return <button class="back" disabled={disabled} onClick={onClick}><Key>B</Key>
    <svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7" /></svg>{t('common.backToLibrary')}</button>;
}
