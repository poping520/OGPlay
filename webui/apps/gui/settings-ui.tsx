import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { choiceLabel, fieldMeta, stepValue, type SettingField, type SettingValue } from './settings';
import { shellPalette } from './library';
import logo from './assets/logo.png';
import './settings.css';

// Settings pages ("主机之夜" layout): top bar with save actions, group sidebar, field rows, gamepad legend. The
// "glass" skin is the same layout restyled (frosted violet); the skin only changes classes.
export type Skin = 'console' | 'glass';
const ICON: Record<string, string> = {
  general: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  storage: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  graphics: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
  display: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
  audio: '<path d="M11 5 6 9H3v6h3l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13"/>',
  input: '<rect x="2" y="7" width="20" height="10" rx="5"/><path d="M7 10v4M5 12h4"/><circle cx="16" cy="11" r=".6"/><circle cx="18" cy="13" r=".6"/>',
  vm: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/>',
  performance: '<path d="M12 14l4-4"/><path d="M3.3 17a9 9 0 1 1 17.4 0"/>',
  device: '<rect x="7" y="2" width="10" height="20" rx="2"/><path d="M11 18h2"/>',
  network: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>',
  diagnostics: '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
  control: '<path d="M4 17l6-5-6-5M12 19h8"/>',
  advanced: '<path d="M4 17l6-5-6-5M12 19h8"/>',
  data: '<path d="M21 8v12H3V8"/><path d="M1 4h22v4H1zM10 12h4"/>',
  compatibility: '<path d="M10 3h4v3a2 2 0 1 0 0 4v0h3v4h-3a2 2 0 1 0 0 4v3h-4v-3a2 2 0 1 0-4 0v3H3v-7h3a2 2 0 1 0 0-4H3V6h7z"/>',
  about: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v0"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v0"/>',
};
// glass skin: each group has its own glossy chip colours
const TINT: Record<string, [string, string]> = {
  general: ['#ff8be0', '#c13dff'], storage: ['#86e3ff', '#3b6ff6'], graphics: ['#ffd36e', '#ff6a3d'], audio: ['#7ff5cf', '#10a39a'],
  input: ['#b9c3ff', '#5b4cf0'], vm: ['#6ff0de', '#0b8fd8'], network: ['#9fd0ff', '#2563eb'], diagnostics: ['#ffb1c1', '#e11d5c'],
  control: ['#d2c2ff', '#7c3aed'], about: ['#fff07a', '#f59e0b'], performance: ['#ffd36e', '#ff4f7b'], device: ['#86e3ff', '#5b4cf0'],
  display: ['#ffd36e', '#ff6a3d'], data: ['#9bf3b4', '#16a34a'], compatibility: ['#f7b5ff', '#a21caf'], advanced: ['#d2c2ff', '#7c3aed'],
  info: ['#9fd0ff', '#2563eb'], warn: ['#ffd36e', '#ff8a3d'],
};
export function GroupIcon({ name, tone = name }: { name: string; tone?: string }) {
  const [g1, g2] = TINT[tone] ?? TINT.info;
  return <span class="ic" style={{ '--g1': g1, '--g2': g2 }}>
    <svg viewBox="0 0 24 24" aria-hidden="true" dangerouslySetInnerHTML={{ __html: ICON[name] ?? ICON.info }} />
  </span>;
}
export function Key({ children }: { children: ComponentChildren }) { return <span class="key" aria-hidden="true">{children}</span>; }
export function Note({ icon, title, warn = false, children }: { icon: string; title?: string; warn?: boolean; children?: ComponentChildren }) {
  return <div class={`note ${warn ? 'warn' : ''}`}><GroupIcon name={icon} tone={warn ? 'warn' : icon} /><div>{title && <b>{title}</b>}{children}</div></div>;
}
export function Paths({ rows }: { rows: [string, string][] }) {
  return <dl class="paths">{rows.map(([label, path]) => <Fragment key={label}><dt>{label}</dt><dd>{path}</dd></Fragment>)}</dl>;
}

