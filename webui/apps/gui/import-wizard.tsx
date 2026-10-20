import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { importingStage, pause, pickPath, upload, type ExternalMode, type ImportJob } from './import';

export function ImportWizard({ file, defaultExternal = '', onClose, onImported }: {
  file: File | null; defaultExternal?: string; onClose: () => void; onImported: (id: string) => Promise<void>;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const activeJob = useRef('');
  const locked = useRef(false);
  const disposed = useRef(false);
  const [job, setJob] = useState<ImportJob | null>(null);
  const [stage, setStage] = useState('选择安装包');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [external, setExternal] = useState('');
  const [externalGuest, setExternalGuest] = useState('/sdcard');
  const [externalMode, setExternalMode] = useState<ExternalMode>('copy');
  const [committing, setCommitting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const [confirmed, setConfirmed] = useState(false);
  const [source, setSource] = useState(file?.name ?? '');
  const summary = job?.summary;
  const message = (reason: unknown) => reason instanceof Error ? reason.message : String(reason);
  async function poll(result: ImportJob, onPoll?: (job: ImportJob) => void): Promise<ImportJob> {
    while (result.state === 'analyzing' || result.state === 'importing') {
      onPoll?.(result);
      await pause();
      if (disposed.current) throw new Error('向导已关闭');
      result = await rpc<ImportJob>('library.job.poll', { job: result.job });
    }
    if (result.state === 'failed') throw new Error(`${result.message ?? '导入失败'}\n请检查安装包和库目录后重新选择。`);
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
      activeJob.current = ''; setJob(null); setConfirmed(false); setExternal(defaultExternal); setExternalGuest('/sdcard'); setExternalMode('copy');
      let initial: ImportJob;
      if (dropped) {
        setStage('正在读取拖入的 APK…');
        initial = await upload(dropped, id => { activeJob.current = id; }, value => setStage(`正在读取 APK… ${value}%`));
      } else {
        setStage('选择安装包');
        const path = await pickPath('file');
        if (!path) { setStage('已取消文件选择'); return; }
        setSource(path); setStage('正在分析 APK…');
        initial = await rpc<ImportJob>('library.analyze', { path });
        activeJob.current = initial.job;
      }
      setStage('正在分析 APK…');
      setJob(await poll(initial)); setStage('确认导入');
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
  const canImport = summary && job?.state === 'ready' && (!summary.existing_instances || confirmed);
  return <dialog ref={dialog} class="import-wizard" aria-label="导入游戏" onCancel={event => { event.preventDefault(); void close(); }}>
    <h2>导入游戏</h2><p role="status">{stage}</p><p class="selected-file">{source}</p>
    {summary && <>
      <div class="import-hero">{summary.icon && <img src={summary.icon} alt="" />}<div><h3>{summary.display_name}</h3><p class="mono">{summary.package}</p><p>{summary.version_name || '版本'} ({summary.version_code})</p></div></div>
      <dl><dt>API</dt><dd>最低 {summary.min_sdk ?? '未声明'} / 目标 {summary.target_sdk ?? '未声明'}</dd>
        <dt>原生 ABI</dt><dd>{summary.abis.join(' / ') || '无原生库'}</dd>
        {summary.profile && <><dt>Profile</dt><dd>{summary.profile}</dd></>}
        <dt>数据包</dt><dd>{external ? (externalMode === 'copy' ? '复制到游戏库' : '引用原目录') : '可按需选择外部数据目录。'}</dd></dl>
      <div class="external-selection"><p class="selected-file">{external || '未选择数据包目录'}</p>
        <button disabled={busy} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) { setExternal(path); } })}>选择数据包目录</button>
        {external && <button disabled={busy} onClick={() => setExternal('')}>清除</button>}
        {external && <div class="import-mode" role="radiogroup" aria-label="数据包导入方式">
          <label><input type="radio" name="external-mode" checked={externalMode === 'copy'} disabled={busy} onChange={() => setExternalMode('copy')} />复制到游戏库（推荐）<small>导入后原目录可移动或删除；同一磁盘支持克隆时几乎不占额外空间。</small></label>
          <label><input type="radio" name="external-mode" checked={externalMode === 'link'} disabled={busy} onChange={() => setExternalMode('link')} />引用原目录<small>不复制；原目录移动、删除或磁盘断开后游戏无法启动。</small></label>
        </div>}
      </div>
      <label>数据包 guest 挂载目录<input aria-label="数据包 guest 挂载目录" value={externalGuest} disabled={busy}
        onInput={event => setExternalGuest(event.currentTarget.value)} /></label>
      <p>默认 /sdcard；可指定其下的目录。</p>
      {summary.existing_instances > 0 && <label class="import-confirm"><input type="checkbox" checked={confirmed} disabled={busy} onChange={event => setConfirmed(event.currentTarget.checked)} />游戏库中已有 {summary.existing_instances} 份该游戏；另外添加一份，现有游戏及存档保留。</label>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">{committing
      ? <button disabled={cancelling} onClick={() => void cancelImport()}>{cancelling ? '正在取消…' : '取消导入'}</button>
      : <button disabled={busy} onClick={() => void close()}>取消</button>}
      <button disabled={busy} onClick={() => void analyze(null)}>重新选择 APK</button>
      <button class="primary" disabled={busy || !canImport} onClick={() => void run(async () => {
        setStage(importingStage({ job: activeJob.current, state: 'importing' })); setCommitting(true); setCancelling(false);
        try {
          const initial = await rpc<ImportJob>('library.import', { job: activeJob.current, new_instance: true, external_guest_dir: externalGuest,
            ...(external ? { external_dir: external, external_mode: externalMode } : {}) });
          const result = await poll(initial, value => setStage(importingStage(value))); setJob(result);
          if (result.state === 'cancelled') { activeJob.current = ''; setStage('已取消导入，可重新选择 APK'); return; }
          if (result.state !== 'imported' || !result.installation_id) throw new Error('宿主未返回导入结果。');
          setStage('已加入游戏库'); activeJob.current = '';
          await onImported(result.installation_id);
        } finally { if (!disposed.current) { setCommitting(false); setCancelling(false); } }
      })}>导入到游戏库</button></div>
  </dialog>;
}
