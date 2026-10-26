import { useEffect, useRef, useState } from 'preact/hooks';
import type { ComponentChildren } from 'preact';
import type { LibraryItem } from './rpc';
import { statuses, versionLabel } from './library';
import { t } from './i18n';

// Dialogs about one library entry, in the import wizard's console look: the same head, game card and pill buttons.
export interface RuntimeInfo { status: string; detail: string; key_editor_status: string; key_editor_detail: string }

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

/** "更多 › 安装信息": install record, directories and runtime state of one entry. */
export function InstallInfoDialog({ item, runtime, busy, openDir, onClose }: {
  item: LibraryItem; runtime?: RuntimeInfo; busy: boolean; openDir: (kind: 'sandbox' | 'log') => void; onClose: () => void;
}) {
  const dialog = useModal();
  const status = statuses[item.status] ?? { label: '状态未知', tone: 'warning' };
  const tone = (ready: boolean) => !runtime ? '' : ready ? 'ready' : 'warning';
  const path = (label: string, value: string, kind: 'sandbox' | 'log') => <div class="ii-path">
    <small>{label}</small>
    <div class="ii-field"><code>{value}</code><button disabled={busy} onClick={() => openDir(kind)}>{t('打开')}</button></div>
  </div>;
  return <dialog ref={dialog} class="import-wizard install-info" aria-label={t('安装信息')} onCancel={event => { event.preventDefault(); onClose(); }}>
    <header class="iw-head"><h2>{t('安装信息')}</h2>
      <span class="iw-stage"><i class={`led ${item.running ? 'running' : status.tone}`} />{t(item.running ? '运行中' : status.label)}</span></header>
    <GameCard item={item}><div class="mono">{versionLabel(item)}</div>
      <ul class="iw-tags"><li title={item.imported_at ?? undefined}>{t('导入于 {date}', { date: importedAt(item.imported_at) })}</li>
        {item.profile && <li title={item.profile.detail}>Profile · {item.profile.value}</li>}</ul></GameCard>
    <dl class="ii-facts">
      <dt>{t('安装 ID')}</dt><dd class="mono">{item.installation_id}</dd>
      <dt>{t('包名')}</dt><dd class="mono">{item.package || t('包名不可用')}</dd>
      <dt>{t('数据包')}</dt><dd>{item.external.value}{item.external.detail && <small>{item.external.detail}</small>}</dd>
    </dl>
    {path(t('沙盒路径'), item.sandbox_path, 'sandbox')}
    {path(t('日志目录'), item.log_directory, 'log')}
    <div class="ii-runtime">
      <div><i class={`led ${tone(runtime?.status === 'ready')}`} /><b>Dashboard</b><span>{runtime?.detail ?? t('游戏尚未运行')}</span></div>
      <div><i class={`led ${tone(runtime?.key_editor_status === 'ready')}`} /><b>{t('按键设置')}</b>
        <span>{runtime?.key_editor_detail ?? t('先启动游戏；运行中按 F1 查看键位、F2 打开按键设置。')}</span></div>
    </div>
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
