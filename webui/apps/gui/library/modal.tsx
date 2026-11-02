import type { ComponentChildren } from 'preact';
import { useEffect, useRef } from 'preact/hooks';
import { t } from '../shared/i18n';

// Notice dialog for the library: errors and the running-games list, dismissed with Esc or its close button.
export function Modal({ title, onDismiss, children }: { title: string; onDismiss: () => void; children: ComponentChildren }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  return <dialog ref={dialog} aria-label={title} onCancel={event => { event.preventDefault(); onDismiss(); }}>
    <h2>{title}</h2>{children}<div class="modal-actions"><button autoFocus onClick={onDismiss}>{t('关闭')}</button></div>
  </dialog>;
}
