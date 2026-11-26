import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { createPortal } from 'preact/compat';
import { changedKeys, choiceLabel, fieldMeta, stepValue, shownFields, shownGroups, type SettingField, type SettingValue } from './settings';
import { shellStyle } from '../cartridge/cartridge';
import { t } from '../shared/i18n';
import { Back, Brand, type Skin } from '../kit';
import { Glyph } from '../../../packages/ui-kit/glyph';
import { Key } from '../../../packages/ui-kit/key';
import { SETTING_ICONS } from './setting-icons';
import { BackButton, NextIcon, Sheet, SheetItem } from '../phone/parts';
import './settings.css';

// Settings pages ('setting.choice.console' layout): top bar with save state, group sidebar, field rows, gamepad legend. Edits are
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
  if (field.choices.length && __OGPLAY_PHONE__) return <ChoicePicker field={field} value={value} disabled={disabled} change={change} />;
  if (field.choices.length) return <Select field={field} value={value} disabled={disabled} change={change} />;
  if (typeof field.initial === 'number') {
    const number = typed.shown.trim() ? Number(typed.shown) : NaN;
    if (field.unit === 'percent') {
      const percent = Number.isFinite(number) ? (number - field.min) / (field.max - field.min) * 100 : 0;
      return <div class="range"><input type="range" min={field.min} max={field.max} step={1} value={Number.isFinite(number) ? number : field.min} disabled={disabled}
        aria-label={meta.label} style={{ '--p': `${percent}%` }} {...typed.events} /><b>{Number.isFinite(number) ? number : '—'}%</b></div>;
    }
    return <div class="step">
      <button aria-label={t('settings.decrease')} disabled={disabled} onClick={() => change(stepValue(field, value, -1))}>−</button>
      <input type="number" min={field.min} max={field.max} step={1} value={Number.isNaN(number) ? '' : typed.shown} disabled={disabled} aria-label={meta.label}
        {...typed.events} />
      {meta.unit && <small>{meta.unit}</small>}
      <button aria-label={t('settings.increase')} disabled={disabled} onClick={() => change(stepValue(field, value, 1))}>+</button>
    </div>;
  }
  return <div class="txt"><input type="text" value={typed.shown} disabled={disabled} aria-label={meta.label}
    placeholder={field.directory ? t('settings.placeholder.default') : field.reserved ? t('settings.placeholder.devicePreset') : t('common.notSet')} {...typed.events} />
    {browse && <button disabled={disabled} onClick={browse}>{t('settings.browse')}</button>}</div>;
}

