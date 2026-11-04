import { t, setLanguage, useLanguage, updateDocumentLanguage } from './i18n';
import { render } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import '../../packages/ui-kit/theme.css';
import './style.css';
import { captureKey, captureMouse, joinStickSources, replaceStickSource, sourceLabel, splitSources } from './keys';
import {
  addControl, baseBindings, buildLayer, clampBox, conflicts, freeStickPreset, loadDraft, newStick, placeZone, removeControl,
  resolveZone, setZoneBinding, toGuest, type Binding, type Box, type Draft, type Session, type Zone,
} from './model';
import { createRpc, rememberToken, resumeOnUnload, RpcError, takeToken, Unauthorized, type Rpc } from './rpc';

interface Frame { url: string; sequence: number }
interface Status { kind: 'info' | 'ok' | 'error'; text: string }
// direction: rebind only that stick direction instead of collecting all four.
interface Capture { zone: string; kind: 'button' | 'stick'; collected: string[]; template: Binding; direction?: number }
type Drag = { kind: 'move' | 'size' | 'travel'; zone: string; start: { x: number; y: number }; box: Box; pointer: number };

const stickSteps = () => [t('上'), t('左'), t('下'), t('右')];
const pressNames = () => ({ tap: t('点按'), hold: t('按住'), repeat: t('连发') } as const);

function message(error: unknown): string {
  if (error instanceof RpcError && error.code === -32010) return t("配置无效：{reason}", { reason: error.message });
  if (error instanceof RpcError && error.code === -32011) return t('游戏还没有输出画面，稍后点“重新获取画面”。');
  return error instanceof Error ? error.message : String(error);
}

function TokenForm({ onToken }: { onToken: (token: string) => void }) {
  const [value, setValue] = useState('');
  return <form class="token" onSubmit={event => { event.preventDefault(); if (value.trim()) onToken(value.trim()); }}>
    <h1>{t("OGPlay 按键设置")}</h1>
    <p>{t("请从 OGPlay 启动器打开此页面；命令行启动时，可粘贴终端输出中的编辑器令牌。")}</p>
    <input value={value} onInput={event => setValue((event.target as HTMLInputElement).value)} placeholder={t("令牌")} autoFocus />
    <button class="pill main" type="submit">{t("连接")}</button>
  </form>;
}

function zoneBindings(draft: Draft, zone: string) { return draft.bindings.filter(binding => binding.zone === zone); }

function bindingText(binding: Binding): string {
  if (binding.action === 'stick') return splitSources(binding.source).map(sourceLabel).join(' ');
  return sourceLabel(binding.source);
}

