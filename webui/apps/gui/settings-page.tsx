import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { pickPath } from './import';
import { groups, isDirty, settingsError, type SettingValue, type Settings } from './settings';
const labels: Record<string, string> = {
  dark: '深色', light: '浅色', system: '跟随系统', compact: '紧凑', comfortable: '舒适',
  'zh-CN': '简体中文', 'en-US': 'English', profile: '跟随 Profile', windowed: '窗口', fullscreen: '全屏',
  aspect: '保持比例', stretch: '拉伸', automatic: '自动', 'hardware-only': '仅硬件', 'software-only': '仅软件',
  disabled: '禁用', loopback: '仅回环', allow: '允许网络', realtime: '实时', fixed: '固定步进',
};
export function SettingsPage({ onBack, onSaved }: { onBack: () => void; onSaved: (settings: Settings) => void }) {
  const [saved, setSaved] = useState<Settings | null>(null);
  const [draft, setDraft] = useState<Record<string, SettingValue>>({});
  const [group, setGroup] = useState<string>('general');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [discard, setDiscard] = useState<'back' | 'reload' | null>(null);
  const confirm = useRef<HTMLDialogElement>(null);
  const lock = useRef(false);
  const dirty = saved ? isDirty(saved.values, draft) : false;
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError(''); setNotice('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  const load = async () => { const value = await rpc<Settings>('settings.get'); setSaved(value); setDraft({ ...value.values }); onSaved(value); };
  useEffect(() => { void run(load); }, []);
  useEffect(() => { if (discard) confirm.current?.showModal(); else confirm.current?.close(); }, [discard]);
  const change = (key: string, value: SettingValue) => { setDraft(current => ({ ...current, [key]: value })); setNotice(''); };
  const leave = (action: 'back' | 'reload') => {
    if (busy) return;
    if (dirty) setDiscard(action); else if (action === 'back') onBack(); else void run(load);
  };
  return <div class="shell"><nav class="navigation" aria-label="设置导航">
    <div class="brand"><span>O</span><b>OGPlay</b></div><button disabled={busy} onClick={() => leave('back')}>← 返回游戏库</button>
    <div class="settings-nav">{groups.map(([key, label]) => <button aria-current={group === key ? 'page' : undefined} onClick={() => setGroup(key)}>{label}</button>)}</div>
  </nav><main><header class="topbar"><h1>全局设置</h1><span class="subtle">{dirty ? '有未保存的修改' : '影响之后启动的游戏'}</span>
    <div class="settings-toolbar"><button disabled={busy} onClick={() => leave('reload')}>重新载入</button>
    <button disabled={busy || !saved} onClick={() => { setDraft(Object.fromEntries(saved!.fields.map(field => [field.key, field.initial]))); setNotice('默认值已填入，保存后生效。'); }}>恢复默认</button>
    <button class="primary" disabled={busy || !saved || !dirty} onClick={() => void run(async () => {
      const invalid = settingsError(saved!.fields, draft); if (invalid) throw new Error(invalid);
      const value = await rpc<Settings>('settings.set', { revision: saved!.revision, values: draft });
      setSaved(value); setDraft({ ...value.values }); onSaved(value); setNotice('设置已保存。');
    })}>保存设置</button></div></header>
    <section class="settings-content" aria-busy={busy}><h2>{groups.find(([key]) => key === group)?.[1]}</h2>
      {error && <pre class="settings-error" role="alert">{error}</pre>}{notice && <p class="settings-notice" role="status">{notice}</p>}
      {!saved && <p>{busy ? '正在读取设置…' : '设置不可用。请检查 config.toml 后重新载入；不会覆盖损坏的配置。'}</p>}
      {saved && <>
        {group === 'general' && <p class="settings-fact">关闭启动器后保持游戏运行：始终开启</p>}
        {group === 'storage' && <div class="settings-fact"><p>库根：{saved.library_root}</p><p>配置：{saved.config_path}</p><button disabled={busy} onClick={() => void run(async () => { await rpc('settings.open_dir', { kind: 'library' }); })}>打开库目录</button><p>沙盒占用：尚未统计。FFmpeg 版本：尚未探测。</p><button disabled>清理沙盒 · 暂未开放</button></div>}
        {saved.fields.filter(field => field.group === group).map(field => <div class="setting-row" key={field.key}>
          <div><label for={`setting-${field.key}`}>{field.label}</label>{field.pending && <small class="setting-pending">{field.pending}；仅保存</small>}</div>
          <div class="setting-control">{typeof field.initial === 'boolean' ? <input id={`setting-${field.key}`} type="checkbox" checked={draft[field.key] === true} disabled={busy} onChange={event => change(field.key, event.currentTarget.checked)} /> :
            field.choices.length ? <select id={`setting-${field.key}`} value={String(draft[field.key])} disabled={busy} onChange={event => change(field.key, event.currentTarget.value)}>{field.choices.map(value => <option value={value}>{labels[value] ?? value}</option>)}</select> :
            typeof field.initial === 'number' ? <input id={`setting-${field.key}`} type="number" min={field.min} max={field.max} step={1} value={Number.isNaN(draft[field.key]) ? '' : String(draft[field.key])} disabled={busy} onInput={event => change(field.key, event.currentTarget.valueAsNumber)} /> :
            <input id={`setting-${field.key}`} type="text" value={String(draft[field.key])} disabled={busy} placeholder={field.directory ? '留空使用默认行为' : '未设置'} onInput={event => change(field.key, event.currentTarget.value)} />}
            {field.directory && <button disabled={busy} onClick={() => void run(async () => { const path = await pickPath('directory'); if (path) change(field.key, path); })}>浏览…</button>}
          </div>
        </div>)}
        {group === 'audio' && <p class="settings-fact">采样率与块大小：启动器不创建音频设备；当前无运行时探测数据。</p>}
        {group === 'network' && <p class="settings-fact">此选项仅保存意向，不改变当前运行时策略。回环仅指本机地址；公网能力以运行时契约为准。</p>}
        {group === 'diagnostics' && <button disabled={busy} onClick={() => void run(async () => { await rpc('settings.open_dir', { kind: 'library' }); })}>打开日志所在库目录</button>}
        {group === 'control' && <p class="settings-fact">MCP 只监听本机。固定端口被其他实例占用时启动失败；端口范围自动分配待后续接入。</p>}
        {group === 'about' && <><p class="settings-fact">版本及制品哈希以随程序交付的清单为准；清单缺失时不推测。</p>{Object.entries(saved.facts).map(([key, value]) => <details class="settings-fact"><summary>{key}</summary><pre>{value}</pre></details>)}<button disabled>导出诊断包 · 暂未开放</button></>}
      </>}
    </section></main>
    <dialog ref={confirm} aria-label="未保存的设置" onCancel={event => { event.preventDefault(); setDiscard(null); }}><h2>有未保存的修改</h2><p>放弃这些修改？</p><div class="modal-actions"><button autoFocus onClick={() => setDiscard(null)}>继续编辑</button><button onClick={() => { const action = discard; setDiscard(null); if (action === 'back') onBack(); else void run(load); }}>放弃修改</button></div></dialog>
  </div>;
}