// ---------------------------------------------------------------- controls
function Control({ field, value, disabled, change, browse }: {
  field: SettingField; value: SettingValue; disabled: boolean; change: (value: SettingValue) => void; browse?: () => void;
}) {
  const meta = fieldMeta(field);
  if (typeof field.initial === 'boolean')
    return <button class={`switch ${value === true ? 'on' : ''}`} role="switch" aria-checked={value === true} aria-label={meta.label} disabled={disabled} onClick={() => change(value !== true)} />;
  if (field.choices.length && field.choices.length <= 3 && field.choices.every(choice => choiceLabel(field, choice).length <= 6))
    return <div class="seg" role="radiogroup" aria-label={meta.label}>{field.choices.map(choice =>
      <button key={choice} role="radio" aria-checked={choice === value} class={choice === value ? 'on' : ''} disabled={disabled} onClick={() => change(choice)}>{choiceLabel(field, choice)}</button>)}</div>;
  if (field.choices.length) {
    const at = field.choices.indexOf(String(value));
    return <div class="cyc" role="group" aria-label={meta.label}>
      <button aria-label="上一项" disabled={disabled} onClick={() => change(stepValue(field, value, -1))}>‹</button>
      <div class="val"><span>{choiceLabel(field, String(value))}</span><div class="pips">{field.choices.map((choice, index) => <i key={choice} class={index === at ? 'on' : ''} />)}</div></div>
      <button aria-label="下一项" disabled={disabled} onClick={() => change(stepValue(field, value, 1))}>›</button>
    </div>;
  }
  if (typeof field.initial === 'number') {
    const number = typeof value === 'number' ? value : NaN;
    if (meta.unit === '%') {
      const percent = Number.isFinite(number) ? (number - field.min) / (field.max - field.min) * 100 : 0;
      return <div class="range"><input type="range" min={field.min} max={field.max} step={1} value={Number.isFinite(number) ? number : field.min} disabled={disabled}
        aria-label={meta.label} style={{ '--p': `${percent}%` }} onInput={event => change(event.currentTarget.valueAsNumber)} /><b>{Number.isFinite(number) ? number : '—'}%</b></div>;
    }
    return <div class="step">
      <button aria-label="减少" disabled={disabled} onClick={() => change(stepValue(field, value, -1))}>−</button>
      <input type="number" min={field.min} max={field.max} step={1} value={Number.isNaN(number) ? '' : String(number)} disabled={disabled} aria-label={meta.label}
        onInput={event => change(event.currentTarget.valueAsNumber)} />
      {meta.unit && <small>{meta.unit}</small>}
      <button aria-label="增加" disabled={disabled} onClick={() => change(stepValue(field, value, 1))}>+</button>
    </div>;
  }
  return <div class="txt"><input type="text" value={String(value)} disabled={disabled} aria-label={meta.label}
    placeholder={field.directory ? '留空使用默认行为' : field.pending.startsWith('预留') ? '跟随设备预设' : '未设置'} onInput={event => change(event.currentTarget.value)} />
    {browse && <button disabled={disabled} onClick={browse}>浏览…</button>}</div>;
}

// What a page tells the frame about one row.
export interface RowModel {
  value: SettingValue; changed: boolean; disabled: boolean;
  inherit?: { overridden: boolean; saved: boolean; source: string; shown: string };
}
function FieldRow({ field, model, focused, focus, change, browse, toggleInherit }: {
  field: SettingField; model: RowModel; focused: boolean; focus: () => void;
  change: (value: SettingValue) => void; browse?: () => void; toggleInherit?: () => void;
}) {
  const meta = fieldMeta(field), inherit = model.inherit, inherited = inherit && !inherit.overridden;
  return <div class={`row ${focused ? 'focus' : ''} ${model.changed ? 'changed' : ''} ${inherited ? 'inherit' : ''}`} data-row onPointerDown={focus}>
    <GroupIcon name={field.group} />
    <div class="lbl"><b>{meta.label}</b>
      {(model.changed || inherit || meta.reserved || meta.hint) && <div class="meta">
        {model.changed && <span class="tag mod">未保存</span>}
        {inherited && <span class="inherit-val">{inherit.source} · {inherit.shown}</span>}
        {inherit?.overridden && inherit.saved && !model.changed && <span class="tag over">已覆盖</span>}
        {meta.reserved && <span class="tag pending" title={field.pending}>预留 · 仅保存</span>}
        {(meta.hint || meta.reserved) && <span>{meta.hint || meta.reserved}</span>}
      </div>}
    </div>
    <div class="ctl">
      <Control field={field} value={model.value} disabled={model.disabled || !!inherited} change={change} browse={field.directory ? browse : undefined} />
      {inherit && <button class={`inh ${inherited ? 'on' : ''}`} disabled={model.disabled} title={`继承${inherit.source}`} onClick={toggleInherit}>
        <span class="sw" />{inherited ? '继承' : '单独设置'}</button>}
    </div>
  </div>;
}

// ---------------------------------------------------------------- toast
export function useToast(): [string, (text: string) => void] {
  const [toast, setToast] = useState('');
  const timer = useRef(0);
  useEffect(() => () => window.clearTimeout(timer.current), []);
  return [toast, text => { setToast(text); window.clearTimeout(timer.current); timer.current = window.setTimeout(() => setToast(''), 2400); }];
}

