import type { ComponentChildren } from 'preact';
import logo from './assets/logo.png';
import '../../packages/ui-kit/theme.css';

// Markup for the ui-kit controls every launcher view shares. Importing this module first (main.tsx does) puts the kit
// stylesheet ahead of the page stylesheets, so pages only add layout and contextual sizes on top of it.
// Wordmark for the library and settings top bars; OG takes the splash yellow.
export function Brand() { return <div class="brand"><img src={logo} alt="" /><b><span>OG</span>Play</b></div>; }
export function Key({ children }: { children: ComponentChildren }) { return <span class="key" aria-hidden="true">{children}</span>; }
