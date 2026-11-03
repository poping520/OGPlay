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
  if (!value || !Number.isFinite(time)) return value || t('未记录');
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
    <button class={`ii-copy ${state}`} aria-live="polite" onClick={copy}>{t(state === 'done' ? '已复制' : state === 'failed' ? '复制失败' : '复制')}</button></>;
}

/** "更多 › 安装信息": install record, disk usage and directories of one entry. */
export function InstallInfoDialog({ item, busy, openDir, onClose }: {
  item: LibraryItem; busy: boolean; openDir: (kind: 'sandbox' | 'log') => void; onClose: () => void;
}) {
  const dialog = useModal();
  const status = statuses[item.status] ?? { label: '状态未知', tone: 'warning' };
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
    <div class="ii-field"><code>{value}</code><button disabled={busy} onClick={() => openDir(kind)}>{t('打开')}</button></div>
  </div>;
  return <dialog ref={dialog} class="import-wizard install-info" aria-label={t('安装信息')} onCancel={event => { event.preventDefault(); onClose(); }}>
    <header class="iw-head"><h2>{t('安装信息')}</h2>
      <span class="iw-stage"><i class={`led ${item.running ? 'running' : status.tone}`} />{t(item.running ? '运行中' : status.label)}</span></header>
    <GameCard item={item}><div class="mono">{versionLabel(item)}</div>
      <ul class="iw-tags"><li title={item.imported_at ?? undefined}>{t('导入于 {date}', { date: importedAt(item.imported_at) })}</li>
        {profile && <li title={profile.detail}>Profile · {profile.value}</li>}</ul></GameCard>
    <dl class="ii-facts">
      <dt>{t('安装 ID')}</dt><dd class="mono">{item.installation_id}</dd>
      <dt>{t('包名')}</dt>{item.package ? <dd class="mono ii-copyable"><Copyable text={item.package} /></dd> : <dd class="mono">{t('包名不可用')}</dd>}
      <dt>{t('数据包')}</dt><dd>{external.value}<small>{external.detail}</small></dd>
      {hasExternal && measured?.external_guest_dir && <><dt>{t('挂载位置')}</dt><dd class="mono">{measured.external_guest_dir}</dd></>}
    </dl>
    <div class={`ii-usage ${hasExternal ? '' : 'two'}`} aria-busy={storage === undefined}>
      <div><small>APK</small><b>{storageText(storage, measured?.apk_bytes, '文件缺失')}</b></div>
      {hasExternal && <div><small>{t('数据包')}</small><b>{storageText(storage, measured?.external_bytes, '目录不可用')}</b></div>}
      <div><small>{t('游戏数据')}</small><b>{storageText(storage, measured?.sandbox_bytes, '尚未生成')}</b></div>
    </div>
    {path(t('沙盒路径'), item.sandbox_path, 'sandbox')}
    {path(t('日志目录'), item.log_directory, 'log')}
    <div class="modal-actions"><button autoFocus onClick={onClose}>{t('关闭')}</button></div>
  </dialog>;
}

/** "更多 › 删除游戏": identity, what goes and what stays, and the opt-in to delete saves (off by default). */
export function RemoveGameDialog({ item, busy, onConfirm, onClose }: {
  item: LibraryItem; busy: boolean; onConfirm: (deleteSaves: boolean) => void; onClose: () => void;
}) {
  const dialog = useModal();
  const [saves, setSaves] = useState(false);
  return <dialog ref={dialog} class="import-wizard remove-game" aria-label={t('删除游戏')} onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{t('确认删除「{name}」？', { name: item.display_name })}</h2></header>
    <GameCard item={item}><div class="mono">{item.installation_id}</div><ul class="iw-tags"><li>{versionLabel(item)}</li></ul></GameCard>
    <div class="rg-scope">
      <div class="rg-col gone"><small>{t('将删除')}</small><ul>
        <li>{t('库内 APK')}</li><li>{t('复制入库的数据包')}</li><li>{t('设置和日志')}</li>{saves && <li>{t('存档')}</li>}</ul></div>
      <div class="rg-col kept"><small>{t('保留')}</small><ul>
        <li>{t('引用的原数据目录')}</li><li>{t('原始 APK')}</li>{!saves && <li>{t('存档（重新导入时恢复）')}</li>}</ul></div>
    </div>
    {item.builtin_demo && <p class="rg-note">{t('这是 OGPlay 自带的示例游戏。删除后会关闭「展示内置 Demo 游戏」，可在设置 › 常规中重新打开。')}</p>}
    <label class={`rg-saves ${saves ? 'on' : ''}`}>
      <input type="checkbox" checked={saves} disabled={busy} onChange={event => setSaves(event.currentTarget.checked)} />
      <span><b>{t('同时删除存档（不可恢复）')}</b>
        <small>{saves ? t('存档将被永久删除；重新导入后从头开始。') : t('存档保留在沙盒中；重新导入同一游戏时自动恢复。')}</small></span>
    </label>
    <div class="modal-actions">
      <button autoFocus disabled={busy} onClick={onClose}>{t('取消')}</button>
      <span class="iw-spacer" />
      <button class="danger" disabled={busy} onClick={() => onConfirm(saves)}>{saves ? t('删除游戏和存档') : t('确认删除')}</button>
    </div>
  </dialog>;
}

/** "清空游戏数据": resets the entry's sandbox to how it was at import; always asks, since nothing can bring it back. */
export function ClearDataDialog({ item, busy, onConfirm, onClose }: {
  item: LibraryItem; busy: boolean; onConfirm: () => void; onClose: () => void;
}) {
  const dialog = useModal();
  return <dialog ref={dialog} class="import-wizard remove-game" aria-label={t('清空游戏数据')} onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{t('清空「{name}」的游戏数据？', { name: item.display_name })}</h2></header>
    <GameCard item={item}><div class="mono">{item.installation_id}</div><ul class="iw-tags"><li>{versionLabel(item)}</li></ul></GameCard>
    <div class="rg-scope">
      <div class="rg-col gone"><small>{t('将清空')}</small><ul>
        <li>{t('存档与游戏进度')}</li><li>{t('游戏内设置与登录')}</li><li>{t('游戏下载或生成的文件')}</li></ul></div>
      <div class="rg-col kept"><small>{t('保留')}</small><ul>
        <li>{t('游戏与数据包')}</li><li>{t('游戏设置与按键')}</li><li>{t('运行日志')}</li><li>{t('设备 ID（ANDROID_ID）')}</li></ul></div>
    </div>
    <p class="rg-warn" role="alert"><b>{t('此操作无法撤销')}</b>{t('游戏会像刚导入时一样从头开始。需要保留进度时，先在“打开沙盒目录”中备份。')}</p>
    <div class="modal-actions">
      <button autoFocus disabled={busy} onClick={onClose}>{t('取消')}</button>
      <span class="iw-spacer" />
      <button class="danger" disabled={busy || item.running} title={item.running ? t('游戏运行中，退出后才能清空数据。') : undefined} onClick={onConfirm}>{t('清空数据')}</button>
    </div>
  </dialog>;
}
