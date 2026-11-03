import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { changedKeys, choiceLabel, fieldMeta, RESERVED_MARK, stepValue, shownFields, shownGroups, type SettingField, type SettingValue } from './settings';
import { shellStyle } from '../cartridge/cartridge';
import { t } from '../shared/i18n';
import { Back, Brand, type Skin } from '../kit';
import { Glyph } from '../../../packages/ui-kit/glyph';
import { Key } from '../../../packages/ui-kit/key';
import { SETTING_ICONS } from './setting-icons';
import './settings.css';

// Settings pages ("磨砂黑" layout): top bar with save state, group sidebar, field rows, gamepad legend. Edits are
// written as they are made. The skin (kit.tsx) only changes classes.
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
// `glyph` draws a setting's own icon (setting-icons.ts) on its group's chip.
export function GroupIcon({ name, tone = name, glyph }: { name: string; tone?: string; glyph?: string }) {
  const [g1, g2] = TINT[tone] ?? TINT.info;
  return <span class="ic" style={{ '--g1': g1, '--g2': g2 }}>
    <svg viewBox="0 0 24 24" aria-hidden="true" dangerouslySetInnerHTML={{ __html: glyph ?? ICON[name] ?? ICON.info }} />
  </span>;
}
export function Note({ icon, title, warn = false, children }: { icon: string; title?: string; warn?: boolean; children?: ComponentChildren }) {
  return <div class={`note ${warn ? 'warn' : ''}`}><GroupIcon name={icon} tone={warn ? 'warn' : icon} /><div>{title && <b>{title}</b>}{children}</div></div>;
}
export function Paths({ rows }: { rows: [string, string][] }) {
  return <dl class="paths">{rows.map(([label, path]) => <Fragment key={label}><dt>{label}</dt><dd>{path}</dd></Fragment>)}</dl>;
}

// ---------------------------------------------------------------- controls
// Display width in Latin-letter units: wide (CJK) characters count twice, so both languages pick the same control.
const textWidth = (text: string) => [...text].reduce((width, char) => width + (/[\u2e80-\u9fff\uff00-\uffef]/.test(char) ? 2 : 1), 0);
// Typed text, numbers and slider drags are written once the edit ends (Enter, leaving the field or releasing the
// slider), not on every keystroke; Esc drops the edit.
function useTyped(value: string, commit: (text: string) => void) {
  const [text, setText] = useState<string | null>(null);
  const edit = useRef<string | null>(null);
  const set = (next: string | null) => { edit.current = next; setText(next); };
  return { shown: text ?? value, events: {
    onInput: (event: Event) => set((event.currentTarget as HTMLInputElement).value),
    onKeyDown: (event: KeyboardEvent) => { if (event.key === 'Escape') set(null); },
    onChange: () => { const typed = edit.current; set(null); if (typed !== null) commit(typed); },
    onBlur: () => set(null),
  } };
}
function Control({ field, value, disabled, change, browse }: {
  field: SettingField; value: SettingValue; disabled: boolean; change: (value: SettingValue) => void; browse?: () => void;
}) {
  const meta = fieldMeta(field);
  const typed = useTyped(String(value), text => change(typeof field.initial === 'number' ? (text.trim() ? Number(text) : NaN) : text));
  if (typeof field.initial === 'boolean')
    return <button class={`switch ${value === true ? 'on' : ''}`} role="switch" aria-checked={value === true} aria-label={meta.label} disabled={disabled} onClick={() => change(value !== true)} />;
  if (field.choices.length && field.choices.length <= 3 && field.choices.every(choice => textWidth(choiceLabel(field, choice)) <= 14))
    return <div class="seg" role="radiogroup" aria-label={meta.label}>{field.choices.map(choice =>
      <button key={choice} role="radio" aria-checked={choice === value} class={choice === value ? 'on' : ''} disabled={disabled} onClick={() => change(choice)}>{choiceLabel(field, choice)}</button>)}</div>;
  if (field.choices.length) {
    const at = field.choices.indexOf(String(value));
    return <div class="cyc" role="group" aria-label={meta.label}>
      <button aria-label={t('上一项')} disabled={disabled} onClick={() => change(stepValue(field, value, -1))}>‹</button>
      <div class="val"><span>{choiceLabel(field, String(value))}</span><div class="pips">{field.choices.map((choice, index) => <i key={choice} class={index === at ? 'on' : ''} />)}</div></div>
      <button aria-label={t('下一项')} disabled={disabled} onClick={() => change(stepValue(field, value, 1))}>›</button>
    </div>;
  }
  if (typeof field.initial === 'number') {
    const number = typed.shown.trim() ? Number(typed.shown) : NaN;
    if (meta.unit === '%') {
      const percent = Number.isFinite(number) ? (number - field.min) / (field.max - field.min) * 100 : 0;
      return <div class="range"><input type="range" min={field.min} max={field.max} step={1} value={Number.isFinite(number) ? number : field.min} disabled={disabled}
        aria-label={meta.label} style={{ '--p': `${percent}%` }} {...typed.events} /><b>{Number.isFinite(number) ? number : '—'}%</b></div>;
    }
    return <div class="step">
      <button aria-label={t('减少')} disabled={disabled} onClick={() => change(stepValue(field, value, -1))}>−</button>
      <input type="number" min={field.min} max={field.max} step={1} value={Number.isNaN(number) ? '' : typed.shown} disabled={disabled} aria-label={meta.label}
        {...typed.events} />
      {meta.unit && <small>{meta.unit}</small>}
      <button aria-label={t('增加')} disabled={disabled} onClick={() => change(stepValue(field, value, 1))}>+</button>
    </div>;
  }
  return <div class="txt"><input type="text" value={typed.shown} disabled={disabled} aria-label={meta.label}
    placeholder={field.directory ? t('留空使用默认行为') : field.pending.startsWith(RESERVED_MARK) ? t('跟随设备预设') : t('未设置')} {...typed.events} />
    {browse && <button disabled={disabled} onClick={browse}>{t('浏览…')}</button>}</div>;
}