function Stage({ session, draft, frame, selected, conflicted, onSelect, onZone, onBinding }: {
  session: Session; draft: Draft; frame: Frame | null; selected: string | null; conflicted: Set<string>;
  onSelect: (zone: string | null) => void; onZone: (zone: Zone) => void; onBinding: (zone: string, binding: Binding) => void;
}) {
  const { width, height } = session.surface;
  const svg = useRef<SVGSVGElement>(null);
  const drag = useRef<Drag | null>(null);
  const point = (event: PointerEvent) => toGuest(event.clientX, event.clientY, svg.current!.getBoundingClientRect(), session.surface);
  const begin = (event: PointerEvent, kind: Drag['kind'], zone: Zone) => {
    event.stopPropagation(); event.preventDefault();
    svg.current!.setPointerCapture(event.pointerId);
    drag.current = { kind, zone: zone.name, start: point(event), box: resolveZone(zone, session.surface), pointer: event.pointerId };
    onSelect(zone.name);
  };
  const move = (event: PointerEvent) => {
    const active = drag.current;
    if (!active || active.pointer !== event.pointerId) return;
    const now = point(event), { box } = active;
    if (active.kind === 'move') {
      onZone(placeZone(active.zone, { ...box, cx: box.cx + now.x - active.start.x, cy: box.cy + now.y - active.start.y }, session.surface));
    } else if (active.kind === 'size') {
      const hw = Math.abs(now.x - box.cx), hh = Math.abs(now.y - box.cy);
      const next = box.circle ? { ...box, hw: Math.hypot(hw, hh), hh: Math.hypot(hw, hh) } : { ...box, hw, hh };
      onZone(placeZone(active.zone, clampBox(next, session.surface), session.surface));
    } else {
      const stick = zoneBindings(draft, active.zone).find(binding => binding.action === 'stick');
      // The handle sits on the diagonal, so the travel is the pointer's distance from the centre.
      const travel = Math.hypot(now.x - box.cx, now.y - box.cy) / box.hw;
      if (stick) onBinding(active.zone, { ...stick, radius: Math.round(Math.min(1, Math.max(0.1, travel)) * 100) / 100 });
    }
  };
  const end = (event: PointerEvent) => { if (drag.current?.pointer === event.pointerId) drag.current = null; };
  return <svg ref={svg} class="stage" viewBox={`0 0 ${width} ${height}`} style={{ aspectRatio: `${width} / ${height}` }}
              onPointerDown={() => onSelect(null)} onPointerMove={move} onPointerUp={end} onPointerCancel={end}>
    {frame ? <image href={frame.url} x="0" y="0" width={width} height={height} preserveAspectRatio="none" />
           : <rect class="placeholder" width={width} height={height} />}
    {draft.zones.map(zone => {
      const box = resolveZone(zone, session.surface);
      const bindings = zoneBindings(draft, zone.name);
      const stick = bindings.find(binding => binding.action === 'stick');
      const classes = ['zone', zone.name === selected ? 'selected' : '', conflicted.has(zone.name) ? 'conflict' : '', bindings.length ? '' : 'unbound'].join(' ');
      const font = Math.max(12, Math.min(28, box.hh * 0.45));
      return <g key={zone.name} class={classes} onPointerDown={event => begin(event as PointerEvent, 'move', zone)}>
        {box.circle ? <circle cx={box.cx} cy={box.cy} r={box.hw} /> : <rect x={box.cx - box.hw} y={box.cy - box.hh} width={box.hw * 2} height={box.hh * 2} rx="6" />}
        {stick ? <>
          <circle class="travel" cx={box.cx} cy={box.cy} r={box.hw * (stick.radius ?? 0.7)} />
          {splitSources(stick.source).map((source, index) => {
            const [dx, dy] = [[0, -1], [-1, 0], [0, 1], [1, 0]][index] ?? [0, 0];
            return <text x={box.cx + dx * box.hw * 0.6} y={box.cy + dy * box.hh * 0.6} font-size={font * 0.8}>{sourceLabel(source)}</text>;
          })}
        </> : <text x={box.cx} y={box.cy} font-size={font}>{bindings.length ? bindings.map(bindingText).join(' / ') : t('未绑定')}</text>}
        <text class="name" x={box.cx} y={box.cy + box.hh + 12} font-size="11">{zone.name}</text>
        {zone.name === selected && <>
          <circle class="handle" cx={box.cx + box.hw * (box.circle ? Math.SQRT1_2 : 1)} cy={box.cy + box.hh * (box.circle ? Math.SQRT1_2 : 1)} r="7"
                  onPointerDown={event => begin(event as PointerEvent, 'size', zone)} />
          {/* Travel handle on the same bottom-right diagonal as the size handle, clear of the four direction labels. */}
          {stick && (() => {
            const offset = box.hw * (stick.radius ?? 0.7) * Math.SQRT1_2, x = box.cx + offset, y = box.cy + offset;
            return <rect class="handle travel-handle" x={x - 6} y={y - 6} width="12" height="12" transform={`rotate(45 ${x} ${y})`}
                         onPointerDown={event => begin(event as PointerEvent, 'travel', zone)} />;
          })()}
        </>}
      </g>;
    })}
  </svg>;
}