// ---------------------------------------------------------------- frame
export interface FrameProps {
  skin: Skin;
  art?: string;                                  // game icon for the console backdrop
  tint?: [number, number, number];               // game colour for the glass sky
  crumb: ComponentChildren;
  idle: string;                                  // status text when nothing is unsaved
  resetLabel: string;
  busy: boolean;
  loaded: boolean;
  changed: string[];                             // unsaved keys
  who?: ComponentChildren;
  groupsTitle: string;
  groups: readonly (readonly [string, string, string])[];
  group: string;
  setGroup: (group: string) => void;
  fields: SettingField[];                        // all fields of the page
  sectionBefore?: Record<string, string>;
  model: (field: SettingField) => RowModel;
  change: (field: SettingField, value: SettingValue) => void;
  browse: (field: SettingField) => void;
  toggleInherit?: (field: SettingField) => void;
  fileLabel: string;
  filePath: string;
  kicker: string;
  note: string;
  top?: ComponentChildren;
  end?: ComponentChildren;
  error: string;
  toast: string;
  discard: 'back' | 'reload' | null;
  keep: () => void;
  confirmDiscard: () => void;
  back: () => void;
  reload: () => void;
  reset: () => void;
  save: () => void;
}
const editable = (target: EventTarget | null) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName));

