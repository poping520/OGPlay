import type { ComponentChildren } from 'preact';
import logo from './assets/logo.png';
import { t } from './i18n';
import '../../packages/ui-kit/theme.css';

// Markup for the ui-kit controls every launcher view shares. Importing this module first (main.tsx does) puts the kit
// stylesheet ahead of the page stylesheets, so pages only add layout and contextual sizes on top of it.
// Wordmark for the library and settings top bars; OG takes the splash yellow.
export function Brand() { return <div class="brand"><img src={logo} alt="" /><b><span>OG</span>Play</b></div>; }
// Launcher skin from the global settings_style: the ui-kit restyles a view whose root carries `skin-glass`.
export type Skin = 'console' | 'glass';
export const skinOf = (style: unknown): Skin => style === 'glass' ? 'glass' : 'console';
export function Key({ children }: { children: ComponentChildren }) { return <span class="key" aria-hidden="true">{children}</span>; }
// Way back to the library, first after the brand on every full-screen view (settings pages, cartridge viewer).
export function Back({ disabled, onClick }: { disabled?: boolean; onClick: () => void }) {
  return <button class="back" disabled={disabled} onClick={onClick}><Key>B</Key>
    <svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7" /></svg>{t('返回游戏库')}</button>;
}