function NumberField({ label, value, min, max, step = 1, onChange }: { label: string; value: number; min: number; max: number; step?: number; onChange: (value: number) => void }) {
  return <label class="field">{label}
    <input type="number" value={value} min={min} max={max} step={step}
           onChange={event => { const next = Number((event.target as HTMLInputElement).value); if (Number.isFinite(next)) onChange(Math.min(max, Math.max(min, next))); }} />
  </label>;
}

function Inspector({ session, draft, zone, conflictedBindings, capturing, onZone, onBinding, onCapture, onStick, onRemove }: {
  session: Session; draft: Draft; zone: Zone; conflictedBindings: Set<number>; capturing?: number;
  onZone: (zone: Zone) => void; onBinding: (zone: string, binding: Binding) => void;
  onCapture: (zone: string, kind: 'button' | 'stick', direction?: number) => void; onStick: (zone: string) => void; onRemove: (zone: string) => void;
}) {
  const box = resolveZone(zone, session.surface);
  const bindings = zoneBindings(draft, zone.name);
  const binding = bindings.find(item => (item.mode ?? 'default') === 'default') ?? bindings[0];
  const reshape = (circle: boolean) => {
    const half = Math.min(box.hw, box.hh);
    onZone(placeZone(zone.name, { ...box, circle, hw: circle ? half : box.hw, hh: circle ? half : box.hh }, session.surface));
  };
  return <section class="inspector">
    <h2>{zone.name}</h2>
    <div class="seg">
      <button class={box.circle ? 'on' : ''} aria-pressed={box.circle} onClick={() => reshape(true)}>{t("圆形")}</button>
      <button class={box.circle ? '' : 'on'} aria-pressed={!box.circle} onClick={() => reshape(false)}>{t("矩形")}</button>
    </div>
    {!binding && <div class="row">
      <button class="pill main" onClick={() => onCapture(zone.name, 'button')}>{t("设为按钮并绑定按键")}</button>
      <button class="pill" onClick={() => onStick(zone.name)}>{t("设为摇杆")}</button>
    </div>}
    {binding?.action === 'button' && <>
      <p class={conflictedBindings.has(draft.bindings.indexOf(binding)) ? 'conflict-text' : ''}>{t("按键：")}<b>{bindingText(binding)}</b></p>
      <div class="row"><button class="pill" onClick={() => onCapture(zone.name, 'button')}>{t("重新绑定")}</button></div>
      <div class="seg">{(['tap', 'hold', 'repeat'] as const).map(press =>
        <button class={(binding.press ?? 'hold') === press ? 'on' : ''} aria-pressed={(binding.press ?? 'hold') === press}
                onClick={() => onBinding(zone.name, { ...binding, press, repeat_frames: press === 'repeat' ? binding.repeat_frames ?? 6 : undefined })}>{pressNames()[press]}</button>)}
      </div>
      {binding.press === 'repeat' && <NumberField label={t("连发间隔（帧）")} value={binding.repeat_frames ?? 6} min={2} max={120}
        onChange={value => onBinding(zone.name, { ...binding, repeat_frames: value })} />}
    </>}
    {binding?.action === 'stick' && <>
      <div class={`directions ${conflictedBindings.has(draft.bindings.indexOf(binding)) ? 'conflict-text' : ''}`}>
        {splitSources(binding.source).map((source, index) =>
          <button key={index} class={capturing === index ? 'on' : ''} aria-label={t("{direction}：{key}，点击更换", { direction: stickSteps()[index], key: sourceLabel(source) })}
                  onClick={() => onCapture(zone.name, 'stick', index)}><span>{stickSteps()[index]}</span><b>{capturing === index ? '…' : sourceLabel(source)}</b></button>)}
      </div>
      <small>{t("点击一个方向后按下新键，只更换该方向。")}</small>
      <div class="row">
        <button class="pill" onClick={() => onCapture(zone.name, 'stick')}>{t("逐个绑定")}</button>
        <button class="pill" onClick={() => onBinding(zone.name, { ...binding, source: 'keys:w,a,s,d' })}>WASD</button>
        <button class="pill" onClick={() => onBinding(zone.name, { ...binding, source: 'keys:up,left,down,right' })}>{t("方向键")}</button>
      </div>
      <NumberField label={t("行程（相对半径）")} value={binding.radius ?? 0.7} min={0.1} max={1} step={0.05}
        onChange={value => onBinding(zone.name, { ...binding, radius: Math.round(value * 100) / 100 })} />
      <NumberField label={t("按下停留（帧）")} value={binding.settle_frames ?? 2} min={0} max={30}
        onChange={value => onBinding(zone.name, { ...binding, settle_frames: value })} />
      <NumberField label={t("滑到目标（帧）")} value={binding.ramp_frames ?? 4} min={1} max={30}
        onChange={value => onBinding(zone.name, { ...binding, ramp_frames: value })} />
      <small>{t("拖动圆内的菱形手柄调整行程。")}</small>
    </>}
    <div class="row"><button class="pill danger" onClick={() => onRemove(zone.name)}>{t("删除此控件")}</button></div>
  </section>;
}