// What a page tells the frame about one row.
export interface RowModel {
  value: SettingValue; disabled: boolean;
  inherit?: { overridden: boolean; source: string; shown: string };
}
// Per-game rows never lock: editing one makes it this game's own value (outlined, "已单独设置"), and the restore
// button beside the control drops it back to the inherited one.
function FieldRow({ field, model, focused, focus, change, browse, restoreInherit }: {
  field: SettingField; model: RowModel; focused: boolean; focus: () => void;
  change: (value: SettingValue) => void; browse?: () => void; restoreInherit?: () => void;
}) {
  const meta = fieldMeta(field), inherit = model.inherit, overridden = !!inherit?.overridden;
  return <div class={`row ${focused ? 'focus' : ''} ${overridden ? 'override' : ''}`} data-row onPointerDown={focus}>
    <GroupIcon name={field.group} glyph={SETTING_ICONS[field.key]} />
    <div class="lbl"><b>{meta.label}</b>
      {(inherit || meta.reserved || meta.hint) && <div class="meta">
        {inherit && !overridden && <span class="inherit-val">{inherit.source} · {inherit.shown}</span>}
        {inherit && overridden && <><span class="tag over">{t('已单独设置')}</span>
          <span class="inherit-val">{t('{source}为 {value}', { source: inherit.source, value: inherit.shown })}</span></>}
        {meta.reserved && <span class="tag pending" title={meta.reserved}>{t('预留 · 仅保存')}</span>}
        {(meta.hint || meta.reserved) && <span>{meta.hint || meta.reserved}</span>}
      </div>}
    </div>
    <div class="ctl">
      <Control field={field} value={model.value} disabled={model.disabled} change={change} browse={field.directory ? browse : undefined} />
      {inherit && (overridden
        ? <button class="restore" disabled={model.disabled} title={t('恢复{source}', { source: inherit.source })} onClick={restoreInherit}>
            <Glyph name="reload" />{t('恢复{source}', { source: inherit.source })}</button>
        : <span class="restore-slot" aria-hidden="true" />)}
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

// ---------------------------------------------------------------- auto save
// Each edit is checked, then written straight away: one request at a time, edits made meanwhile go out together in
// the next one against the new revision. A rejected write (host validation, a revision changed elsewhere) reloads
// the host's values and reports the error; nothing is left half-saved in the page.
export interface SavedSettings { revision: string; values: Record<string, SettingValue> }
type Values = Record<string, SettingValue>;
export function useAutoSave<T extends SavedSettings>({ load, write, check, accepted }: {
  load: () => Promise<T>; write: (saved: T, values: Values) => Promise<T>;
  check: (saved: T, values: Values) => string | null; accepted?: (value: T) => void;
}) {
  const [saved, setSaved] = useState<T | null>(null);
  const [draft, setDraft] = useState<Values>({});
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const savedRef = useRef<T | null>(null), draftRef = useRef<Values>({});
  const writing = useRef(false), settled = useRef<(() => void)[]>([]);
  const show = (values: Values) => { draftRef.current = values; setDraft(values); };
  const accept = (value: T) => { savedRef.current = value; setSaved(value); show({ ...value.values }); accepted?.(value); };
  const flush = async () => {
    if (writing.current) return;
    writing.current = true; setSaving(true);
    let ok = true;
    for (let base = savedRef.current; base && changedKeys(base.values, draftRef.current).length; base = savedRef.current) {
      const values = draftRef.current;
      try {
        const next = await write(base, values);
        savedRef.current = next; setSaved(next); accepted?.(next);
        if (draftRef.current === values) show({ ...next.values });
      } catch (reason) {
        ok = false; setError(reason instanceof Error ? reason.message : String(reason));
        try { accept(await load()); } catch { show({ ...base.values }); }
        break;
      }
    }
    writing.current = false; setSaving(false);
    for (const callback of settled.current.splice(0)) if (ok) callback();
  };
  return {
    saved, draft, saving, error, setError,
    latest: () => savedRef.current,
    load: async () => accept(await load()),
    edit: (update: (current: Values) => Values) => {
      const base = savedRef.current;
      if (!base) return;
      const next = update(draftRef.current), invalid = check(base, next);
      if (invalid) { setError(invalid); return; }
      setError(''); show(next); void flush();
    },
    // Runs once the pending writes succeed; dropped if one fails, so the error stays in view.
    afterSave: (callback: () => void) => { if (writing.current) settled.current.push(callback); else callback(); },
  };
}

// ---------------------------------------------------------------- frame
export interface FrameProps {
  skin: Skin;
  art?: string;                                  // game icon for the console backdrop
  tint?: [number, number, number];               // game colour for the glass sky
  crumb: ComponentChildren;
  idle: string;                                  // status text while nothing is being written
  resetLabel: string;
  resetTitle: string;                            // confirmation heading
  resetKeys: string[];                           // keys the reset would change
  busy: boolean;
  saving: boolean;
  loaded: boolean;
  who?: ComponentChildren;
  groupsTitle: string;
  groups: readonly (readonly [string, string])[];
  group: string;
  setGroup: (group: string) => void;
  fields: SettingField[];                        // all fields of the page
  reservedVisible: boolean;                      // false in release builds: reserved rows (and groups left empty) hidden
  sectionBefore?: Record<string, string>;
  // Content shown right under a field's row (e.g. a preview of its choices).
  after?: Record<string, ComponentChildren>;
  model: (field: SettingField) => RowModel;
  change: (field: SettingField, value: SettingValue) => void;
  browse: (field: SettingField) => void;
  restoreInherit?: (field: SettingField) => void;   // per-game pages: drop this game's own value
  fileLabel: string;
  filePath: string;
  kicker: string;
  note: string;
  top?: ComponentChildren;
  end?: ComponentChildren;
  error: string;
  toast: string;
  back: () => void;
  reload: () => void;
  reset: () => void;                             // after confirmation
}
const editable = (target: EventTarget | null) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName));

