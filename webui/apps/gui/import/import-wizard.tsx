import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { guestPathError, importingStage, normalizeGuestPath, pause, pickPath, upload, type ExternalMode, type ImportJob } from './import';
import { MountPreview, useDataDirectory } from './mount-preview';
import { gameName, t } from '../shared/i18n';

const MODES: { value: ExternalMode; label: string }[] = [
  { value: 'copy', label: '复制到游戏库' },
  { value: 'link', label: '引用原目录' },
];

export function ImportWizard({ file, defaultExternal = '', onClose, onImported }: {
  file: File | null; defaultExternal?: string; onClose: () => void; onImported: (id: string) => Promise<void>;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const activeJob = useRef('');
  const locked = useRef(false);
  const disposed = useRef(false);
  const [job, setJob] = useState<ImportJob | null>(null);
  const [stage, setStage] = useState(() => t('选择安装包'));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [external, setExternal] = useState('');
  const [externalGuest, setExternalGuest] = useState('/sdcard');
  // Typed mount positions win over the one inferred from the picked directory.
  const [guestEdited, setGuestEdited] = useState(false);
  const [externalMode, setExternalMode] = useState<ExternalMode>('copy');
  const [committing, setCommitting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const [confirmed, setConfirmed] = useState(false);
  const [source, setSource] = useState(file?.name ?? '');
  const summary = job?.summary;
  const guest = normalizeGuestPath(externalGuest);
  const guestError = external ? guestPathError(guest) : null;
  const message = (reason: unknown) => reason instanceof Error ? reason.message : String(reason);
  async function poll(result: ImportJob, onPoll?: (job: ImportJob) => void): Promise<ImportJob> {
    while (result.state === 'analyzing' || result.state === 'importing') {
      onPoll?.(result);
      await pause();
      if (disposed.current) throw new Error(t('向导已关闭'));
      result = await rpc<ImportJob>('library.job.poll', { job: result.job });
    }
    if (result.state === 'failed') throw new Error(`${t(result.message ?? '导入失败')}\n${t('请检查安装包和库目录后重新选择。')}`);
    return result;
  }
  async function run(action: () => Promise<void>) {
    if (locked.current) return;
    locked.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { if (!disposed.current) setError(message(reason)); }
    finally { locked.current = false; if (!disposed.current) setBusy(false); }
  }
  async function analyze(dropped: File | null) {
    await run(async () => {
      if (activeJob.current) await rpc('library.job.cancel', { job: activeJob.current });
      activeJob.current = ''; setJob(null); setConfirmed(false); setExternal(defaultExternal); setExternalGuest('/sdcard'); setGuestEdited(false); setExternalMode('copy');
      let initial: ImportJob;
      if (dropped) {
        setStage(t('正在读取拖入的 APK…'));
        initial = await upload(dropped, id => { activeJob.current = id; }, value => setStage(t('正在读取 APK… {percent}%', { percent: value })));
      } else {
        setStage(t('选择安装包'));
        const path = await pickPath('file');
        if (!path) { setStage(t('已取消文件选择')); return; }
        setSource(path); setStage(t('正在分析 APK…'));
        initial = await rpc<ImportJob>('library.analyze', { path });
        activeJob.current = initial.job;
      }
      setStage(t('正在分析 APK…'));
      setJob(await poll(initial)); setStage('');
    });
  }
  async function close() {
    if (locked.current) return;
    await run(async () => {
      if (activeJob.current && job?.state !== 'imported') await rpc('library.job.cancel', { job: activeJob.current });
      onClose();
    });
  }
  // Copying a data pack can take a while; the host stops it before publication.
  async function cancelImport() {
    if (!activeJob.current || cancelling) return;
    setCancelling(true);
    try { await rpc('library.job.cancel', { job: activeJob.current }); }
    catch (reason) { if (!disposed.current) setError(message(reason)); }
  }
  useEffect(() => {
    dialog.current!.showModal();
    void analyze(file);
    return () => { disposed.current = true; dialog.current?.close(); };
  }, []);
  // The host lists the chosen directory so the preview shows real guest paths.
  const { data, error: dataError } = useDataDirectory(external, summary?.package);
  const suggested = data?.suggested_guest_dir ?? null;
  useEffect(() => { if (data && !guestEdited) setExternalGuest(suggested ?? '/sdcard'); }, [data, guestEdited]);
  const canImport = summary && job?.state === 'ready' && !guestError && (!summary.existing_instances || confirmed);
  const led = error || job?.state === 'failed' ? 'danger' : committing || busy ? 'running' : summary ? 'ready' : '';
  const sourceName = source.split(/[\\/]/).pop();
  return <dialog ref={dialog} class="import-wizard" aria-label={t('导入游戏')} onCancel={event => { event.preventDefault(); void close(); }}>
    <header class="iw-head"><h2>{t('导入游戏')}</h2>{stage && <span class="iw-stage" role="status"><i class={`led ${led}`} />{stage}</span>}</header>
    <div class="iw-source">
      <p class="iw-file" title={source}><bdi>{sourceName}</bdi></p>
      <button disabled={busy} aria-label={t('重新选择 APK')} onClick={() => void analyze(null)}>{source ? t('更换') : t('选择 APK')}</button>
    </div>
    {summary && <>
      <section class="iw-game">
        {summary.icon ? <img src={summary.icon} alt="" /> : <span class="iw-icon">?</span>}
        <div>
          <h3>{gameName(summary)}</h3>
          <p class="mono">{summary.package}</p>
          <ul class="iw-tags">
            <li>{summary.version_name || t('版本')} ({summary.version_code})</li>
            <li>API {summary.min_sdk ?? '?'}{summary.target_sdk ? ` → ${summary.target_sdk}` : ''}</li>
            <li>{summary.abis.join(' / ') || t('无原生库')}</li>
            {summary.profile && <li>Profile {summary.profile}</li>}
          </ul>
        </div>
      </section>
      <section class="iw-data">
        <div class="iw-title"><b>{t('数据包')}</b><small>{t('可选')}</small></div>
        <div class="iw-pick">
          <span class={external ? 'iw-host' : 'iw-host empty'} title={external}><span><bdi>{external || t('未选择，不需要可跳过')}</bdi></span></span>
          <button disabled={busy} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) setExternal(path); })}>{external ? t('更换') : t('选择目录')}</button>
          {external && <button disabled={busy} onClick={() => setExternal('')}>{t('清除')}</button>}
        </div>
        {external && <>
          <div class="iw-row"><span>{t('导入方式')}</span>
            <div class="seg" role="radiogroup" aria-label={t('数据包导入方式')}>{MODES.map(item =>
              <button role="radio" aria-checked={item.value === externalMode} class={item.value === externalMode ? 'on' : ''} disabled={busy}
                onClick={() => setExternalMode(item.value)}>{t(item.label)}</button>)}</div>
          </div>
          {externalMode === 'link' && <p class="iw-note warn">{t('原目录移动、删除或磁盘断开后，游戏将无法启动。')}</p>}
          <div class="iw-row"><span>{t('挂载位置')}</span>
            <label class={guestError ? 'iw-txt bad' : 'iw-txt'}><input aria-label={t('数据包挂载位置')} value={externalGuest} disabled={busy} spellcheck={false}
              onInput={event => { setExternalGuest(event.currentTarget.value); setGuestEdited(true); }} /></label>
          </div>
          {guestError && <p class="iw-note bad">{guestError}</p>}
          <MountPreview caption={t('挂载效果预览')} guest={guest} invalid={!!guestError} data={data} error={dataError} />
        </>}
      </section>
      {summary.existing_instances > 0 && <label class="iw-check"><input type="checkbox" checked={confirmed} disabled={busy} onChange={event => setConfirmed(event.currentTarget.checked)} />{t('游戏库中已有 {n} 份该游戏；另外添加一份，现有游戏及存档保留。', { n: summary.existing_instances })}</label>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">
      {committing
        ? <button disabled={cancelling} onClick={() => void cancelImport()}>{cancelling ? t('正在取消…') : t('取消导入')}</button>
        : <button disabled={busy} onClick={() => void close()}>{t('取消')}</button>}
      <button class="primary" disabled={busy || !canImport} onClick={() => void run(async () => {
        setStage(importingStage({ job: activeJob.current, state: 'importing' })); setCommitting(true); setCancelling(false);
        try {
          const initial = await rpc<ImportJob>('library.import', { job: activeJob.current, new_instance: true, external_guest_dir: external ? guest : '/sdcard',
            ...(external ? { external_dir: external, external_mode: externalMode } : {}) });
          const result = await poll(initial, value => setStage(importingStage(value))); setJob(result);
          if (result.state === 'cancelled') { activeJob.current = ''; setStage(t('已取消导入，可重新选择 APK')); return; }
          if (result.state !== 'imported' || !result.installation_id) throw new Error(t('宿主未返回导入结果。'));
          setStage(t('已加入游戏库')); activeJob.current = '';
          await onImported(result.installation_id);
        } finally { if (!disposed.current) { setCommitting(false); setCancelling(false); } }
      })}>{t('导入到游戏库')}</button>
    </div>
  </dialog>;
}