// Longer choice lists: the value with a chevron, opening the whole list below it (above when there is no room).
// The list lives on the page root so neither the scrolling column nor the glass rows' backdrop filter clip it.
// While open it owns the keyboard (and the gamepad, which arrives as keys): ↑/↓ move, A/Enter choose, B/Esc close.
interface Placement { left: number; width: number; top?: number; bottom?: number; maxHeight: number }
function Select({ field, value, disabled, change }: { field: SettingField; value: SettingValue; disabled: boolean; change: (value: SettingValue) => void }) {
  const [open, setOpen] = useState(false);
  const [active, setActiveState] = useState(0);
  const activeRef = useRef(0);
  const setActive = (index: number) => { activeRef.current = index; setActiveState(index); };
  const [place, setPlace] = useState<Placement | null>(null);
  const trigger = useRef<HTMLButtonElement>(null), list = useRef<HTMLDivElement>(null);
  const current = String(value), at = field.choices.indexOf(current), label = fieldMeta(field).label;
  const close = () => { setOpen(false); setPlace(null); };
  const choose = (choice: string) => { close(); if (choice !== current) change(choice); };
  useLayoutEffect(() => {
    if (!open) return;
    const measure = () => {
      const box = trigger.current?.getBoundingClientRect();
      if (!box) return;
      const gap = 6, margin = 12, width = Math.max(box.width, 240);
      const room = { below: innerHeight - box.bottom - gap - margin, above: box.top - gap - margin };
      const wanted = Math.min(field.choices.length * 40 + 12, 380);
      const up = room.below < wanted && room.above > room.below;
      const left = Math.min(Math.max(margin, box.right - width), innerWidth - width - margin);
      setPlace(up ? { left, width, bottom: innerHeight - box.top + gap, maxHeight: Math.min(wanted, room.above) }
        : { left, width, top: box.bottom + gap, maxHeight: Math.min(wanted, room.below) });
    };
    measure();
    const outside = (event: PointerEvent) => {
      // A click on the list's own row toggles it through the row (activateRow), so the row is not "outside".
      const target = event.target as Node, row = trigger.current?.closest('[data-row]');
      if (!(row ?? trigger.current)?.contains(target) && !list.current?.contains(target)) close();
    };
    const keys = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      const last = field.choices.length - 1;
      const move: Record<string, number> = { ArrowDown: 1, ArrowUp: -1, ArrowRight: 1, ArrowLeft: -1 };
      if (event.key in move) setActive(Math.min(last, Math.max(0, activeRef.current + move[event.key])));
      else if (event.key === 'Home' || event.key === 'End') setActive(event.key === 'Home' ? 0 : last);
      else if (event.key === 'Enter' || event.key === 'a' || event.key === ' ') choose(field.choices[activeRef.current]);
      else if (event.key === 'Escape' || event.key === 'b' || event.key === 'Tab') close();
      else return;
      // The page listens on window too: keep its row navigation and back key out of the open list.
      event.preventDefault(); event.stopPropagation();
    };
    document.addEventListener('pointerdown', outside, true);
    window.addEventListener('keydown', keys, true);
    window.addEventListener('resize', close);
    // Scrolling the page would leave the list behind its value; the list's own scrolling keeps it.
    const scrolled = (event: Event) => { if (!list.current?.contains(event.target as Node)) close(); };
    document.addEventListener('scroll', scrolled, true);
    return () => {
      document.removeEventListener('pointerdown', outside, true);
      window.removeEventListener('keydown', keys, true);
      window.removeEventListener('resize', close);
      document.removeEventListener('scroll', scrolled, true);
    };
  }, [open]);
  useEffect(() => { if (open) list.current?.querySelector(`[data-index="${active}"]`)?.scrollIntoView({ block: 'nearest' }); }, [open, active, place]);
  const root = trigger.current?.closest('.cfg');
  return <>
    <button ref={trigger} class={`sel ${open ? 'open' : ''}`} aria-haspopup="listbox" aria-expanded={open} aria-label={label} disabled={disabled}
      onClick={() => { if (open) close(); else { setActive(Math.max(0, at)); setOpen(true); } }}>
      <span>{choiceLabel(field, current)}</span><Glyph name="down" />
    </button>
    {open && place && root && createPortal(
      <div ref={list} class={`sel-pop ${place.bottom !== undefined ? 'up' : ''}`} role="listbox" aria-label={label}
        style={{ left: `${place.left}px`, width: `${place.width}px`, maxHeight: `${place.maxHeight}px`,
          ...(place.top !== undefined ? { top: `${place.top}px` } : { bottom: `${place.bottom}px` }) }}>
        {field.choices.map((choice, index) =>
          <button key={choice} data-index={index} role="option" aria-selected={choice === current}
            class={`${choice === current ? 'on' : ''} ${index === active ? 'active' : ''}`}
            onPointerMove={() => setActive(index)} onClick={() => choose(choice)}>
            <span>{choiceLabel(field, choice)}</span>{choice === current && <Glyph name="check" />}
          </button>)}
      </div>, root)}
  </>;
}

