import { useEffect, useRef, useState } from 'preact/hooks';
import type { ComponentChildren } from 'preact';
import { rpc, type LibraryItem, type LibraryStorage } from '../shared/rpc';
import { externalText, profileText, statuses, storageText, versionLabel } from './library';
import { t } from '../shared/i18n';

// Dialogs about one library entry, in the import wizard's console look: the same head, game card and pill buttons.

function useModal() {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  return dialog;
}
function GameCard({ item, children }: { item: LibraryItem; children?: ComponentChildren }) {
  return <div class="iw-game">
    {item.icon ? <img src={item.icon} alt="" /> : <div class="iw-icon" aria-hidden="true">{[...item.display_name][0] ?? '?'}</div>}
    <div><h3>{item.display_name}</h3>{children}</div>
  </div>;
}
// Imported time in the viewer's locale; the host's ISO value stays in the tooltip.
function importedAt(value: string | null) {
  const time = Date.parse(value ?? '');
  if (!value || !Number.isFinite(time)) return value || t('library.info.notRecorded');
  return new Date(time).toLocaleString(undefined, { year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' });
}

// Async clipboard first; a WebView without it copies a selection of the shown text instead.
async function copyText(text: string, source: Element) {
  try { await navigator.clipboard.writeText(text); return true; } catch { /* fall back to a selection copy */ }
  const selection = getSelection();
  if (!selection) return false;
  const range = document.createRange();
  range.selectNodeContents(source);
  selection.removeAllRanges(); selection.addRange(range);
  try { return document.execCommand('copy'); } finally { selection.removeAllRanges(); }
}
// A fact value with a copy pill; the pill reports success or failure for a moment, then resets.
function Copyable({ text }: { text: string }) {
  const value = useRef<HTMLSpanElement>(null);
  const timer = useRef(0);
  const [state, setState] = useState<'' | 'done' | 'failed'>('');
  useEffect(() => () => clearTimeout(timer.current), []);
  const copy = async () => {
    setState(await copyText(text, value.current!) ? 'done' : 'failed');
    clearTimeout(timer.current);
    timer.current = window.setTimeout(() => setState(''), 1500);
  };
  return <><span ref={value}>{text}</span>
    <button class={`ii-copy ${state}`} aria-live="polite" onClick={copy}>{t(state === 'done' ? 'common.copied' : state === 'failed' ? 'common.copyFailed' : 'common.copy')}</button></>;
}

/** "更多 › 安装信息": install record, disk usage and directories of one entry. */
export function InstallInfoDialog({ item, busy, openDir, onClose }: {
  item: LibraryItem; busy: boolean; openDir?: (kind: 'sandbox' | 'log') => void; onClose: () => void;
}) {
  const dialog = useModal();
  const status = statuses[item.status] ?? { label: 'library.status.unknown', tone: 'warning' };
  const profile = profileText(item), external = externalText(item);
  const hasExternal = item.external.status !== 'not_required';
  // Measured when the dialog opens, since data packs can be large; the list itself stays cheap.
  const [storage, setStorage] = useState<LibraryStorage | 'failed'>();
  useEffect(() => {
    let live = true;
    rpc<LibraryStorage>('library.storage', { installation_id: item.installation_id })
      .then(value => { if (live) setStorage(value); }, () => { if (live) setStorage('failed'); });
    return () => { live = false; };
  }, [item.installation_id]);
  const measured = typeof storage === 'object' ? storage : undefined;
  const path = (label: string, value: string, kind: 'sandbox' | 'log') => <div class="ii-path">
    <small>{label}</small>
    <div class="ii-field"><code>{value}</code>{openDir && <button disabled={busy} onClick={() => openDir(kind)}>{t('common.open')}</button>}</div>
  </div>;
  return <dialog ref={dialog} class="import-wizard install-info" aria-label={t('common.installInfo')} onCancel={event => { event.preventDefault(); onClose(); }}>
    <header class="iw-head"><h2>{t('common.installInfo')}</h2>
      <span class="iw-stage"><i class={`led ${item.running ? 'running' : status.tone}`} />{t(item.running ? 'library.status.running' : status.label)}</span></header>
    <GameCard item={item}><div class="mono">{versionLabel(item)}</div>
      <ul class="iw-tags"><li title={item.imported_at ?? undefined}>{t('library.importedOn', { date: importedAt(item.imported_at) })}</li>
        {profile && <li title={profile.detail}>Profile · {profile.value}</li>}</ul></GameCard>
    <dl class="ii-facts">
      <dt>{t('library.info.installId')}</dt><dd class="mono">{item.installation_id}</dd>
      <dt>{t('library.info.package')}</dt>{item.package ? <dd class="mono ii-copyable"><Copyable text={item.package} /></dd> : <dd class="mono">{t('library.packageUnavailable')}</dd>}
      <dt>{t('common.dataPack')}</dt><dd>{external.value}<small>{external.detail}</small></dd>
      {hasExternal && measured?.external_guest_dir && <><dt>{t('common.mountAt')}</dt><dd class="mono">{measured.external_guest_dir}</dd></>}
    </dl>
    <div class={`ii-usage ${hasExternal ? '' : 'two'}`} aria-busy={storage === undefined}>
      <div><small>APK</small><b>{storageText(storage, measured?.apk_bytes, 'library.info.fileMissing')}</b></div>
      {hasExternal && <div><small>{t('common.dataPack')}</small><b>{storageText(storage, measured?.external_bytes, 'library.info.directoryUnavailable')}</b></div>}
      <div><small>{t('library.info.gameData')}</small><b>{storageText(storage, measured?.sandbox_bytes, 'library.info.notCreated')}</b></div>
    </div>
    {path(t('library.info.sandboxPath'), item.sandbox_path, 'sandbox')}
    {path(t('library.info.logDirectory'), item.log_directory, 'log')}
    <div class="modal-actions"><button autoFocus onClick={onClose}>{t('common.close')}</button></div>
  </dialog>;
}

/** "更多 › 删除游戏": identity, what goes and what stays, and the opt-in to delete saves (off by default). */
export function RemoveGameDialog({ item, busy, onConfirm, onClose }: {
  item: LibraryItem; busy: boolean; onConfirm: (deleteSaves: boolean) => void; onClose: () => void;
}) {
  const dialog = useModal();
  const [saves, setSaves] = useState(false);
  return <dialog ref={dialog} class="import-wizard remove-game" aria-label={t('common.deleteGame')} onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{t('library.delete.confirmTitle', { name: item.display_name })}</h2></header>
    <GameCard item={item}><div class="mono">{item.installation_id}</div><ul class="iw-tags"><li>{versionLabel(item)}</li></ul></GameCard>
    <div class="rg-scope">
      <div class="rg-col gone"><small>{t('library.delete.willDelete')}</small><ul>
        <li>{t('library.delete.libraryApk')}</li><li>{t('library.delete.copiedDataPack')}</li><li>{t('library.delete.settingsAndLogs')}</li>{saves && <li>{t('library.delete.saves')}</li>}</ul></div>
      <div class="rg-col kept"><small>{t('library.delete.kept')}</small><ul>
        <li>{t('library.delete.linkedData')}</li><li>{t('library.delete.originalApk')}</li>{!saves && <li>{t('library.delete.savesRestored')}</li>}</ul></div>
    </div>
    {item.builtin_demo && <p class="rg-note">{t('library.demoGameNote')}</p>}
    <label class={`rg-saves ${saves ? 'on' : ''}`}>
      <input type="checkbox" checked={saves} disabled={busy} onChange={event => setSaves(event.currentTarget.checked)} />
      <span><b>{t('library.delete.alsoSaves')}</b>
        <small>{saves ? t('library.delete.savesGone') : t('library.delete.savesStay')}</small></span>
    </label>
    <div class="modal-actions">
      <button autoFocus disabled={busy} onClick={onClose}>{t('common.cancel')}</button>
      <span class="iw-spacer" />
      <button class="danger" disabled={busy} onClick={() => onConfirm(saves)}>{saves ? t('library.delete.withSaves') : t('library.delete.confirm')}</button>
    </div>
  </dialog>;
}

/** 'common.clearGameData': resets the entry's sandbox to how it was at import; always asks, since nothing can bring it back. */
export function ClearDataDialog({ item, busy, onConfirm, onClose }: {
  item: LibraryItem; busy: boolean; onConfirm: () => void; onClose: () => void;
}) {
  const dialog = useModal();
  return <dialog ref={dialog} class="import-wizard remove-game" aria-label={t('common.clearGameData')} onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{t('library.clear.title', { name: item.display_name })}</h2></header>
    <GameCard item={item}><div class="mono">{item.installation_id}</div><ul class="iw-tags"><li>{versionLabel(item)}</li></ul></GameCard>
    <div class="rg-scope">
      <div class="rg-col gone"><small>{t('library.clear.willClear')}</small><ul>
        <li>{t('library.clear.saves')}</li><li>{t('library.clear.inGameSettings')}</li><li>{t('library.clear.files')}</li></ul></div>
      <div class="rg-col kept"><small>{t('library.delete.kept')}</small><ul>
        <li>{t('library.clear.gameAndData')}</li><li>{t('library.clear.settingsAndKeys')}</li><li>{t('library.clear.logs')}</li><li>{t('library.clear.deviceId')}</li></ul></div>
    </div>
    <p class="rg-warn" role="alert"><b>{t('library.clear.irreversible')}</b>{t('library.clear.backupHint')}</p>
    <div class="modal-actions">
      <button autoFocus disabled={busy} onClick={onClose}>{t('common.cancel')}</button>
      <span class="iw-spacer" />
      <button class="danger" disabled={busy || item.running} title={item.running ? t('library.clear.running') : undefined} onClick={onConfirm}>{t('library.clear.confirm')}</button>
    </div>
  </dialog>;
}