function OtherBindings({ draft, conflictedBindings, onRemove }: { draft: Draft; conflictedBindings: Set<number>; onRemove: (index: number) => void }) {
  const others = draft.bindings.map((binding, index) => ({ binding, index })).filter(({ binding }) => !binding.zone);
  if (!others.length) return null;
  return <section class="others">
    <h3>{t("其他绑定")}</h3>
    {others.map(({ binding, index }) => <div class={`other ${conflictedBindings.has(index) ? 'conflict-text' : ''}`} key={index}>
      <span>{bindingText(binding)} → {binding.action === 'key' ? t("安卓键 {key}", { key: binding.key ?? '' }) : t("切换到 {mode}", { mode: binding.target ?? '' })}
        {binding.mode && binding.mode !== 'default' ? <small>{t("模式")}{binding.mode === '*' ? t('全部') : binding.mode}</small> : null}</span>
      <button class="pill danger" onClick={() => onRemove(index)}>{t("删除")}</button>
    </div>)}
  </section>;
}

function CaptureBanner({ capture, onMouse, onCancel }: { capture: Capture; onMouse: (button: number) => void; onCancel: () => void }) {
  const prompt = capture.kind === 'button' ? t("按下要绑定到 {zone} 的键", { zone: capture.zone })
    : capture.direction !== undefined ? t("按下摇杆“{direction}”方向的新键", { direction: stickSteps()[capture.direction] })
    : t("按下摇杆“{direction}”方向的键（{step}/4）", { direction: stickSteps()[capture.collected.length], step: capture.collected.length + 1 });
  return <div class="capture" role="dialog" aria-live="polite">
    <b>{prompt}</b><span>{t("Esc 取消；F1、F2 与 Ctrl/Cmd 组合键保留给宿主。")}</span>
    {capture.kind === 'button' && <div class="mouse-pad" onMouseDown={event => { event.preventDefault(); onMouse(event.button); }}
                                       onContextMenu={event => event.preventDefault()}>{t("在此框内点击鼠标键以绑定鼠标")}</div>}
    <button class="pill" onClick={onCancel}>{t("取消")}</button>
  </div>;
}