// Phone: longer choice lists open a bottom sheet instead of stepping through them one at a time.
function ChoicePicker({ field, value, disabled, change }: { field: SettingField; value: SettingValue; disabled: boolean; change: (value: SettingValue) => void }) {
  const [open, setOpen] = useState(false);
  const label = fieldMeta(field).label;
  return <>
    <button class="pick" aria-haspopup="dialog" aria-label={label} disabled={disabled} onClick={() => setOpen(true)}>
      <span>{choiceLabel(field, String(value))}</span><Glyph name="down" /></button>
    {open && <Sheet title={label} onClose={() => setOpen(false)}>{field.choices.map(choice =>
      <SheetItem key={choice} label={choiceLabel(field, choice)} checked={choice === String(value)} onClick={() => { setOpen(false); change(choice); }} />)}</Sheet>}
  </>;
}

// What a page tells the frame about one row.
export interface RowModel {
  value: SettingValue; disabled: boolean;
  inherit?: { overridden: boolean; shown: string };
  confirmed?: boolean;                           // a wrong value stops the game: edits wait for the row's save button
}
// Per-game rows never lock: editing one makes it this game's own value (outlined, 'settings.customized'), and the restore
// button beside the control drops it back to the inherited one.
// A row's main action, the same for A/Enter and for a click on the row outside its controls: a choice list opens,
// a switch flips, other choices step, and text or numbers take the keyboard. A click leaves segmented choices and
// sliders alone: their own buttons and track are the target.
function activateRow(row: Element | null | undefined, field: SettingField, model: RowModel, change: (value: SettingValue) => void,
  pointer = false) {
  if (!row || model.disabled) return;
  const list = row.querySelector<HTMLButtonElement>('.sel, .pick');
  if (list) list.click();
  else if (pointer && row.querySelector('.seg, .range')) return;
  else if (typeof field.initial === 'boolean' || field.choices.length) change(stepValue(field, model.value, 0));
  else row.querySelector<HTMLElement>(pointer ? 'input:not([type=range]), textarea' : 'input, textarea')?.focus();
}
// Clicks that land on a control (or a link in a note) are the control's own.
const onControl = (event: MouseEvent) => !!(event.target as Element).closest('button, input, textarea, select, a, label, .seg, .range, .step, .txt, .path-edit');
const rowClick = (field: SettingField, model: RowModel, change: (value: SettingValue) => void) => (event: MouseEvent) => {
  if (!onControl(event)) activateRow(event.currentTarget as Element, field, model, change, true);
};

function FieldRow({ field, model, focused, focus, change, browse, restoreInherit }: {
  field: SettingField; model: RowModel; focused: boolean; focus: () => void;
  change: (value: SettingValue) => void; browse?: () => void; restoreInherit?: () => void;
}) {
  const meta = fieldMeta(field), inherit = model.inherit, overridden = !!inherit?.overridden;
  return <div class={`row ${focused ? 'focus' : ''} ${overridden ? 'override' : ''}`} data-row onPointerDown={focus}
    onClick={rowClick(field, model, change)}>
    <GroupIcon name={field.group} glyph={SETTING_ICONS[field.key]} />
    <div class="lbl"><b>{meta.label}</b>
      {(inherit || meta.reserved || meta.hint) && <div class="meta">
        {inherit && !overridden && <span class="inherit-val">{t('settings.inherit.source')} · {inherit.shown}</span>}
        {inherit && overridden && <><span class="tag over">{t('settings.customized')}</span>
          <span class="inherit-val">{t('settings.inherit.globalValue', { value: inherit.shown })}</span></>}
        {meta.reserved && <span class="tag pending" title={meta.reserved}>{t('settings.reservedLegend')}</span>}
        {(meta.hint || meta.reserved) && <span>{meta.hint || meta.reserved}</span>}
      </div>}
    </div>
    <div class="ctl">
      {inherit && overridden &&
        <button class="restore" disabled={model.disabled} title={t('settings.inherit.restoreGlobal')} onClick={restoreInherit}>
          <Glyph name="reload" />{t('settings.inherit.restoreGlobal')}</button>}
      <Control field={field} value={model.value} disabled={model.disabled} change={change} browse={field.directory ? browse : undefined} />
    </div>
  </div>;
}

