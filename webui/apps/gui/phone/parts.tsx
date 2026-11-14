import type { ComponentChildren } from 'preact';
import { useEffect, useRef } from 'preact/hooks';
import { t } from '../shared/i18n';

// Pieces every phone screen uses: the top bar's back and icon buttons, and the bottom sheet. Sheets are modal
// <dialog>s, so the back key closes the top one like any other dialog (phone-app.tsx).

export function BackButton({ disabled, onClick }: { disabled?: boolean; onClick: () => void }) {
  return <button class="ph-icon" aria-label={t('返回')} disabled={disabled} onClick={onClick}>
    <svg class="glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7" /></svg></button>;
}
export function MoreIcon() {
  return <svg class="glyph" viewBox="0 0 24 24" aria-hidden="true"><circle cx="5" cy="12" r="1.2" /><circle cx="12" cy="12" r="1.2" /><circle cx="19" cy="12" r="1.2" /></svg>;
}
export function NextIcon() {
  return <svg class="glyph ph-next" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5l7 7-7 7" /></svg>;
}

// Bottom sheet: slides up over the screen; the back key, a tap on the dimmed screen or a choice closes it.
export function Sheet({ title, onClose, children }: { title?: string; onClose: () => void; children: ComponentChildren }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  return <dialog ref={dialog} class="ph-sheet" aria-label={title} onCancel={event => { event.preventDefault(); onClose(); }}>
    <i class="ph-grip" aria-hidden="true" />
    {title && <h2>{title}</h2>}
    <div class="ph-list">{children}</div>
  </dialog>;
}
// One sheet entry; `checked` marks the current choice of a picker, `danger` a destructive action.
export function SheetItem({ label, note, checked, danger, disabled, onClick }: {
  label: string; note?: string; checked?: boolean; danger?: boolean; disabled?: boolean; onClick: () => void;
}) {
  return <button class={`ph-item ${danger ? 'danger' : ''}`} role={checked === undefined ? undefined : 'radio'} aria-checked={checked}
    disabled={disabled} onClick={onClick}>
    <span><b>{label}</b>{note && <small>{note}</small>}</span>
    {checked && <svg class="glyph" viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12.5l4.5 4.5L19 7" /></svg>}
  </button>;
}
