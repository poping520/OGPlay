import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { pickPath, type DataHint } from './import';
import { MountPreview, useDataDirectory } from './mount-preview';

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
    if (next.kind !== 'remount') throw new Error('所选目录里没有游戏刚才找不到的文件；请确认选的是这个游戏的数据包。');
    setPicked(path); setHint(next); setChoice(0);
  });
  const apply = (relaunch: boolean) => run(async () => {
    await rpc('library.data_mount.apply', { installation_id: id, external_guest_dir: candidate.guest_dir, ...(picked ? { external_dir: picked } : {}) });
    await onApplied(relaunch);
  });
  const remount = hint.kind === 'remount' && candidate;
  return <dialog ref={dialog} class="import-wizard data-hint" aria-label="数据包挂载提示" onCancel={event => { event.preventDefault(); if (!busy) onClose(); }}>
    <header class="iw-head"><h2>{remount ? '数据包可能挂错了位置' : '这个游戏可能需要数据包'}</h2><span class="iw-stage"><i class="led warning" />{name}</span></header>
    {remount ? <>
      <p class="dh-lead">刚才运行时，游戏有 <b>{candidate.explained}</b> 个文件没找到，而{picked ? '所选' : ''}数据包里正好有这些文件。按建议挂载后，游戏就能读到它们。</p>
      <div class="dh-move">
        <div><small>当前挂载</small><code>{picked ? '未配置数据包' : hint.current_guest_dir}</code></div>
        <span aria-hidden="true">→</span>
        <div><small>建议挂载</small><code class="good">{candidate.guest_dir}</code></div>
      </div>
      {candidates.length > 1 && <div class="dh-choices" role="radiogroup" aria-label="候选挂载位置">{candidates.map((item, index) =>
        <button role="radio" aria-checked={index === choice} class={index === choice ? 'on' : ''} disabled={busy} onClick={() => setChoice(index)}>
          <code>{item.guest_dir}</code><small>{item.explained} 个文件</small></button>)}</div>}
      <div class="dh-examples"><small>游戏没找到的文件（示例）</small><ul>{candidate.examples.map(path => <li><code>{path}</code></li>)}</ul></div>
      <MountPreview caption="按建议挂载后" guest={candidate.guest_dir} data={data} error={dataError} />
      <p class="iw-note dh-foot">只修改这个游戏的数据包挂载位置，数据包本身不会移动。可在游戏设置 › 数据中关闭这个提示。</p>
    </> : <>
      <p class="dh-lead">刚才运行时，游戏在 sdcard 上有 <b>{hint.missing}</b> 个文件没找到，但还没有为它配置数据包。如果这个游戏需要数据包，请选择数据包目录，OGPlay 会据此判断应挂载的位置。</p>
      <div class="dh-examples"><small>游戏没找到的文件（示例）</small><ul>{(hint.examples ?? []).map(path => <li><code>{path}</code></li>)}</ul></div>
      {hint.search_root && <p class="iw-note dh-foot">游戏似乎在 <code>{hint.search_root}</code> 附近读取数据。</p>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">
      <button disabled={busy} onClick={onClose}>忽略</button>
      <span class="iw-spacer" />
      {remount ? <>
        <button disabled={busy} onClick={() => void apply(false)}>仅修改</button>
        <button class="primary" disabled={busy} onClick={() => void apply(true)}>挂载并重新启动</button>
      </> : <button class="primary" disabled={busy} onClick={() => void pick()}>选择数据包目录</button>}
    </div>
  </dialog>;
}