export function SettingsFrame(props: FrameProps) {
  const { skin, group } = props;
  const fields = shownFields(props.fields, props.reservedVisible);
  // Which groups survive is only known once the fields arrive: list none until then rather than all and then fewer.
  const groups = props.loaded ? shownGroups(props.groups, props.fields, fields) : [];
  // A reset writes several settings at once, so it is the one edit that asks first.
  const [confirm, setConfirm] = useState(false);
  const askReset = () => { if (!props.busy && props.loaded && props.resetKeys.length) setConfirm(true); };
  const confirmReset = () => { setConfirm(false); props.reset(); };
  // The ref answers key repeat that arrives before the next render.
  const [focus, setFocusState] = useState(0);
  const focusRef = useRef(0);
  const setFocus = (index: number) => { focusRef.current = index; setFocusState(index); };
  const main = useRef<HTMLElement>(null);
  const dialog = useRef<HTMLDialogElement>(null);
  const visible = fields.filter(field => field.group === group);
  const [, title] = props.groups.find(([key]) => key === group) ?? props.groups[0];
  const selectGroup = (next: string) => { props.setGroup(next); setFocus(0); main.current?.scrollTo({ top: 0 }); };
  useEffect(() => { if (confirm) dialog.current?.showModal(); else dialog.current?.close(); }, [confirm]);
  useEffect(() => { if (focus >= visible.length) setFocus(0); }, [visible.length]);
  // Keyboard mirrors the gamepad legend in the footer.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      if (confirm) {
        if (event.key === 'a' || event.key === 'Enter') { event.preventDefault(); confirmReset(); }
        else if (event.key === 'b') setConfirm(false);
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
        if (!model.disabled && (typeof field.initial !== 'string' || field.choices.length))
          props.change(field, stepValue(field, model.value, event.key === 'ArrowRight' ? 1 : -1));
      } else if ((event.key === 'Enter' || event.key === 'a') && field) {
        event.preventDefault();
        const model = props.model(field);
        if (model.disabled) return;
        if (typeof field.initial === 'boolean' || field.choices.length) props.change(field, stepValue(field, model.value, 0));
        else main.current?.querySelectorAll('[data-row]')[focus]?.querySelector<HTMLInputElement>('input')?.focus();
      } else if ((event.key === 'q' || event.key === 'e') && groups.length) {
        selectGroup(groups[(at + (event.key === 'e' ? 1 : groups.length - 1)) % groups.length][0]);
      } else if (event.key === 'i' && field && props.restoreInherit && props.model(field).inherit?.overridden) props.restoreInherit(field);
      else if (event.key === 'x') askReset();
      else if (event.key === 'Escape' || event.key === 'b') props.back();
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });
  const rows = visible.map((field, index) => <Fragment key={field.key}>
    {props.sectionBefore?.[field.key] && <div class="sect">{props.sectionBefore[field.key]}</div>}
    <FieldRow field={field} model={props.model(field)} focused={index === focus} focus={() => setFocus(index)}
      change={value => props.change(field, value)} browse={() => props.browse(field)} restoreInherit={props.restoreInherit && (() => props.restoreInherit!(field))} />
    {props.after?.[field.key]}
  </Fragment>);
  return <div class={`cfg skin-${skin}`} style={props.tint ? shellStyle(props.tint) : {}}>
    <div class="cfg-bg glass-sky" aria-hidden="true"><div class={`art ${props.art ? '' : 'plain'}`} style={props.art ? { backgroundImage: `url("${props.art}")` } : {}} /><div class="fade" /><div class="grain" /></div>
    <header class="cfg-top">
      <Brand />
      <Back disabled={props.busy} onClick={props.back} />
      <span class="crumb">{props.crumb}</span>
      <span class="spacer" />
      <span class={`save-state ${props.saving ? 'on' : ''}`} role="status"><i />{props.saving ? t('正在保存…') : props.idle}</span>
      <button class="pill" disabled={props.busy || props.saving} onClick={props.reload}><Key><Glyph name="reload" /></Key>{t('重新载入')}</button>
      <button class="pill" disabled={props.busy || !props.loaded || !props.resetKeys.length} onClick={askReset}><Key>X</Key>{props.resetLabel}</button>
    </header>
    <aside class="cfg-side">
      {props.who}
      <div class="groups-h"><span class="sh">L</span>{props.groupsTitle}<span class="sh">R</span></div>
      <nav class="groups" aria-label={props.groupsTitle}>{groups.map(([key, label]) => {
        const count = fields.filter(field => field.group === key).length;
        return <button key={key} class={key === group ? 'on' : ''} aria-current={key === group ? 'page' : undefined} onClick={() => selectGroup(key)}>
          <GroupIcon name={key} />{t(label)}<span class="n">{count || ''}</span></button>;
      })}</nav>
      <div class="cfg-file">{props.fileLabel}<span title={props.filePath}><span dir="ltr">{props.filePath}</span></span></div>
    </aside>
    <main class="cfg-main" ref={main}>
      <div class="head"><div><div class="kicker"><i />{props.kicker}</div><h1>{t(title)}</h1></div>
        <div class="legend">{props.restoreInherit && <span><i class="l-inh" />{t('已单独设置')}</span>}{fields.some(field => fieldMeta(field).reserved) && <span><i class="l-pending" />{t('预留 · 仅保存')}</span>}</div></div>
      <div class="rows" key={group} aria-busy={props.busy}>
        {props.error && <Note icon="info" title={t('操作失败')} warn><pre role="alert">{props.error}</pre></Note>}
        {props.loaded ? <>{props.top}{rows}{props.end}</> : !props.error && <Note icon="info" title={props.busy ? t('正在读取设置…') : t('设置不可用')} />}
      </div>
    </main>
    <footer class="cfg-foot">
      <span class="hk"><Key>↑ ↓</Key>{t('选择')}</span><span class="hk"><Key>← →</Key>{t('调整')}</span><span class="hk"><Key>A</Key>{t('切换')}</span>
      <span class="hk"><Key>B</Key>{t('返回')}</span><span class="hk"><Key>X</Key>{props.resetLabel}</span>
      <span class="hk"><Key>L R</Key>{t('切换分组')}<kbd>Q E</kbd></span>
      <span class="note-text">{props.note}</span>
    </footer>
    <dialog ref={dialog} aria-label={props.resetLabel} onCancel={event => { event.preventDefault(); setConfirm(false); }}>
      <div class="kicker"><i />{props.resetLabel}</div>
      <h2>{props.resetTitle}</h2>
      <p>{t('确认后立即写入配置。')}</p>
      <ul>{fields.filter(field => props.resetKeys.includes(field.key)).map(field => <li key={field.key}>{fieldMeta(field).label}</li>)}</ul>
      <div class="acts"><button class="pill" autoFocus onClick={() => setConfirm(false)}><Key>B</Key>{t('取消')}</button>
        <button class="pill danger" onClick={confirmReset}><Key>A</Key>{props.resetLabel}</button></div>
    </dialog>
    {props.toast && <div class="toast" role="status"><i />{props.toast}</div>}
  </div>;
}
