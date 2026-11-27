import { has, isLanguage, LANGUAGE_NAMES, t, type Language } from '../shared/i18n';
import { followScreenLabel } from './screen-size';
export type SettingValue = boolean | number | string;
export interface SettingField {
  key: string; group: string; initial: SettingValue;
  choices: string[]; min: number; max: number; directory: boolean;
  unit: string;                                  // setting.unit.<unit>, empty for none
  reserved: string;                              // setting.reserved.<reserved> when saved only, else empty
  global_default: boolean;                       // read as "Default …" on the global page
  hidden: boolean;                               // kept off the page by the host until it is polished
}
export interface Settings {
  system_language: Language;
  schema: number; revision: string; values: Record<string, SettingValue>; fields: SettingField[];
  library_root: string; config_path: string; facts: Record<string, string | null>;
  reserved_visible: boolean;                     // false in release builds
}
// Same priority as the per-game page: graphics before storage, input before audio.
export const groups = [
  ['general', 'settings.group.general'], ['graphics', 'settings.group.graphics'], ['storage', 'settings.group.storage'], ['input', 'settings.group.input'], ['audio', 'settings.group.audio'],
  ['vm', 'settings.group.vm'], ['network', 'settings.group.network'], ['diagnostics', 'settings.group.diagnostics'], ['control', 'settings.group.control'], ['about', 'settings.group.about'],
] as const;
// Host facts arrive under stable ids with data values (versions, file text, paths, codes); the labels and
// coded values are worded here, in this order. null is a file the host could not read.
const factLabels: Record<string, string> = {
  android_manifest: 'settings.facts.manifest', artifacts_error: 'settings.facts.artifactsError',
  profile_status: 'settings.facts.profileStatus', profiles_dir: 'settings.facts.profilesDir',
  profile_error: 'settings.facts.profileError',
};
const factValues: Record<string, Record<string, string>> = {
  profile_status: { recorded: 'settings.facts.profileRecorded', missing: 'library.condition.profileMissing' },
};
export function factRows(facts: Record<string, string | null>): [string, string][] {
  const order = Object.keys(factLabels);
  const rank = (key: string) => order.includes(key) ? order.indexOf(key) : order.length;
  return Object.entries(facts).sort(([a], [b]) => rank(a) - rank(b)).map(([key, value]) => {
    const coded = value === null ? undefined : factValues[key]?.[value];
    const shown = value === null ? t('settings.facts.unreadable') : coded ? t(coded) : value;
    return [factLabels[key] ? t(factLabels[key]) : key, shown];
  });
}
export function settingsError(fields: SettingField[], values: Record<string, SettingValue>): string | null {
  if (Object.keys(values).some(key => !fields.some(field => field.key === key))) return t('settings.error.unknown');
  for (const field of fields) {
    const value = values[field.key], label = fieldMeta(field).label;
    if (typeof value !== typeof field.initial) return t('settings.error.type', { label });
    if (typeof value === 'number' && (!Number.isInteger(value) || value < field.min || value > field.max))
      return t('settings.error.range', { label, min: field.min, max: field.max });
    if (typeof value === 'string' && field.choices.length && !field.choices.includes(value)) return t('settings.error.choice', { label });
  }
  return null;
}
export function isDirty(saved: Record<string, SettingValue>, draft: Record<string, SettingValue>): boolean {
  return Object.keys(saved).length !== Object.keys(draft).length || Object.keys(saved).some(key => saved[key] !== draft[key]);
}
// Keys whose value differs between the saved and the draft set; a key present in only one of them counts too.
export function changedKeys(saved: Record<string, SettingValue>, draft: Record<string, SettingValue>): string[] {
  return [...new Set([...Object.keys(saved), ...Object.keys(draft)])].filter(key => saved[key] !== draft[key]);
}

