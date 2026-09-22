import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { pause, pickPath, upload, type ImportJob } from './import';

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
  const [confirmed, setConfirmed] = useState(false);
  const [skip, setSkip] = useState(false);
  const [source, setSource] = useState(file?.name ?? '');
  const summary = job?.summary;
  const message = (reason: unknown) => reason instanceof Error ? reason.message : String(reason);
  async function poll(result: ImportJob): Promise<ImportJob> {
    while (result.state === 'analyzing' || result.state === 'importing') {
      await pause();
      if (disposed.current) throw new Error('向导已关闭');
      result = await rpc<ImportJob>('library.job.poll', { job: result.job });
    }
    if (result.state === 'failed') throw new Error(`${result.message ?? '导入失败'}\n请检查安装包、Profile 配置和库目录后重新选择。`);
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
      activeJob.current = ''; setJob(null); setConfirmed(false); setExternal(defaultExternal); setSkip(false);
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
  useEffect(() => {
    dialog.current!.showModal();
    void analyze(file);
    return () => { disposed.current = true; dialog.current?.close(); };
  }, []);
  const canImport = summary && job?.state === 'ready' && (!summary.existing_instances || confirmed) &&
    (!summary.requires_external || external || skip);
  return <dialog ref={dialog} class="import-wizard" aria-label="导入游戏" onCancel={event => { event.preventDefault(); void close(); }}>
    <h2>导入游戏</h2><p role="status">{stage}</p><p class="selected-file">{source}</p>
    {summary && <>
      <div class="import-hero">{summary.icon && <img src={summary.icon} alt="" />}<div><h3>{summary.display_name}</h3><p class="mono">{summary.package}</p><p>{summary.version_name || '版本'} ({summary.version_code})</p></div></div>
      <dl><dt>API</dt><dd>最低 {summary.min_sdk ?? '未声明'} / 目标 {summary.target_sdk ?? '未声明'}</dd>
        <dt>原生 ABI</dt><dd>{summary.abis.join(' / ') || '无原生库'}</dd>
        <dt>Profile</dt><dd>{summary.profile ?? '无精确匹配，将按通用 APK 入库；兼容性尚未确认。'}</dd>
        <dt>数据包</dt><dd>{summary.requires_external === null ? '无 Profile，无法判断是否需要。' : summary.requires_external ? '此游戏需要外部数据包。' : 'Profile 未要求外部数据包。'}</dd></dl>
      <div class="external-selection"><p class="selected-file">{external || '未选择数据包目录'}</p>
        <button disabled={busy} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) { setExternal(path); setSkip(false); } })}>选择数据包目录</button>
        {external && <button disabled={busy} onClick={() => setExternal('')}>清除</button>}
        {summary.requires_external && !external && <label><input type="checkbox" checked={skip} disabled={busy} onChange={event => setSkip(event.currentTarget.checked)} />暂时跳过，入库后显示“缺数据包”</label>}
      </div>
      {summary.existing_instances > 0 && <label class="import-confirm"><input type="checkbox" checked={confirmed} disabled={busy} onChange={event => setConfirmed(event.currentTarget.checked)} />已有 {summary.existing_instances} 个同包实例；新建实例，保留现有游戏及存档。</label>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions"><button disabled={busy} onClick={() => void close()}>取消</button>
      <button disabled={busy} onClick={() => void analyze(null)}>重新选择 APK</button>
      <button class="primary" disabled={busy || !canImport} onClick={() => void run(async () => {
        setStage('正在入库，请勿关闭…');
        const initial = await rpc<ImportJob>('library.import', { job: activeJob.current, new_instance: true, ...(external ? { external_dir: external } : {}) });
        const result = await poll(initial); setJob(result);
        if (result.state !== 'imported' || !result.installation_id) throw new Error('宿主未返回入库结果。');
        setStage('入库成功'); activeJob.current = '';
        await onImported(result.installation_id);
      })}>新建实例并入库</button></div>
  </dialog>;
}
