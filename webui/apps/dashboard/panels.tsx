import type { ComponentChildren } from 'preact';
import { facts, sectionData, matchesFact, identitySelection, type DashboardStore, type Fact, type Section, type Snapshot } from './store';
const text = (v: unknown): string => v == null ? '—' : typeof v === 'object' ? Array.isArray(v) ? v.map(text).join(' · ') : '结构化数据' : String(v);
const names: Record<string, string> = {dexvm:'DexVM · 堆 / GC / 调用', jni:'JNI 引用', memory:'内存权限', cpu:'Dynarmic 缓存', gpu:'GLES / GPU', vfs:'VFS / 沙盒', audio:'AudioTrack', video:'VideoView', ui:'Input / UiTree', libraries:'动态库', capabilities:'能力账本', log:'结构化日志'};
const fieldNames: Record<string,string> = {heap_used:'堆使用',heap_target:'堆目标',heap_growth_limit:'增长上限',heap_maximum:'最大堆',objects:'对象',classes:'登记类',linked_classes:'已链接类',classes_initialized:'已初始化类',method_calls:'解释调用',intrinsic_calls:'Intrinsic',native_calls:'Native',gc_collections:'GC 次数',gc_pause_ns:'累计 GC 暂停 ns',gc_freed_bytes:'GC 回收字节',draws:'Draw',clears:'Clear',gl_errors:'GL 错误',local:'Local 引用',global:'Global 引用',weak_global:'Weak 引用',attached_threads:'JNI 线程',nodes:'UI 节点',focus:'焦点节点',layout_dirty:'布局脏节点',draw_dirty:'绘制脏节点'};
function Table({rows, columns, onValue}: {rows: Fact[]; columns: string[]; onValue?: (column: string, value: unknown, row: Fact) => void}) {
  return <div class="table-scroll"><table><thead><tr>{columns.map(c => <th>{fieldNames[c] ?? c}</th>)}</tr></thead><tbody>{rows.slice(0,128).map(row => <tr>{columns.map(c => <td>{onValue && row[c] != null && (!['guest_tid','host_tid','context_token','owner_context'].includes(c) || String(row[c]) !== '0') && ['guest_tid','context_token','host_tid','fd','node_id','player','id','owner_context','address','object','lifecycle_phase','generation'].includes(c) ? <button class="link" onClick={() => onValue(c,row[c],row)}>{text(row[c])}</button> : text(row[c])}</td>)}</tr>)}</tbody></table>{!rows.length && <p class="empty">无匹配记录；未关联的键不作推测</p>}</div>;
}
function Metrics({value}: {value: Fact}) { return <dl class="fact-metrics">{Object.entries(value).filter(([,v]) => v == null || typeof v !== 'object').map(([k,v]) => <div><dt>{fieldNames[k] ?? k}</dt><dd>{text(v)}</dd></div>)}</dl>; }
export function Panels({store, snapshot}: {store: DashboardStore; snapshot: Snapshot | null}) {
  const diagnostic = sectionData(snapshot,'diagnostics') as Fact | null;
  const choose = (column: string, value: unknown) => store.select(identitySelection(column,value,diagnostic));
  const sourceState = (name: string) => {
    const source = facts(diagnostic?.sections).find(s => s.name === name);
    return source?.status !== 'complete' ? <p class="muted">{name}：{source?.status ?? 'unavailable'} · {String(source?.reason ?? '来源不可用')}</p> : null;
  };
  const panel = (name: string, children: ComponentChildren) => {
    const source = snapshot?.[name] as Section | undefined;
    return <section class="panel detail-panel" id={`panel-${name}`}><div class="panel-title"><h2>{names[name] ?? name}</h2><span class={`badge ${source?.status ?? 'unavailable'}`}>{source?.status ?? 'unavailable'}</span></div>
      {!source || source.status === 'unavailable' || source.data == null ? <p class="empty">{source?.reason === 'busy' ? '来源忙碌，本拍不可用' : '来源未接入或不可用'}</p> : <div class="body">{children}{source.status === 'partial' && <p class="muted">{source.reason}</p>}</div>}</section>;
  };
  const scalar = (name: string) => panel(name,<Metrics value={(sectionData(snapshot,name) ?? {}) as Fact}/>);
  const vfs = sectionData(snapshot,'vfs') as Fact | null;
  const memory = sectionData(snapshot,'memory') as Fact | null;
  const caps = facts(sectionData(snapshot,'capabilities')).sort((a,b) => Number(b.count)-Number(a.count));
  const libraries = sectionData(snapshot,'libraries') as Fact | null;
  const monitors = facts(diagnostic?.monitors).filter(m => (!store.selection.monitor || String(m.object) === store.selection.monitor) && (!store.selection.contextToken || [m.owner_context,...(m.entry_waiters as unknown[] ?? []),...(m.notify_wait_set as unknown[] ?? [])].map(String).includes(store.selection.contextToken)));
  return <div class="diagnostic-panels">
    {scalar('dexvm')}{scalar('jni')}{panel('gpu',<><Metrics value={(sectionData(snapshot,'gpu') ?? {}) as Fact}/><h3>GLES trace</h3>{sourceState('gles')}<Table rows={facts(diagnostic?.gles)} columns={['call','r0','r1','r2','r3','error']}/><p class="muted">来源未提供线程与帧键，trace 不作推测关联。</p></>)}{scalar('ui')}
    {panel('memory',<Table rows={Array.isArray(memory?.pages_by_protection) ? memory.pages_by_protection.map((count,index) => ({protection:['NONE','R','W','RW','X','RX','WX','RWX'][index],pages:count,bytes:Number(count)*4096})) : []} columns={['protection','pages','bytes']}/>)}
    {panel('cpu',<Table rows={facts(sectionData(snapshot,'cpu'))} columns={['processor_id','status','capacity_bytes','used_bytes','flushes','captured_at_steady_ns']}/>)}
    {panel('vfs',<><Metrics value={vfs ?? {}}/><h3>挂载</h3><Table rows={facts(vfs?.mounts)} columns={['root','source']}/><h3>打开的 FD</h3><Table rows={facts(vfs?.descriptors).filter(r => (!store.selection.fd || String(r.fd) === store.selection.fd) && (!store.selection.nodeId || String(r.node_id) === store.selection.nodeId))} columns={['fd','node_id','offset','readable','writable','busy']} onValue={choose}/></>)}
    {panel('audio',<Table rows={facts(sectionData(snapshot,'audio')).filter(r => !store.selection.player || String(r.player) === store.selection.player)} columns={['player','written_frames','consumed_frames','queued_bytes','underrun_count']} onValue={choose}/>)}
    {panel('video',<><p class="muted">FFmpeg：{text((snapshot?.metadata as Fact)?.ffmpeg_available)} · {text((snapshot?.metadata as Fact)?.ffmpeg_reason)}</p><Table rows={facts(sectionData(snapshot,'video'))} columns={['receiver','playing','completed','duration_ms','base_position_ms','decoder_attached']}/></>)}
    {panel('libraries',<Table rows={facts(libraries?.records)} columns={['soname','handle','state','failure']}/>)}
    {panel('capabilities',<Table rows={caps.filter(r => !store.selection.capability || String(r.id) === store.selection.capability)} columns={['id','count','first_lr','last_lr']} onValue={choose}/>)}
    {panel('log',<Table rows={facts(sectionData(snapshot,'log')).filter(r => matchesFact(r,{...store.selection,kind:undefined})).reverse()} columns={['level','category','message','guest_tid','host_tid','frame']} onValue={choose}/>)}
    <section class="panel detail-panel wide" id="panel-diagnostics"><div class="panel-title"><h2>Monitor · Futex · Pacer</h2></div><div class="body">
      <p class={Array.isArray(diagnostic?.confirmed_cycles) && diagnostic.confirmed_cycles.length ? 'notice error' : 'muted'}>已确认等待环：{text(diagnostic?.confirmed_cycles ?? null)}</p>
      <h3>Monitor（点击 owner 跳转线程）</h3>{sourceState("monitors")}<Table rows={monitors} columns={['object','owner_context','recursion','entry_waiters','notify_wait_set']} onValue={choose}/>
      <h3>Futex</h3>{sourceState("futexes")}<Table rows={facts(diagnostic?.futexes).filter(r => !store.selection.futex || String(r.address) === store.selection.futex)} columns={['address','wake_count']} onValue={choose}/><Table rows={facts(diagnostic?.futexes).filter(r => !store.selection.futex || String(r.address) === store.selection.futex).flatMap(r => facts(r.waiters).map((w): Fact => ({...w,address:r.address}))).filter(r => !store.selection.guestTid || String(r.guest_tid) === store.selection.guestTid)} columns={['address','guest_tid','expected','timed','wait_since_steady_ns']} onValue={choose}/>
      <h3>Pacer / Lifecycle</h3>{sourceState("pacer")}{sourceState("lifecycle")}<Metrics value={(diagnostic?.pacer ?? {}) as Fact}/><Table rows={diagnostic?.lifecycle ? [{...(diagnostic.lifecycle as Fact), lifecycle_phase:(diagnostic.lifecycle as Fact).phase}] : []} columns={['lifecycle_phase','generation','unchanged_ns']} onValue={choose}/>
    </div></section>
    <section class="panel detail-panel wide"><div class="panel-title"><h2>线程调用链 · A32 → JNI → Syscall</h2></div><div class="body">
      <h3>A32</h3>{sourceState("executions")}<Table rows={facts(diagnostic?.executions).filter(r => matchesFact(r,{guestTid:store.selection.guestTid,contextToken:store.selection.contextToken,hostTid:store.selection.hostTid}))} columns={['guest_tid','host_tid','context_token','arm_pc','active']} onValue={choose}/>
      <h3>Native 在途 / 返回</h3>{sourceState("native_calls")}<Table rows={facts(diagnostic?.native_calls).filter(r => matchesFact(r,store.selection))} columns={['guest_tid','context_token','method','phase']} onValue={choose}/>
      <h3>Syscall</h3>{sourceState("syscalls")}<Table rows={facts(diagnostic?.syscalls).filter(r => matchesFact(r,store.selection))} columns={['guest_tid','syscall_nr','fd','result','steady_ns']} onValue={choose}/>
    </div></section>
  </div>;
}