// A path that must be right: the field grows to show all of it, and an edit, typed or browsed, stays in the row
// until saved. A rejected save stays there too, to be corrected.
function ConfirmedPathRow({ field, model, saving, focused, focus, change, browse }: {
  field: SettingField; model: RowModel; saving: boolean; focused: boolean; focus: () => void;
  change: (value: SettingValue) => void; browse: (apply: (path: string) => void) => void;
}) {
  const meta = fieldMeta(field), value = String(model.value);
  const [text, setText] = useState<string | null>(null);
  const area = useRef<HTMLTextAreaElement>(null);
  const shown = text ?? value, dirty = shown !== value;
  const fit = () => { const el = area.current; if (el) { el.style.height = 'auto'; el.style.height = `${el.scrollHeight}px`; } };
  useLayoutEffect(fit, [shown]);
  useEffect(() => { const observer = new ResizeObserver(fit); if (area.current) observer.observe(area.current); return () => observer.disconnect(); }, []);
  // The page shows a write's value before it lands: only once it has can the edit be let go.
  useEffect(() => { if (!saving && text === value) setText(null); }, [text, value, saving]);
  const save = () => { if (dirty && !model.disabled) change(shown); };
  return <div class={`row path-row ${focused ? 'focus' : ''}`} data-row onPointerDown={focus} onClick={rowClick(field, model, change)}>
    <GroupIcon name={field.group} glyph={SETTING_ICONS[field.key]} />
    <div class="lbl"><b>{meta.label}</b>{dirty && <span class="tag pending">{t('import.unsaved')}</span>}
      {meta.hint && <div class="meta"><span>{meta.hint}</span></div>}
    </div>
    <div class="path-edit">
      <div class="path-field">
        <textarea ref={area} rows={1} value={shown} disabled={model.disabled} aria-label={meta.label} placeholder={t('common.notSet')} spellcheck={false}
          onInput={event => setText(event.currentTarget.value.replace(/[\r\n]+/g, ''))}
          onKeyDown={event => { if (event.key === 'Enter') { event.preventDefault(); save(); } else if (event.key === 'Escape') setText(null); }} />
        {field.directory && <button disabled={model.disabled} onClick={() => browse(setText)}>{t('settings.browse')}</button>}
      </div>
      {dirty && <div class="path-acts">
        <button disabled={model.disabled} onClick={() => setText(null)}>{t('common.cancel')}</button>
        <button class="save" disabled={model.disabled} onClick={save}>{t('common.save')}</button>
      </div>}
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
  browse: (field: SettingField, apply?: (path: string) => void) => void;   // apply: where the path goes instead of the draft
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

// Console pages lay groups beside their rows; the phone shows the group list first and one group per page.
export function SettingsFrame(props: FrameProps) {
  return __OGPLAY_PHONE__ ? <PhoneFrame {...props} /> : <ConsoleFrame {...props} />;
}

function ConsoleFrame(props: FrameProps) {
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
        activateRow(main.current?.querySelectorAll('[data-row]')[focus], field, props.model(field), value => props.change(field, value));
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
    {props.model(field).confirmed
      ? <ConfirmedPathRow field={field} model={props.model(field)} saving={props.saving} focused={index === focus} focus={() => setFocus(index)}
          change={value => props.change(field, value)} browse={apply => props.browse(field, apply)} />
      : <FieldRow field={field} model={props.model(field)} focused={index === focus} focus={() => setFocus(index)}
          change={value => props.change(field, value)} browse={() => props.browse(field)} restoreInherit={props.restoreInherit && (() => props.restoreInherit!(field))} />}
    {props.after?.[field.key]}
  </Fragment>);
  return <div class={`cfg skin-${skin}`} style={props.tint ? shellStyle(props.tint) : {}}>
    <div class="cfg-bg glass-sky" aria-hidden="true"><div class={`art ${props.art ? '' : 'plain'}`} style={props.art ? { backgroundImage: `url("${props.art}")` } : {}} /><div class="fade" /><div class="grain" /></div>
    <header class="cfg-top">
      <Brand />
      <Back disabled={props.busy} onClick={props.back} />
      <span class="crumb">{props.crumb}</span>
      <span class="spacer" />
      <span class={`save-state ${props.saving ? 'on' : ''}`} role="status"><i />{props.saving ? t('settings.saving') : props.idle}</span>
      <button class="pill" disabled={props.busy || props.saving} onClick={props.reload}><Key><Glyph name="reload" /></Key>{t('settings.reload')}</button>
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
        <div class="legend">{props.restoreInherit && <span><i class="l-inh" />{t('settings.customized')}</span>}{fields.some(field => fieldMeta(field).reserved) && <span><i class="l-pending" />{t('settings.reservedLegend')}</span>}</div></div>
      <div class="rows" key={group} aria-busy={props.busy}>
        {props.error && <Note icon="info" title={t('settings.actionFailed')} warn><pre role="alert">{props.error}</pre></Note>}
        {props.loaded ? <>{props.top}{rows}{props.end}</> : !props.error && <Note icon="info" title={props.busy ? t('settings.loading') : t('settings.unavailable')} />}
      </div>
    </main>
    <footer class="cfg-foot">
      <span class="hk"><Key>↑ ↓</Key>{t('common.select')}</span><span class="hk"><Key>← →</Key>{t('settings.adjust')}</span><span class="hk"><Key>A</Key>{t('settings.toggle')}</span>
      <span class="hk"><Key>B</Key>{t('common.back')}</span><span class="hk"><Key>X</Key>{props.resetLabel}</span>
      <span class="hk"><Key>L R</Key>{t('settings.switchGroup')}<kbd>Q E</kbd></span>
      <span class="note-text">{props.note}</span>
    </footer>
    <dialog ref={dialog} aria-label={props.resetLabel} onCancel={event => { event.preventDefault(); setConfirm(false); }}>
      <div class="kicker"><i />{props.resetLabel}</div>
      <h2>{props.resetTitle}</h2>
      <p>{t('settings.confirmWrites')}</p>
      <ul>{fields.filter(field => props.resetKeys.includes(field.key)).map(field => <li key={field.key}>{fieldMeta(field).label}</li>)}</ul>
      <div class="acts"><button class="pill" autoFocus onClick={() => setConfirm(false)}><Key>B</Key>{t('common.cancel')}</button>
        <button class="pill danger" onClick={confirmReset}><Key>A</Key>{props.resetLabel}</button></div>
    </dialog>
    {props.toast && <div class="toast" role="status"><i />{props.toast}</div>}
  </div>;
}

