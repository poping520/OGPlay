import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { describeData, guestPathError, importingStage, normalizeGuestPath, pause, pickPath, upload, type DataDirectory, type ExternalMode, type ImportJob } from './import';

const MODES: { value: ExternalMode; label: string; hint: string }[] = [
  { value: 'copy', label: '复制到游戏库', hint: '导入后原目录可移动或删除；同一磁盘支持克隆时几乎不占额外空间。' },
  { value: 'link', label: '引用原目录', hint: '不复制；原目录移动、删除或磁盘断开后游戏无法启动。' },
];

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
  const [data, setData] = useState<DataDirectory | null>(null);
  const [dataError, setDataError] = useState('');
  const [externalGuest, setExternalGuest] = useState('/sdcard');
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
  // The host lists the chosen directory so the preview shows real guest paths.
  useEffect(() => {
    setData(null); setDataError('');
    if (!external) return;
    let current = true;
    rpc<DataDirectory>('library.external.inspect', { path: external })
      .then(value => { if (current) setData(value); })
      .catch(reason => { if (current) setDataError(message(reason)); });
    return () => { current = false; };
  }, [external]);
  const canImport = summary && job?.state === 'ready' && !guestError && (!summary.existing_instances || confirmed);
  const led = error || job?.state === 'failed' ? 'danger' : committing || busy ? 'running' : summary ? 'ready' : '';
  const mode = MODES.find(item => item.value === externalMode)!;
  const shown = data?.entries ?? [];
  const hidden = data ? data.entry_count - shown.length : 0;
  return <dialog ref={dialog} class="import-wizard" aria-label="导入游戏" onCancel={event => { event.preventDefault(); void close(); }}>
    <header class="iw-head"><h2>导入游戏</h2><span class="iw-stage" role="status"><i class={`led ${led}`} />{stage}</span></header>
    {source && <p class="iw-file" title={source}><bdi>{source}</bdi></p>}
    {summary && <>
      <section class="iw-game">
        {summary.icon ? <img src={summary.icon} alt="" /> : <span class="iw-icon">?</span>}
        <div>
          <h3>{summary.display_name}</h3>
          <p class="mono">{summary.package}</p>
          <ul class="iw-tags">
            <li>{summary.version_name || '版本'} ({summary.version_code})</li>
            <li>API {summary.min_sdk ?? '?'}{summary.target_sdk ? ` → ${summary.target_sdk}` : ''}</li>
            <li>{summary.abis.join(' / ') || '无原生库'}</li>
            {summary.profile && <li>Profile {summary.profile}</li>}
          </ul>
        </div>
      </section>
      <section class="iw-data">
        <div class="iw-title"><b>数据包</b><small>可选 · 游戏在 sdcard 上读取的资源目录</small></div>
        <div class="iw-pick">
          <span class={external ? 'iw-host' : 'iw-host empty'} title={external}><span><bdi>{external || '未选择；不需要外部数据的游戏可跳过'}</bdi></span></span>
          <button disabled={busy} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) setExternal(path); })}>{external ? '更换' : '选择目录'}</button>
          {external && <button disabled={busy} onClick={() => setExternal('')}>清除</button>}
        </div>
        {external && <>
          <div class="iw-row"><span>导入方式</span>
            <div class="seg" role="radiogroup" aria-label="数据包导入方式">{MODES.map(item =>
              <button role="radio" aria-checked={item.value === externalMode} class={item.value === externalMode ? 'on' : ''} disabled={busy}
                onClick={() => setExternalMode(item.value)}>{item.label}</button>)}</div>
          </div>
          <p class="iw-note">{mode.hint}</p>
          <div class="iw-row"><span>挂载位置</span>
            <label class={guestError ? 'iw-txt bad' : 'iw-txt'}><input aria-label="数据包挂载位置" value={externalGuest} disabled={busy} spellcheck={false}
              onInput={event => setExternalGuest(event.currentTarget.value)} /></label>
          </div>
          <p class={guestError ? 'iw-note bad' : 'iw-note'}>{guestError ?? '目录内容会出现在此 guest 路径下；按游戏实际读取的位置填写，例如 /sdcard/gameloft/games/<游戏名>。'}</p>
          <figure class="iw-mount" aria-label="挂载预览">
            <figcaption><span>{externalMode === 'copy' ? '复制到游戏库后挂载' : '直接挂载原目录'}</span>{data && <small>{describeData(data)}</small>}</figcaption>
            <code class={guestError ? 'iw-root bad' : 'iw-root'}>{guestError ? '挂载位置无效，修正后显示' : `${guest}/`}</code>
            {dataError ? <p class="iw-note bad">{dataError}</p> : !data ? <p class="iw-note">正在读取目录…</p> :
              <ul class="iw-tree">
                {shown.map((entry, index) => <li><i>{index === shown.length - 1 && hidden <= 0 ? '└─' : '├─'}</i><code>{entry.name}{entry.directory ? '/' : ''}</code></li>)}
                {hidden > 0 && <li class="more"><i>└─</i>另 {hidden} 项</li>}
                {!data.entry_count && <li class="more"><i>└─</i>空目录</li>}
              </ul>}
          </figure>
        </>}
      </section>
      {summary.existing_instances > 0 && <label class="iw-check"><input type="checkbox" checked={confirmed} disabled={busy} onChange={event => setConfirmed(event.currentTarget.checked)} />游戏库中已有 {summary.existing_instances} 份该游戏；另外添加一份，现有游戏及存档保留。</label>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">
      <button disabled={busy} onClick={() => void analyze(null)}>重新选择 APK</button>
      <span class="iw-spacer" />
      {committing
        ? <button disabled={cancelling} onClick={() => void cancelImport()}>{cancelling ? '正在取消…' : '取消导入'}</button>
        : <button disabled={busy} onClick={() => void close()}>取消</button>}
      <button class="primary" disabled={busy || !canImport} onClick={() => void run(async () => {
        setStage(importingStage({ job: activeJob.current, state: 'importing' })); setCommitting(true); setCancelling(false);
        try {
          const initial = await rpc<ImportJob>('library.import', { job: activeJob.current, new_instance: true, external_guest_dir: external ? guest : '/sdcard',
            ...(external ? { external_dir: external, external_mode: externalMode } : {}) });
          const result = await poll(initial, value => setStage(importingStage(value))); setJob(result);
          if (result.state === 'cancelled') { activeJob.current = ''; setStage('已取消导入，可重新选择 APK'); return; }
          if (result.state !== 'imported' || !result.installation_id) throw new Error('宿主未返回导入结果。');
          setStage('已加入游戏库'); activeJob.current = '';
          await onImported(result.installation_id);
        } finally { if (!disposed.current) { setCommitting(false); setCancelling(false); } }
      })}>导入到游戏库</button>
    </div>
  </dialog>;
}
