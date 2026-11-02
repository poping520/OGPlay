import type { ComponentChildren } from 'preact';

// Gamepad key badge (theme.css `.key`): a letter, a short label or a Glyph.
export function Key({ children }: { children: ComponentChildren }) { return <span class="key" aria-hidden="true">{children}</span>; }
