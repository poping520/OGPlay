import type { DashboardStore, Event, Selection } from './store';
const names: Record<string,string>={gc:'GC',gles_error:'GL 错误',syscall:'Syscall',native:'Native',dexvm:'DexVM',audio_underrun:'音频欠载',capability_miss:'能力缺口',lifecycle:'生命周期',vfs_flush:'VFS flush'};
export function EventLanes({store}: {store: DashboardStore}) {
  const start=BigInt(store.history[0]?.captured_at_steady_ns ?? 0), end=BigInt(store.history.at(-1)?.captured_at_steady_ns ?? 0);
  const select=(event:Event) => { const selection:Selection={kind:event.kind};
    if(event.player!=null) selection.player=String(event.player);
    if(event.capability) selection.capability=event.capability;
    if(event.guest_tid!=null) selection.guestTid=String(event.guest_tid);
    if(event.context_token!=null) selection.contextToken=String(event.context_token);
    if(event.lifecycle_phase) selection.lifecycle=event.lifecycle_phase;
    if(event.generation!=null) selection.generation=String(event.generation);
    store.select(selection);
  };
  return <div class="lanes"><p class="muted">泳道与采样时间轴共用范围；空心点为累计计数变化的观测时刻，不代表事件精确发生时间。</p>
    {Object.entries(names).map(([kind,title]) => <div class="lane"><span>{title}</span><div>{store.events.filter(e => e.kind===kind).slice(-256).map(event => {
      const stamp=event.steady_ns ?? event.observed_at_steady_ns;
      if(stamp==null || end<=start || BigInt(stamp)<start || BigInt(stamp)>end) return null;
      const left=Number((BigInt(stamp)-start)*10000n/(end-start))/100;
      return <button class={`mark ${event.observed_only?'observed':''}`} style={{left:`${left}%`}} title={`${title} · ${event.observed_only?'观测增量 '+event.delta:'来源事件'} · #${event.sequence}`} aria-label={`${title}事件 ${event.sequence}`} onClick={() => select(event)}/>;
    })}</div></div>)}
    <p class="muted">缺少时间键的事件仅列于事件表；未装配来源不代表没有事件。</p>
  </div>;
}