export function SettingsFrame(props: FrameProps) {
  const { skin, changed, fields, group, groups } = props;
  // The ref answers key repeat that arrives before the next render.
  const [focus, setFocusState] = useState(0);
  const focusRef = useRef(0);
  const setFocus = (index: number) => { focusRef.current = index; setFocusState(index); };
  const main = useRef<HTMLElement>(null);
  const dialog = useRef<HTMLDialogElement>(null);
  const visible = fields.filter(field => field.group === group);
  const [, title, description] = groups.find(([key]) => key === group) ?? groups[0];
  const dirtyGroups = new Set(fields.filter(field => changed.includes(field.key)).map(field => field.group));
  const selectGroup = (next: string) => { props.setGroup(next); setFocus(0); main.current?.scrollTo({ top: 0 }); };
  useEffect(() => { if (props.discard) dialog.current?.showModal(); else dialog.current?.close(); }, [props.discard]);
  useEffect(() => { if (focus >= visible.length) setFocus(0); }, [visible.length]);
  // Keyboard mirrors the gamepad legend in the footer.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      if (props.discard) {
        if (event.key === 'a' || event.key === 'Enter') { event.preventDefault(); props.confirmDiscard(); }
        else if (event.key === 'b') props.keep();
        return;
      }
      if (editable(event.target)) {
        if ((event.key === 'Escape' || event.key === 'Enter') && (event.target as HTMLInputElement).type !== 'range') (event.target as HTMLElement).blur();
        if (!(event.target instanceof HTMLInputElement && event.target.type === 'range')) return;
        if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
      }
      const focus = focusRef.current, field = visible[focus];
      const at = groups.findIndex(([key]) => key === group);
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        if (!visible.length) return;
        event.preventDefault();
        const next = (focus + (event.key === 'ArrowDown' ? 1 : visible.length - 1)) % visible.length;
        setFocus(next);
        requestAnimationFrame(() => main.current?.querySelectorAll('[data-row]')[next]?.scrollIntoView({ block: 'nearest', behavior: 'smooth' }));
      } else if ((event.key === 'ArrowLeft' || event.key === 'ArrowRight') && field) {
        event.preventDefault();
        const model = props.model(field);
        if (!model.disabled && !(model.inherit && !model.inherit.overridden) && (typeof field.initial !== 'string' || field.choices.length))
          props.change(field, stepValue(field, model.value, event.key === 'ArrowRight' ? 1 : -1));
      } else if ((event.key === 'Enter' || event.key === 'a') && field) {
        event.preventDefault();
        const model = props.model(field);
        if (model.disabled || (model.inherit && !model.inherit.overridden)) return;
        if (typeof field.initial === 'boolean' || field.choices.length) props.change(field, stepValue(field, model.value, 0));
        else main.current?.querySelectorAll('[data-row]')[focus]?.querySelector<HTMLInputElement>('input')?.focus();
      } else if (event.key === 'q' || event.key === 'e') {
        selectGroup(groups[(at + (event.key === 'e' ? 1 : groups.length - 1)) % groups.length][0]);
      } else if (event.key === 'i' && field && props.toggleInherit) props.toggleInherit(field);
      else if (event.key === 'x') props.reset();
      else if (event.key === 'y') props.save();
      else if (event.key === 'Escape' || event.key === 'b') props.back();
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });
  const palette = props.tint ? shellPalette(props.tint) : null;
  const rows = visible.map((field, index) => <Fragment key={field.key}>
    {props.sectionBefore?.[field.key] && <div class="sect">{props.sectionBefore[field.key]}</div>}
    <FieldRow field={field} model={props.model(field)} focused={index === focus} focus={() => setFocus(index)}
      change={value => props.change(field, value)} browse={() => props.browse(field)} toggleInherit={props.toggleInherit && (() => props.toggleInherit!(field))} />
  </Fragment>);
  return <div class={`cfg skin-${skin}`} style={palette ? { '--t1': palette.light, '--t2': palette.shell } : {}}>
    <div class="cfg-bg" aria-hidden="true"><div class={`art ${props.art ? '' : 'plain'}`} style={props.art ? { backgroundImage: `url("${props.art}")` } : {}} /><div class="fade" /><div class="grain" /></div>
    <header class="cfg-top">
      <div class="brand"><img src={logo} alt="" />OGPlay</div>
      <button class="back" disabled={props.busy} onClick={props.back}><Key>B</Key>游戏库</button>
      <span class="crumb">{props.crumb}</span>
      <span class="spacer" />
      <span class={`dirty ${changed.length ? 'on' : ''}`} role="status"><i />{changed.length ? `${changed.length} 项未保存` : props.idle}</span>
      <button class="pill" disabled={props.busy} onClick={props.reload}><Key>⟳</Key>重新载入</button>
      <button class="pill" disabled={props.busy || !props.loaded} onClick={props.reset}><Key>X</Key>{props.resetLabel}</button>
      <button class="pill main" disabled={props.busy || !props.loaded || !changed.length} onClick={props.save}><Key>Y</Key>保存设置{changed.length ? ` · ${changed.length}` : ''}</button>
    </header>
    <aside class="cfg-side">
      {props.who}
      <div class="groups-h"><span class="sh">L</span>{props.groupsTitle}<span class="sh">R</span></div>
      <nav class="groups" aria-label={props.groupsTitle}>{groups.map(([key, label]) => {
        const count = fields.filter(field => field.group === key).length;
        return <button key={key} class={key === group ? 'on' : ''} aria-current={key === group ? 'page' : undefined} onClick={() => selectGroup(key)}>
          <GroupIcon name={key} />{label}{dirtyGroups.has(key) && <span class="dot" />}<span class="n">{count || ''}</span></button>;
      })}</nav>
      <div class="cfg-file">{props.fileLabel}<span title={props.filePath}><span dir="ltr">{props.filePath}</span></span></div>
    </aside>
    <main class="cfg-main" ref={main}>
      <div class="head"><div><div class="kicker"><i />{props.kicker}</div><h1>{title}</h1><p>{description}</p></div>
        <div class="legend"><span><i class="l-mod" />未保存</span>{props.toggleInherit && <span><i class="l-inh" />继承</span>}<span><i class="l-pending" />预留 · 仅保存</span></div></div>
      <div class="rows" key={group} aria-busy={props.busy}>
        {props.error && <Note icon="info" title="操作失败" warn><pre role="alert">{props.error}</pre></Note>}
        {props.loaded ? <>{props.top}{rows}{props.end}</> : !props.error && <Note icon="info" title={props.busy ? '正在读取设置…' : '设置不可用'} />}
      </div>
    </main>
    <footer class="cfg-foot">
      <span class="hk"><Key>↑ ↓</Key>选择</span><span class="hk"><Key>← →</Key>调整</span><span class="hk"><Key>A</Key>切换</span>
      <span class="hk"><Key>B</Key>返回</span><span class="hk"><Key>X</Key>{props.resetLabel}</span><span class="hk"><Key>Y</Key>保存</span>
      <span class="hk"><Key>L R</Key>切换分组<kbd>Q E</kbd></span>
      <span class="note-text">{props.note}</span>
    </footer>
    <dialog ref={dialog} aria-label="有未保存的修改" onCancel={event => { event.preventDefault(); props.keep(); }}>
      <div class="kicker"><i />Unsaved</div>
      <h2>放弃 {changed.length} 项修改？</h2>
      <p>{props.discard === 'back' ? '返回游戏库前，' : '重新载入前，'}这些修改不会写入配置。</p>
      <ul>{fields.filter(field => changed.includes(field.key)).map(field => <li key={field.key}>{fieldMeta(field).label}</li>)}</ul>
      <div class="acts"><button class="pill" autoFocus onClick={props.keep}><Key>B</Key>继续编辑</button>
        <button class="pill danger" onClick={props.confirmDiscard}><Key>A</Key>放弃修改</button></div>
    </dialog>
    {props.toast && <div class="toast" role="status"><i />{props.toast}</div>}
  </div>;
}