// The host schema carries no wording: each field is worded here from its key (setting.<key>.label, .defaultLabel on
// the global page for a per-game value's default, .hint), with the unit and the reason a saved-only field is not
// wired yet (reserved) as codes.
export interface FieldMeta { label: string; unit: string; hint: string; reserved: string }
export function fieldMeta(field: SettingField): FieldMeta {
  const id = `setting.${field.key}`;
  return {
    label: t(field.global_default && has(`${id}.defaultLabel`) ? `${id}.defaultLabel` : `${id}.label`),
    unit: field.unit ? t(`setting.unit.${field.unit}`) : '',
    hint: has(`${id}.hint`) ? t(`${id}.hint`) : '',
    reserved: field.reserved ? t(`setting.reserved.${field.reserved}`) : '',
  };
}
const labels: Record<string, string> = {
  ui: 'setting.choice.ui',
  dark: 'setting.choice.dark', light: 'setting.choice.light', system: 'setting.choice.system', compact: 'setting.choice.compact', comfortable: 'setting.choice.comfortable',
  switch: 'Switch', threaded: 'Threaded',
  windowed: 'setting.choice.windowed', fullscreen: 'setting.choice.fullscreen', aspect: 'setting.choice.aspect', stretch: 'setting.choice.stretch', automatic: 'setting.choice.automatic', 'hardware-only': 'setting.choice.hardwareOnly', 'software-only': 'setting.choice.softwareOnly',
  disabled: 'setting.choice.disabled', loopback: 'setting.choice.loopback', allow: 'setting.choice.allow', host: 'setting.choice.host', guest: 'setting.choice.guest', realtime: 'setting.choice.realtime', fixed: 'setting.choice.fixed', portrait: 'setting.choice.portrait', landscape: 'setting.choice.landscape', unlimited: 'setting.choice.unlimited',
  '3d': 'setting.choice.3d', css: 'setting.choice.css', console: 'setting.choice.console', glass: 'setting.choice.glass', cradle: 'setting.choice.cradle', badge: 'setting.choice.badge',
  'top-left': 'setting.choice.topLeft', 'top-right': 'setting.choice.topRight', 'bottom-left': 'setting.choice.bottomLeft', 'bottom-right': 'setting.choice.bottomRight',
  licensed: 'setting.choice.licensed', 'not-licensed': 'setting.choice.notLicensed', 'error-contacting-server': 'setting.choice.errorContactingServer',
};
// The FPS corner only matters while the counter is shown, the license answer only while Google Play is on.
export function overlayFields(fields: SettingField[], values: Record<string, SettingValue>): SettingField[] {
  return fields.filter(field =>
    (field.key !== 'fps_overlay_position' || values.fps_overlay === true) &&
    (field.key !== 'google_play_license' || values.google_play !== false));
}
// Hidden rows never show; release builds (host reserved_visible false) also leave reserved rows out, and with
// them the groups that only had such rows; groups without any field (e.g. 关于) stay.
export function shownFields(fields: SettingField[], reservedVisible: boolean): SettingField[] {
  return fields.filter(field => !field.hidden && (reservedVisible || !fieldMeta(field).reserved));
}
export function shownGroups<G extends readonly [string, ...string[]]>(groups: readonly G[], fields: SettingField[], shown: SettingField[]): G[] {
  return groups.filter(([key]) => shown.some(field => field.group === key) || !fields.some(field => field.group === key));
}
export function choiceLabel(field: SettingField, value: string): string {
  if (field.key === 'clock_rate') return `${value}×`;
  if (field.key === 'max_fps' && value !== 'unlimited') return `${value} FPS`;
  if (field.key === 'screen_resolution') return value === 'custom' ? t('settings.screen.custom') : value === '1440p' ? '1440p (2K)' : value === '2160p' ? '2160p (4K)' : value;
  if (field.key === 'screen_aspect') return value === 'screen' ? followScreenLabel() : value;
  if (isLanguage(value)) return LANGUAGE_NAMES[value];
  return value in labels ? t(labels[value]) : value;
}
export function displayValue(field: SettingField, value: SettingValue | undefined): string {
  if (typeof value === 'boolean') return value ? t('setting.choice.on') : t('setting.choice.off');
  if (value === undefined || value === '') return field.directory ? t('setting.choice.default') : t('common.notSet');
  if (typeof value === 'number') { const unit = fieldMeta(field).unit; return unit ? `${value} ${unit}` : String(value); }
  return field.choices.length ? choiceLabel(field, value) : value;
}
// Keyboard and stepper adjustment: d = 0 toggles or advances, ±1 moves one step and clamps.
export function stepValue(field: SettingField, value: SettingValue | undefined, d: number): SettingValue {
  if (typeof field.initial === 'boolean') return d === 0 ? value !== true : d > 0;
  if (field.choices.length) {
    const at = Math.max(0, field.choices.indexOf(String(value)));
    return field.choices[(at + (d || 1) + field.choices.length) % field.choices.length];
  }
  if (typeof field.initial === 'number') {
    const current = typeof value === 'number' && Number.isFinite(value) ? value : field.min;
    const step = field.unit === 'percent' ? 5 : 1;
    return Math.min(field.max, Math.max(field.min, Math.round(current) + (d || 1) * step));
  }
  return value ?? field.initial;
}
