import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { pickPath, type DataHint } from './import';
import { has } from '../shared/features';
import { MountPreview, useDataDirectory } from './mount-preview';
import { t, tj } from '../shared/i18n';

/** ADR-0146: shown after a game exits when its missing files point at a better data mount. */
export function DataHintDialog({ id, name, initial, onClose, onApplied }: {
  id: string; name: string; initial: DataHint; onClose: () => void; onApplied: (relaunch: boolean) => Promise<void>;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const [hint, setHint] = useState(initial);
  const [picked, setPicked] = useState('');
  const [choice, setChoice] = useState(0);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  const candidates = hint.candidates ?? [];
  const candidate = candidates[Math.min(choice, candidates.length - 1)];
  const { data, error: dataError } = useDataDirectory(hint.kind === 'remount' ? picked || hint.external_dir || '' : '');
  async function run(action: () => Promise<void>) {
    if (busy) return;
    setBusy(true); setError('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { setBusy(false); }
  }
  const pick = () => run(async () => {
    const path = await pickPath('directory');
    if (!path) return;
    const next = await rpc<DataHint>('library.data_hint', { installation_id: id, external_dir: path });
    if (next.kind !== 'remount') throw new Error(t('dataHint.noMatchingFiles'));
    setPicked(path); setHint(next); setChoice(0);
  });
  const apply = (relaunch: boolean) => run(async () => {
    await rpc('library.data_mount.apply', { installation_id: id, external_guest_dir: candidate.guest_dir, ...(picked ? { external_dir: picked } : {}) });
    await onApplied(relaunch);
  });
  const remount = hint.kind === 'remount' && candidate;
  return <dialog ref={dialog} class="import-wizard data-hint" aria-label={t('dataHint.title')} onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{remount ? t('dataHint.wrongMount') : t('dataHint.needsData')}</h2><span class="iw-stage"><i class="led warning" />{name}</span></header>
    {remount ? <>
      <p class="dh-lead">{t(picked ? 'dataHint.foundInChosen' : 'dataHint.foundInData', { n: candidate.explained })}</p>
      <div class="dh-move">
        <div><small>{t('dataHint.currentMount')}</small><code>{picked ? t('dataHint.noDataPack') : hint.current_guest_dir}</code></div>
        <span aria-hidden="true">→</span>
        <div><small>{t('dataHint.suggestedMount')}</small><code class="good">{candidate.guest_dir}</code></div>
      </div>
      {candidates.length > 1 && <div class="dh-choices" role="radiogroup" aria-label={t('dataHint.candidates')}>{candidates.map((item, index) =>
        <button role="radio" aria-checked={index === choice} class={index === choice ? 'on' : ''} disabled={busy} onClick={() => setChoice(index)}>
          <code>{item.guest_dir}</code><small>{t('dataHint.fileCount', { n: item.explained })}</small></button>)}</div>}
      <div class="dh-examples"><small>{t('dataHint.missingExamples')}</small><ul>{candidate.examples.map(path => <li><code>{path}</code></li>)}</ul></div>
      <MountPreview caption={t('dataHint.afterMount')} guest={candidate.guest_dir} data={data} error={dataError} />
      <p class="iw-note dh-foot">{t('dataHint.onlyThisGame')}</p>
    </> : <>
      <p class="dh-lead">{t('dataHint.noDataConfigured', { n: hint.missing ?? 0 })}</p>
      <div class="dh-examples"><small>{t('dataHint.missingExamples')}</small><ul>{(hint.examples ?? []).map(path => <li><code>{path}</code></li>)}</ul></div>
      {hint.search_root && <p class="iw-note dh-foot">{tj('dataHint.readsAround', { path: <code>{hint.search_root}</code> })}</p>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">
      <button disabled={busy} onClick={onClose}>{t('dataHint.ignore')}</button>
      <span class="iw-spacer" />
      {remount ? <>
        <button disabled={busy} onClick={() => void apply(false)}>{t('dataHint.changeOnly')}</button>
        <button class="primary" disabled={busy} onClick={() => void apply(true)}>{t('dataHint.mountAndRelaunch')}</button>
      </> : has('path_settings') && <button class="primary" disabled={busy} onClick={() => void pick()}>{t('dataHint.chooseFolder')}</button>}
    </div>
  </dialog>;
}