function Editor({ rpc, onUnauthorized }: { rpc: Rpc; onUnauthorized: () => void }) {
  const [session, setSession] = useState<Session | null>(null);
  const [draft, setDraft] = useState<Draft | null>(null);
  const [base, setBase] = useState<Binding[]>([]);
  const [frame, setFrame] = useState<Frame | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  const [capture, setCapture] = useState<Capture | null>(null);
  const [status, setStatus] = useState<Status>({ kind: 'info', text: t('正在连接游戏…') });
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  const [done, setDone] = useState(false);
  const captureRef = useRef<Capture | null>(null), draftRef = useRef<Draft | null>(null), capturedAt = useRef(-Infinity);
  const changeCapture = (next: Capture | null) => {
    if (!next && captureRef.current) capturedAt.current = performance.now();
    captureRef.current = next; setCapture(next);
  };
  const replaceDraft = (next: Draft | null) => { draftRef.current = next; setDraft(next); };

  const run = async (work: () => Promise<void>) => {
    setBusy(true);
    try { await work(); } catch (error) {
      if (error instanceof Unauthorized) onUnauthorized();
      setStatus({ kind: 'error', text: message(error) });
    } finally { setBusy(false); }
  };
  const load = async () => {
    const next = await rpc<Session>('control.get');
    setLanguage(next.language);
    setSession(next); replaceDraft(loadDraft(next)); setBase(baseBindings(next)); setDirty(false);
    return next;
  };
  const refreshFrame = async () => {
    const result = await rpc<{ data: string; mime_type: string; sequence: number }>('control.frame');
    setFrame({ url: `data:${result.mime_type};base64,${result.data}`, sequence: result.sequence });
  };
  const freeze = () => run(async () => {
    await rpc('control.freeze'); const next = await load(); await refreshFrame();
    setStatus({ kind: 'info', text: next.persistent ? t('游戏已暂停，可以编辑。') : t('游戏已暂停；临时沙盒的修改只在本次运行生效。') });
  });
  useEffect(() => { void freeze(); }, []);
  // The game resumes by itself when this heartbeat stops (window closed without pagehide).
  useEffect(() => {
    const timer = window.setInterval(() => { void rpc('control.status', { client: 'editor' }).catch(() => undefined); }, 2000);
    return () => window.clearInterval(timer);
  }, [rpc]);

  const update = (next: Draft) => { replaceDraft(next); setDirty(true); };
  const onZone = (zone: Zone) => { const current = draftRef.current; if (current) update({ ...current, zones: current.zones.map(item => item.name === zone.name ? zone : item) }); };
  const onBinding = (zone: string, binding: Binding) => { const current = draftRef.current; if (current) update(setZoneBinding(current, zone, binding)); };
  const startCapture = (zone: string, kind: 'button' | 'stick', direction?: number) => {
    const existing = draftRef.current && zoneBindings(draftRef.current, zone).find(binding => binding.action === kind);
    const template: Binding = existing ?? (kind === 'stick' ? newStick(zone, '') : { source: '', action: 'button', zone, press: 'tap' });
    // A focused button would be activated again by the key's keyup (Space/Enter).
    (document.activeElement as HTMLElement | null)?.blur?.();
    setSelected(zone); changeCapture({ zone, kind, collected: [], template, direction });
  };
  // A new stick takes the first free preset (WASD, then the arrows); only when both are taken does it ask for four keys.
  const makeStick = (current: Draft, zone: string) => {
    const preset = freeStickPreset(current);
    if (!preset) { update(current); startCapture(zone, 'stick'); return; }
    update(setZoneBinding(current, zone, newStick(zone, preset))); setSelected(zone);
    setStatus({ kind: 'info', text: t("{zone} 已用 {keys} 控制上左下右，可在右侧点击单个方向更换按键。", { zone, keys: splitSources(preset).map(sourceLabel).join(' ') }) });
  };
  // Key events can arrive before the next paint; capture state lives in refs so the listener
  // registered once always sees the latest capture and draft.
  const finishSource = (source: string) => {
    const active = captureRef.current, current = draftRef.current;
    if (!active || !current) return;
    if (active.kind === 'button') {
      update(setZoneBinding(current, active.zone, { ...active.template, source })); changeCapture(null); return;
    }
    if (active.direction !== undefined) {
      const group = replaceStickSource(active.template.source, active.direction, source);
      if (!group) { setStatus({ kind: 'error', text: t('摇杆四个方向需要不同的键。') }); return; }
      update(setZoneBinding(current, active.zone, { ...active.template, source: group })); changeCapture(null);
      setStatus({ kind: 'info', text: t("{zone} 的“{direction}”方向改为 {key}。", { zone: active.zone, direction: stickSteps()[active.direction], key: sourceLabel(source) }) }); return;
    }
    if (active.collected.includes(source)) { setStatus({ kind: 'error', text: t('摇杆四个方向需要不同的键。') }); return; }
    const collected = [...active.collected, source];
    if (collected.length < 4) { changeCapture({ ...active, collected }); return; }
    update(setZoneBinding(current, active.zone, { ...active.template, source: joinStickSources(collected) })); changeCapture(null);
  };
  const finishRef = useRef(finishSource);
  finishRef.current = finishSource;
  useEffect(() => {
    const listener = (event: KeyboardEvent) => {
      if (!captureRef.current) return;
      event.preventDefault(); event.stopPropagation();
      if (event.repeat) return;
      if (event.code === 'Escape') { changeCapture(null); return; }
      const result = captureKey(event);
      if ('error' in result) setStatus({ kind: 'error', text: result.error }); else finishRef.current(result.source);
    };
    // A key that finished a capture must not activate a focused button on keyup.
    const swallow = (event: KeyboardEvent) => { if (captureRef.current || event.timeStamp - capturedAt.current < 1000) { event.preventDefault(); event.stopPropagation(); } };
    window.addEventListener('keydown', listener, true);
    window.addEventListener('keyup', swallow, true);
    return () => { window.removeEventListener('keydown', listener, true); window.removeEventListener('keyup', swallow, true); };
  }, []);

  const conflictedBindings = useMemo(() => draft ? conflicts(draft.bindings) : new Set<number>(), [draft]);
  const conflictedZones = useMemo(() => new Set([...conflictedBindings].map(index => draft?.bindings[index]?.zone).filter((zone): zone is string => !!zone)), [conflictedBindings]);

  if (!session || !draft) {
    return <div class="loading"><p class={status.kind}>{status.text}</p><button class="pill" onClick={() => void freeze()} disabled={busy}>{t("重试")}</button></div>;
  }
  if (!session.surface.width || !session.surface.height) {
    return <div class="loading"><p>{t("游戏画面尚未就绪，请在游戏出现画面后重试。")}</p><button class="pill" onClick={() => void freeze()} disabled={busy}>{t("重试")}</button></div>;
  }
  if (done) {
    return <div class="loading"><h1>{t("按键已应用")}</h1><p>{t("游戏继续运行，此窗口会自动关闭；如果没有关闭，可直接关闭此页面。")}</p></div>;
  }
  // Editing happens only while the game is paused; 试玩 and 完成 apply the draft first, so what runs is what was edited.
  const editing = session.frozen;
  const applyDraft = async () => {
    if (!dirty) return '';
    if (conflictedBindings.size) throw new Error(t('存在按键冲突（红色标出），请先修改。'));
    const result = await rpc<{ saved: boolean; config_path: string | null }>('control.apply', { layer: buildLayer(draft, base) });
    await load();
    return result.saved ? t('已保存') : t('临时沙盒不保存到文件');
  };
  const tryOut = () => run(async () => {
    const saved = await applyDraft();
    await rpc('control.resume'); setSession({ ...session, frozen: false }); changeCapture(null);
    setStatus({ kind: 'info', text: t("试玩中：到游戏窗口里操作试试{saved}。要继续修改，点“返回编辑”。", { saved: saved ? ` (${saved})` : '' }) });
  });
  // The launcher closes this window once the game reports the finished edit; a browser tab stays on the done page.
  const finish = () => run(async () => {
    await applyDraft();
    changeCapture(null);
    await rpc('control.resume', { close: true });
    setDone(true);
  });
  const selectedZone = draft.zones.find(zone => zone.name === selected);
  return <div class="editor">
    <header>
      <h1>{t("按键设置")}</h1>
      <span class="state"><span class={`led ${editing ? 'warning' : 'running'}`} />{editing ? t('编辑中 · 游戏已暂停') : t('试玩中 · 游戏运行')}</span>
      {editing && <>
        <button class="pill" onClick={() => { const added = addControl(draft, 'button', session.surface); update(added.draft); startCapture(added.zone, 'button'); }}>{t("添加按钮")}</button>
        <button class="pill" onClick={() => { const added = addControl(draft, 'stick', session.surface); makeStick(added.draft, added.zone); }}>{t("添加摇杆")}</button>
      </>}
      <span class="spacer" />
      {dirty && <span class="unsaved">{t("有未应用的修改")}</span>}
      {editing ? <>
        <button class="pill" disabled={busy || !dirty} title={t("回到上次应用的按键")} onClick={() => void run(async () => { await load(); setStatus({ kind: 'info', text: t('已放弃未应用的修改。') }); })}>{t("放弃修改")}</button>
        <button class="pill" disabled={busy || conflictedBindings.size > 0} title={t("应用修改并继续游戏；本窗口保持打开，可随时返回编辑")} onClick={() => void tryOut()}>{t("试玩")}</button>
      </> : <button class="pill" disabled={busy} title={t("暂停游戏，继续修改按键")} onClick={() => void freeze()}>{t("返回编辑")}</button>}
      <button class="pill main" disabled={busy || conflictedBindings.size > 0} title={t("应用修改、继续游戏并关闭此窗口")} onClick={() => void finish()}>{t("完成")}</button>
    </header>
    <p class={`status ${status.kind}`} role="status">{status.text}</p>
    {capture && <CaptureBanner capture={capture} onCancel={() => changeCapture(null)}
      onMouse={button => { const result = captureMouse(button); if ('error' in result) setStatus({ kind: 'error', text: result.error }); else finishSource(result.source); }} />}
    <div class={`workspace ${editing ? '' : 'locked'}`} inert={!editing}>
      <div class="canvas"><Stage session={session} draft={draft} frame={frame} selected={selected} conflicted={conflictedZones}
        onSelect={setSelected} onZone={onZone} onBinding={onBinding} />
        {!editing && <p class="canvas-note">{t("试玩中，画面为暂停时的截图")}</p>}
        {editing && !frame && <div class="canvas-note">{t("还没有游戏画面")}<button class="pill" disabled={busy} onClick={() => void run(refreshFrame)}>{t("重新获取画面")}</button></div>}
      </div>
      <aside>
        {selectedZone ? <Inspector session={session} draft={draft} zone={selectedZone} conflictedBindings={conflictedBindings}
            capturing={capture?.zone === selectedZone.name ? capture.direction : undefined}
            onZone={onZone} onBinding={onBinding} onCapture={startCapture} onStick={zone => { if (draftRef.current) makeStick(draftRef.current, zone); }}
            onRemove={zone => { update(removeControl(draft, zone)); setSelected(null); }} />
          : <section class="inspector"><p>{t("点击画面上的控件进行编辑，拖动移动，拖动圆点缩放。新控件放在画面中央。")}</p></section>}
        <OtherBindings draft={draft} conflictedBindings={conflictedBindings}
          onRemove={index => update({ ...draft, bindings: draft.bindings.filter((_, i) => i !== index) })} />
        <p class="path"><small>{session.config_path ? t("保存位置：{path}", { path: session.config_path }) : t('临时沙盒：修改不写入文件')}</small></p>
      </aside>
    </div>
  </div>;
}

function App() {
  const currentLanguage = useLanguage();
  useEffect(updateDocumentLanguage, [currentLanguage]);
  const [token, setToken] = useState(() => takeToken(location, sessionStorageOrUndefined(), url => history.replaceState(null, '', url)));
  const rpc = useMemo(() => token ? createRpc(token) : null, [token]);
  useEffect(() => {
    if (!token) return;
    const listener = () => resumeOnUnload(token);
    window.addEventListener('pagehide', listener);
    return () => window.removeEventListener('pagehide', listener);
  }, [token]);
  if (!rpc) return <TokenForm onToken={value => { rememberToken(value, sessionStorageOrUndefined()); setToken(value); }} />;
  return <Editor rpc={rpc} onUnauthorized={() => setToken(undefined)} />;
}

function sessionStorageOrUndefined(): Storage | undefined {
  try { return window.sessionStorage; } catch { return undefined; }
}

updateDocumentLanguage();
render(<App />, document.getElementById('app')!);
