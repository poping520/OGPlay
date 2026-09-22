import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { pickPath } from './import';
import { isDirty, type SettingValue } from './settings';
import { gameGroups, gameSettingsError, inheritSetting, type GameSettings } from './game-settings';

export function GameSettingsPage({ id, onBack }: { id: string; onBack: () => void }) {
  const [saved, setSaved] = useState<GameSettings | null>(null);
  const [draft, setDraft] = useState<Record<string, SettingValue>>({});
  const [group, setGroup] = useState<string>('performance');
  const [mode, setMode] = useState('normal');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [discard, setDiscard] = useState<'back' | 'reload' | null>(null);
  const confirmation = useRef<HTMLDialogElement>(null);
  const lock = useRef(false);
  const dirty = saved ? isDirty(saved.values, draft) : false;
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError(''); setNotice('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  const accept = (value: GameSettings) => { setSaved(value); setDraft({ ...value.values }); };
  const load = async () => accept(await rpc<GameSettings>('game_settings.get', { installation_id: id }));
  useEffect(() => { void run(load); }, [id]);
  useEffect(() => { if (discard) confirmation.current?.showModal(); else confirmation.current?.close(); }, [discard]);
  const leave = (action: 'back' | 'reload') => {
    if (busy) return;
    if (dirty) setDiscard(action); else if (action === 'back') onBack(); else void run(load);
  };
  const change = (key: string, value: SettingValue) => { setDraft(current => ({ ...current, [key]: value })); setNotice(''); };
  const open = (kind: string) => void run(async () => { await rpc('library.open_dir', { installation_id: id, kind }); });
  const preview = saved?.previews[mode];
  return <div class="shell"><nav class="navigation" aria-label="游戏设置导航">
    <div class="brand"><span>O</span><b>OGPlay</b></div>
    <button disabled={busy} onClick={() => leave('back')}>← 返回游戏库</button>
    <div class="settings-nav">{gameGroups.map(([key, label]) => <button key={key} aria-current={group === key ? 'page' : undefined} onClick={() => setGroup(key)}>{label}</button>)}</div>
  </nav><main><header class="topbar"><h1>游戏设置 · {saved?.display_name ?? id}</h1>
    <span class="subtle">{dirty ? '有未保存的修改' : '下次启动生效'}</span>
    <div class="settings-toolbar"><button disabled={busy} onClick={() => leave('reload')}>重新载入</button>
      <button disabled={busy || !saved} onClick={() => { setDraft({}); setNotice('已恢复继承，保存后生效。'); }}>全部继承</button>
      <button class="primary" disabled={busy || !dirty || !saved} onClick={() => void run(async () => {
        const invalid = gameSettingsError(saved!, draft); if (invalid) throw new Error(invalid);
        accept(await rpc<GameSettings>('game_settings.set', { installation_id: id, revision: saved!.revision, values: draft }));
        setNotice('实例设置已保存。');
      })}>保存设置</button></div></header>
    <section class="settings-content" aria-busy={busy}><h2>{gameGroups.find(([key]) => key === group)?.[1]}</h2>
      <p class="instance-identity">实例：{id} · 继承项使用全局值或默认值；数据包目录继承导入记录。</p>
      {error && <pre class="settings-error" role="alert">{error}</pre>}{notice && <p class="settings-notice" role="status">{notice}</p>}
      {!saved && <p>{busy ? '正在读取…' : '设置不可用；不会覆盖损坏的配置。'}</p>}
      {saved && <>
        {group === 'device' && <div class="settings-fact"><p>Android 4.4.4 / API 19（固定）</p><p>机型预设数据已提供，列表选择与运行时应用尚未接入。下列数值为保存的配置意向，不代表运行时硬件。</p><button disabled>ANDROID_ID 查看/重新生成 · 管理接口待接入</button></div>}
        {group === 'data' && <div class="settings-fact"><p>{saved.config_path}</p><div class="game-actions"><button disabled={busy} onClick={() => open('sandbox')}>打开沙盒目录</button><button disabled={busy} onClick={() => open('log')}>打开日志目录</button><button disabled={busy} onClick={() => open('external')}>打开数据包目录</button></div><p>存档管理接口尚未接入。</p><div class="game-actions"><button disabled>导出/导入存档 · 预留</button><button disabled>重置沙盒 · 暂未开放</button></div></div>}
        {group === 'network' && <p class="settings-fact">仅保存意向，不改变运行时网络策略；“allow”表示允许网络的预留配置。</p>}
        {group === 'compatibility' && <div class="settings-fact"><p>导入时记录的 Profile：{saved.profile_id}</p>{Object.entries(saved.facts).map(([key, value]) => <p key={key}>{key}：{value}</p>)}</div>}
        {saved.fields.filter(field => field.group === group).map(field => {
          const overridden = Object.hasOwn(draft, field.key);
          const value = overridden ? draft[field.key] : saved.inherited[field.key];
          return <div class="setting-row" key={field.key}><div><label for={`game-${field.key}`}>{field.label}</label>
            <label class="inherit-control"><input type="checkbox" checked={!overridden} disabled={busy} onChange={event => {
              setDraft(current => event.currentTarget.checked ? inheritSetting(current, field.key) : { ...current, [field.key]: saved.inherited[field.key] }); setNotice('');
            }} />继承{field.key === 'external_dir' ? '导入记录' : '全局 / 默认'}</label>
            {field.pending && <small class="setting-pending">{field.pending}；仅保存</small>}</div>
            <div class="setting-control">{typeof field.initial === 'boolean' ? <input id={`game-${field.key}`} type="checkbox" checked={value === true} disabled={busy || !overridden} onChange={event => change(field.key, event.currentTarget.checked)} /> : field.choices.length ?
              <select id={`game-${field.key}`} value={String(value)} disabled={busy || !overridden} onChange={event => change(field.key, event.currentTarget.value)}>{field.choices.map(choice => <option key={choice} value={choice}>{choice === 'unlimited' ? '不限' : choice}</option>)}</select> : typeof field.initial === 'number' ?
              <input id={`game-${field.key}`} type="number" step={1} min={field.min} max={field.max} value={Number.isNaN(value) ? '' : String(value)} disabled={busy || !overridden} onInput={event => change(field.key, event.currentTarget.valueAsNumber)} /> :
              <input id={`game-${field.key}`} type="text" value={String(value)} disabled={busy || !overridden} onInput={event => change(field.key, event.currentTarget.value)} />}
              {field.directory && <button disabled={busy || !overridden} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) change(field.key, path); })}>浏览…</button>}</div>
          </div>;
        })}
        {(group === 'advanced' || group === 'compatibility') && <div class="settings-fact"><h3>启动与参数预览</h3><p>预览来自已保存设置；修改后须先保存。预检不会启用 MCP。</p>
          <div class="game-actions"><label>启动模式 <select value={mode} onChange={event => setMode(event.currentTarget.value)}><option value="normal">正常启动</option><option value="preflight">预检</option><option value="diagnostic">带诊断启动</option></select></label>
          <button disabled={busy || dirty || !!preview?.error} onClick={() => void run(async () => { await rpc('library.launch', { installation_id: id, mode }); setNotice('已启动子进程；预检结果与诊断信息见实例日志。'); })}>执行{mode === 'preflight' ? '预检' : '启动'}</button></div>
          {preview?.error ? <pre role="alert">{preview.error}</pre> : <pre aria-label="生成的 argv">{JSON.stringify(preview?.argv ?? [], null, 2)}</pre>}</div>}
      </>}
    </section></main>
    <dialog ref={confirmation} aria-label="未保存的实例设置" onCancel={event => { event.preventDefault(); setDiscard(null); }}><h2>有未保存的修改</h2><p>放弃这些修改？</p><div class="modal-actions"><button autoFocus onClick={() => setDiscard(null)}>继续编辑</button><button onClick={() => { const action = discard; setDiscard(null); if (action === 'back') onBack(); else void run(load); }}>放弃修改</button></div></dialog>
  </div>;
}
