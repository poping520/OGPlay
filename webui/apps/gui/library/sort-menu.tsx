import { useEffect, useRef, useState } from 'preact/hooks';
import { Glyph } from '../../../packages/ui-kit/glyph';
import { t } from '../shared/i18n';
import type { Sort } from './library';

// Sort picker in the "更多" menu style: a native <select> opens the system menu, which no skin can restyle.
const sorts: readonly (readonly [Sort, string])[] = [['name', '名称'], ['newest', '最近导入'], ['oldest', '最早导入']];
export function SortMenu({ value, change }: { value: Sort; change: (value: Sort) => void }) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    root.current?.querySelector<HTMLElement>('[aria-checked=true]')?.focus();
    const close = (event: PointerEvent) => { if (!root.current?.contains(event.target as Node)) setOpen(false); };
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [open]);
  const dismiss = () => { setOpen(false); root.current?.querySelector<HTMLElement>('[aria-haspopup]')?.focus(); };
  // Esc closes only the menu (not the library's filters); ↑/↓ walk its entries.
  const keydown = (event: KeyboardEvent) => {
    if (!open) return;
    if (event.key === 'Escape') { event.stopPropagation(); dismiss(); return; }
    if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
    event.preventDefault();
    const entries = Array.from(root.current!.querySelectorAll<HTMLElement>('[role=menuitemradio]'));
    const index = entries.indexOf(document.activeElement as HTMLElement);
    entries[(index + (event.key === 'ArrowDown' ? 1 : entries.length - 1)) % entries.length].focus();
  };
  const label = t(sorts.find(([key]) => key === value)![1]);
  return <div class="sort" ref={root} onKeyDown={keydown}><span aria-hidden="true">{t('排序')}</span>
    <button aria-haspopup="menu" aria-expanded={open} aria-label={`${t('排序')} · ${label}`} onClick={() => setOpen(current => !current)}>
      {label}<Glyph name="down" /></button>
    {open && <div class="menu" role="menu" aria-label={t('排序')}>{sorts.map(([key, label]) =>
      <button key={key} role="menuitemradio" aria-checked={key === value} onClick={() => { change(key); dismiss(); }}>
        {key === value && <Glyph name="check" />}{t(label)}</button>)}</div>}
  </div>;
}