// Phone settings: a list of groups (with the game card, reload and reset on a per-game page), each opening its rows
// on a page of its own; back returns from a group to the list, then leaves. Rows, controls, saving and the reset
// confirmation are the console frame's.
function PhoneFrame(props: FrameProps) {
  const { skin, group } = props;
  const fields = shownFields(props.fields, props.reservedVisible);
  const groups = props.loaded ? shownGroups(props.groups, props.fields, fields) : [];
  const [open, setOpen] = useState(false);
  const [confirm, setConfirm] = useState(false);
  const main = useRef<HTMLElement>(null);
  const dialog = useRef<HTMLDialogElement>(null);
  const listScroll = useRef(0);
  const visible = fields.filter(field => field.group === group);
  const [, title] = props.groups.find(([key]) => key === group) ?? props.groups[0];
  const askReset = () => { if (!props.busy && props.loaded && props.resetKeys.length) setConfirm(true); };
  const confirmReset = () => { setConfirm(false); props.reset(); };
  const openGroup = (key: string) => {
    listScroll.current = main.current?.scrollTop ?? 0;
    props.setGroup(key); setOpen(true); main.current?.scrollTo({ top: 0 });
  };
  const back = () => {
    if (!open) { props.back(); return; }
    setOpen(false);
    requestAnimationFrame(() => main.current?.scrollTo({ top: listScroll.current }));
  };
  useEffect(() => { if (confirm) dialog.current?.showModal(); else dialog.current?.close(); }, [confirm]);
  // The back key arrives as Esc (phone-app.tsx); a focused field only gives up its keyboard.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (event.key !== 'Escape' || confirm) return;
      if (editable(event.target)) { (event.target as HTMLElement).blur(); return; }
      event.preventDefault(); back();
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });
  const rows = visible.map(field => <Fragment key={field.key}>
    {props.sectionBefore?.[field.key] && <div class="sect">{props.sectionBefore[field.key]}</div>}
    {props.model(field).confirmed
      ? <ConfirmedPathRow field={field} model={props.model(field)} saving={props.saving} focused={false} focus={() => {}}
          change={value => props.change(field, value)} browse={apply => props.browse(field, apply)} />
      : <FieldRow field={field} model={props.model(field)} focused={false} focus={() => {}}
          change={value => props.change(field, value)} browse={() => props.browse(field)} restoreInherit={props.restoreInherit && (() => props.restoreInherit!(field))} />}
    {props.after?.[field.key]}
  </Fragment>);
  return <div class={`cfg cfg-phone skin-${skin}`} style={props.tint ? shellStyle(props.tint) : {}}>
    <div class="cfg-bg glass-sky" aria-hidden="true"><div class={`art ${props.art ? '' : 'plain'}`} style={props.art ? { backgroundImage: `url("${props.art}")` } : {}} /><div class="fade" /><div class="grain" /></div>
    <header class="ph-bar">
      <BackButton disabled={props.busy} onClick={back} />
      <h1>{open ? t(title) : props.groupsTitle}</h1>
      <span class={`save-state ${props.saving ? 'on' : ''}`} role="status"><i />{props.saving ? t('settings.saving') : ''}</span>
    </header>
    <main class="cfg-main" ref={main}>
      {props.error && <Note icon="info" title={t('settings.actionFailed')} warn><pre role="alert">{props.error}</pre></Note>}
      {!props.loaded ? !props.error && <Note icon="info" title={props.busy ? t('settings.loading') : t('settings.unavailable')} /> :
        open ? <div class="rows" key={group} aria-busy={props.busy}>{props.top}{rows}{props.end}</div> : <>
          {props.who}
          <p class="cfg-idle">{props.idle}</p>
          <nav class="groups" aria-label={props.groupsTitle}>{groups.map(([key, label]) => {
            const count = fields.filter(field => field.group === key).length;
            return <button key={key} onClick={() => openGroup(key)}><GroupIcon name={key} />{t(label)}<span class="n">{count || ''}</span><NextIcon /></button>;
          })}</nav>
          <div class="cfg-acts">
            <button disabled={props.busy || props.saving} onClick={props.reload}><Glyph name="reload" />{t('settings.reload')}</button>
            <button class="danger" disabled={props.busy || !props.resetKeys.length} onClick={askReset}>{props.resetLabel}</button>
          </div>
          <div class="cfg-file">{props.fileLabel}<span dir="ltr">{props.filePath}</span></div>
        </>}
    </main>
    <dialog ref={dialog} class="ph-sheet" aria-label={props.resetLabel} onCancel={event => { event.preventDefault(); setConfirm(false); }}>
      <i class="ph-grip" aria-hidden="true" />
      <h2>{props.resetTitle}</h2>
      <p>{t('settings.confirmWrites')}</p>
      <ul>{fields.filter(field => props.resetKeys.includes(field.key)).map(field => <li key={field.key}>{fieldMeta(field).label}</li>)}</ul>
      <div class="modal-actions"><button onClick={() => setConfirm(false)}>{t('common.cancel')}</button>
        <button class="danger" onClick={confirmReset}>{props.resetLabel}</button></div>
    </dialog>
    {props.toast && <div class="toast" role="status"><i />{props.toast}</div>}
  </div>;
}
