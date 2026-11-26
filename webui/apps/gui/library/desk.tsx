import type { ComponentChildren, Ref } from 'preact';
import { useLayoutEffect, useRef } from 'preact/hooks';
import type { LibraryItem } from '../shared/rpc';
import { t } from '../shared/i18n';
import { statusOf, versionLabel } from './library';
import { GameIcon } from './game-icon';

// The console's icon view (the simple one): the middle of the window is a launcher desktop, every game an app icon
// with its name, and the import tile last. A click selects, a double click starts, a right click opens the game's
// menu; app.tsx moves the selection with the arrow keys (↑/↓ by a row) and shows the selected game's bar below.
export function Desk({ items, selected, select, launch, menu, importing, startImport, grid }: {
  items: LibraryItem[]; selected?: string; select: (id: string) => void; launch: (game: LibraryItem) => void;
  menu: (game: LibraryItem, x: number, y: number) => void; importing: boolean; startImport: () => void; grid: Ref<HTMLDivElement>;
}) {
  return <div class="desk-grid" ref={grid}>
    {items.map(game => {
      const status = statusOf(game), tone = game.running ? 'running' : status.tone;
      return <button key={game.installation_id} class="app" data-instance={game.installation_id} aria-pressed={game.installation_id === selected}
        aria-label={t('{name}，{status}', { name: game.display_name, status: game.running ? t('library.status.running') : status.label })}
        title={`${game.display_name} · ${versionLabel(game)}`}
        onClick={() => select(game.installation_id)} onDblClick={() => launch(game)}
        onContextMenu={event => { event.preventDefault(); select(game.installation_id); menu(game, event.clientX, event.clientY); }}>
        <GameIcon game={game} />
        {/* Ready games stay quiet; anything else lights the icon's corner, as the cartridge LED does. */}
        {tone !== 'ready' && <i class={`led ${tone}`} />}
        <span class="name">{game.display_name}</span>
      </button>;
    })}
    <button class="app add" title={t('library.import.shortcut')} disabled={importing} onClick={startImport}>
      <span class="app-icon" aria-hidden="true">＋</span><span class="name">{t('common.importGame')}</span></button>
  </div>;
}

// Columns the grid lays out now, for ↑/↓.
export const gridColumns = (grid: HTMLElement) => getComputedStyle(grid).gridTemplateColumns.split(' ').length;

// Scrolls only the given container (scrollIntoView would also move the console, which clips instead of scrolling).
export function revealIn(container: HTMLElement, element: HTMLElement, margin = 16) {
  const top = element.offsetTop - margin, bottom = element.offsetTop + element.offsetHeight + margin;
  if (top < container.scrollTop) container.scrollTo({ top, behavior: 'smooth' });
  else if (bottom > container.scrollTop + container.clientHeight) container.scrollTo({ top: bottom - container.clientHeight, behavior: 'smooth' });
}

// A game's menu at the pointer (`.menu` look), kept inside the window.
export function ContextMenu({ x, y, children }: { x: number; y: number; children: ComponentChildren }) {
  const root = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    const menu = root.current;
    if (!menu) return;
    const box = menu.getBoundingClientRect();
    menu.style.setProperty('left', `${Math.max(8, Math.min(x, innerWidth - box.width - 8))}px`);
    menu.style.setProperty('top', `${Math.max(8, Math.min(y, innerHeight - box.height - 8))}px`);
  }, [x, y]);
  return <div ref={root} class="menu context" role="menu">{children}</div>;
}
