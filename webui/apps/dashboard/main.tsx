import { render } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import './style.css';
import { DashboardStore, matches, threads, type Section, type Status, type Snapshot } from './store';
import { Poller } from './rpc';
import { Panels } from './panels';
import { selectedSnapshot, health } from './store';
import { EventLanes } from './lanes';
const store = new DashboardStore();
const labels: Record<Status, string> = {complete: '可用', partial: '部分可用', unavailable: '不可用'};
const sectionNames: Record<string, string> = {session: '会话', diagnostics: '运行时诊断', gpu: 'GPU · GLES', vfs: 'VFS · IO', audio: '音频', capabilities: '能力账本', log: '结构化日志', dexvm:'DexVM', jni:'JNI', memory:'内存', cpu:'CPU 缓存', libraries:'动态库', ui:'Input / UiTree', video:'视频'};
function reasonText(reason?: string) {
  if (!reason) return '来源已连接';
  if (reason === 'not_connected') return '尚未接入';
  if (reason === 'busy' || reason === 'provider_busy') return '来源忙碌';
  if (reason === 'provider_error') return '来源读取失败';
  if (reason === 'see source sections') return '部分来源暂不可用';
  if (reason.includes('limit') || reason.includes('bounded')) return '仅展示限量快照';
  if (reason.includes('unimplemented')) return '未实现能力命中记录';
  return '部分事实可用';
}
function Badge({status}: {status: Status}) { return <span class={`badge ${status}`}>{labels[status]}</span>; }
function Timeline({history}: {history: Snapshot[]}) {
  const host = useRef<HTMLDivElement>(null), chart = useRef<uPlot>();
  const current = useRef(history); current.current = history;
  useEffect(() => {
    const element = host.current!;
    const plot = chart.current = new uPlot({width: Math.max(240, element.clientWidth), height: 190,
      scales: {x: {time: false}}, legend: {show: false},
      axes: [{stroke: '#8093a9', grid: {stroke: '#253143'}, label: '采样经过时间（秒）'}, {stroke: '#8093a9', grid: {stroke: '#253143'}, label: 'frame'}],
      series: [{}, {label: 'frame', stroke: '#58a6ff', width: 2, spanGaps: false}],
      hooks: {setCursor: [(p) => {
        if (p.cursor.idx == null) return;
        const frame = current.current[p.cursor.idx]?.session?.data?.frame;
        if (frame != null && String(current.current[p.cursor.idx]?.captured_at_steady_ns) !== store.selection.snapshotTime) store.select({...store.selection, frame: String(frame), snapshotTime: String(current.current[p.cursor.idx]?.captured_at_steady_ns ?? 0), steadyStart: String(current.current[Math.max(0,p.cursor.idx-1)]?.captured_at_steady_ns ?? 0), steadyEnd: String(current.current[p.cursor.idx]?.captured_at_steady_ns ?? 0)});
      }]},
    }, [[], []], element);
    const observer = new ResizeObserver(() => plot.setSize({width: Math.max(240, element.clientWidth), height: 190}));
    observer.observe(element);
    return () => { observer.disconnect(); plot.destroy(); chart.current = undefined; };
  }, []);
  useEffect(() => {
    if (!chart.current) return;
    const base = BigInt(history[0]?.captured_at_steady_ns ?? 0);
    chart.current.setData([
      history.map(s => Number(BigInt(s.captured_at_steady_ns) - base) / 1e9),
      history.map(s => s.session?.status !== 'unavailable' && s.session?.data && Number.isSafeInteger(Number(s.session.data.frame)) ? Number(s.session.data.frame) : null),
    ]);
  }, [history]);
  return <div ref={host} class="chart" aria-label="实际 frame 采样时间轴" />;
}
function App() {
  const [, setVersion] = useState(0);
  const [module, setModule] = useState('diagnostics');
  useEffect(() => {
    const unsubscribe = store.subscribe(() => setVersion(n => n + 1));
    const poller = new Poller(store); poller.start(); return () => { unsubscribe(); poller.stop(); };
  }, []);
  const latest = selectedSnapshot(store), session = latest?.session?.status !== 'unavailable' ? latest?.session?.data : null;
  const rows = threads(latest);
  const focused = threads(store.selection.frame ? latest : store.selectedThread).find(t => String(t.guest_tid) === store.selection.guestTid);
  const events = store.events.filter(e => matches(e, store.selection));
  const section = latest?.[module] as Section | undefined;
  return <>
    <header><div class="brand"><b>OG</b><span>PLAY <em>RUNTIME</em></span></div><span class="divider" /><h1>Dashboard</h1>
      <span class={`connection ${store.connection}`}>● {store.connection === 'connected' ? '已连接' : store.connection === 'connecting' ? '连接中' : '连接已断开'}</span>
      <span class="endpoint">{location.host}</span><span class="readonly">只读观测</span></header>
    <div class="metrics"><div><label>LIFECYCLE</label><strong>{session?.lifecycle ?? '—'}</strong></div><div><label>FRAME</label><strong>{session?.frame ?? '—'}</strong></div>
      <div><label>PRESENTED</label><strong>{session?.presented_frame ?? '—'}</strong></div><div><label>GUEST TICKS</label><strong>{session?.guest_ticks ?? '—'}</strong></div>
      <div><label>采样</label><strong>8 Hz <small>目标频率</small></strong></div></div>
    {store.error && <div role="alert" class="notice error">无法读取当前状态：{store.error}。正在重连；图表与事件仅保留历史记录。</div>}
    {session?.guest_fault && <div role="alert" class="notice error">Guest fault · {session.guest_fault}</div>}
    {store.gap && <div class="notice">事件环发生覆盖，历史存在缺口。服务丢失 {store.dropped} 条；syscall / native 来源丢失 {store.sourceDropped}。</div>}
    {latest?.metadata && <div class="metadata">{Object.entries(latest.metadata as Record<string,string>).filter(([k]) => !k.startsWith("ffmpeg_")).map(([k,v]) => <span>{k} <b>{v}</b></span>)}</div>}
    {store.selection.frame && !latest && <div class="notice">所选历史快照已离开保留窗口，请清除筛选回到实时。</div>}
    <main>
      <aside class="panel topology"><div class="panel-title"><h2>模块拓扑</h2><span>数据来源</span></div><div class="layer-label">编排 → 运行时 → 边界</div>
        {Object.entries(sectionNames).map(([name, title]) => { const s = latest?.[name] as Section | undefined; return <button class={`module ${health(s)} ${module === name ? 'selected' : ''}`} onClick={() => setModule(name)}>
          <span>{title}</span><Badge status={s?.status ?? 'unavailable'}/><small>{s ? reasonText(s.reason) : '等待快照'}</small></button>; })}
        <p class="muted">状态表示数据可用性，不代表游戏兼容性。未接入的数据不会显示为零。</p>
      </aside>
      <section class="center">
        <div class="panel"><div class="panel-title"><h2>帧进度</h2><span>{store.history.length} / 600 个采样 · {store.connection === 'connected' ? '实时' : '历史'}</span></div>
          <Timeline history={store.history}/><EventLanes store={store}/><p class="chart-note">悬停选择 frame；采样曲线不等同于逐帧耗时。当前事件来源没有 frame 时不会强行关联。</p></div>
        <div class="panel"><div class="panel-title"><h2>线程</h2><span>{rows.length} 条 · 点击联动</span></div>
          <div class="table-scroll"><table><thead><tr><th>Guest TID</th><th>Context</th><th>线程 / 执行</th><th>状态</th></tr></thead><tbody>
            {rows.map(t => <tr class={store.selection.guestTid === String(t.guest_tid) ? 'selected' : ''}>
              <td><button class="link" onClick={() => store.select({...store.selection, guestTid: String(t.guest_tid), contextToken: String(t.context_token)})}>#{t.guest_tid}</button></td>
              <td>{t.context_token}</td><td>{t.name || '—'}</td><td>{t.wait_state || t.status || '—'}</td></tr>)}
          </tbody></table>{!rows.length && <div class="empty">{latest?.diagnostics?.status === 'unavailable' || !latest ? '线程来源不可用' : '当前快照没有线程记录'}</div>}</div>
        </div>
        <div class="panel"><div class="panel-title"><h2>事件流</h2><span>{events.length} 条 · {store.connection === 'connected' ? '实时' : '历史'}</span></div>
          <div class="filters"><span>{store.selection.guestTid ? `线程 #${store.selection.guestTid}` : '全部线程'}</span><span>{store.selection.frame ? `frame ${store.selection.frame}` : '全部帧'}</span>
            <button onClick={() => store.select({})}>清除筛选</button></div>
          <div class="events table-scroll"><table><thead><tr><th>Sequence</th><th>类型</th><th>Thread</th><th>Frame</th><th>事实</th></tr></thead><tbody>
            {events.slice(-100).reverse().map(e => <tr><td>{e.sequence}</td><td><span class="event-kind">{e.kind}</span></td><td>{e.guest_tid ?? '—'}</td><td>{e.frame ?? '未关联'}</td><td>{e.observed_only ? `观测增量 +${e.delta} · ${e.capability ?? (e.player == null ? '' : 'player '+e.player)}` : e.method ?? e.lifecycle_phase ?? e.detail ?? (e.syscall_nr == null ? '—' : `syscall ${e.syscall_nr} → ${e.result}`)}</td></tr>)}
          </tbody></table>{!events.length && <div class="empty">没有符合筛选条件的事件</div>}</div>
        </div>
      </section>
      <aside class="right"><div class="panel focus"><div class="panel-title"><h2>联动焦点</h2><span>{store.selection.guestTid ? `#${store.selection.guestTid}` : '未选择'}</span></div>
        <div class="body">{focused ? <><h3>{focused.name || `线程 #${focused.guest_tid}`}</h3><p class="muted">Context {focused.context_token} · {focused.wait_state || focused.status || '—'}</p>
          <h4>Java 栈</h4>{focused.frames?.length ? focused.frames.map(f => <div class="stack"><code>{f.method}</code><small>dex_pc {f.dex_pc}</small></div>) : <p class="muted">当前来源没有可用 Java 栈</p>}</> : <div class="empty">{store.selection.guestTid ? '等待所选线程快照；忙碌时保持不可用' : '选择一个线程，查看同一 context 的调用事实。'}</div>}</div>
      </div><div class="panel source"><div class="panel-title"><h2>{sectionNames[module]}</h2><Badge status={section?.status ?? 'unavailable'}/></div>
        <div class="body"><p>{section ? reasonText(section.reason) : '等待当前快照'}</p>
          {module === 'diagnostics' && latest?.diagnostics?.data?.sections.map(s => <div class="source-row"><span>{s.name}</span><Badge status={s.status}/></div>)}
          {module !== 'session' && <a href={`#panel-${module}`}>查看模块面板 ↓</a>}</div></div></aside>
    </main><div class="selection-bar"><b>共享键筛选</b><span>{Object.entries(store.selection).filter(([k]) => !k.startsWith('steady') && k !== 'snapshotTime').map(([k,v]) => `${k}: ${v}`).join(' · ') || '全部事实'}</span><button onClick={() => store.select({})}>清除 / 回到实时</button></div><Panels store={store} snapshot={latest}/><footer>OGPLAY · DASHBOARD<span>schema 1 · 固定容量历史 · 不推进 guest</span></footer>
  </>;
}
render(<App/>, document.getElementById('app')!);
