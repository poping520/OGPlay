import { t } from './i18n';
export type SettingValue = boolean | number | string;
export interface SettingField {
  key: string; group: string; label: string; pending: string; initial: SettingValue;
  choices: string[]; min: number; max: number; directory: boolean;
}
export interface Settings {
  schema: number; revision: string; values: Record<string, SettingValue>; fields: SettingField[];
  library_root: string; config_path: string; facts: Record<string, string>;
}
export const groups = [
  ['general', '常规', '启动器自身的外观与交互习惯。'], ['storage', '目录与存储', '游戏库、Profile 与数据包所在位置。'],
  ['graphics', '图形', '新启动游戏的默认渲染选项。'], ['audio', '音频', '默认音频输出。'],
  ['input', '输入', '键位模板与手柄。游戏内按 F2 可打开按键设置。'], ['vm', '虚拟机', '解释器、时钟与默认虚拟设备。'],
  ['network', '网络', '游戏的默认网络意向。'], ['diagnostics', '诊断与日志', '日志、停滞快照与退出诊断。'],
  ['control', '控制面 (MCP)', '本机 MCP 服务与 Dashboard。'], ['about', '关于', '版本与随程序交付的制品清单。'],
] as const;
export function settingsError(fields: SettingField[], values: Record<string, SettingValue>): string | null {
  if (Object.keys(values).some(key => !fields.some(field => field.key === key))) return t('包含未知设置。');
  for (const field of fields) {
    const value = values[field.key], label = fieldMeta(field).label;
    if (typeof value !== typeof field.initial) return t('{label}：类型不正确。', { label });
    if (typeof value === 'number' && (!Number.isInteger(value) || value < field.min || value > field.max))
      return t('{label}：请输入 {min}–{max} 的整数。', { label, min: field.min, max: field.max });
    if (typeof value === 'string' && field.choices.length && !field.choices.includes(value)) return t('{label}：选项不支持。', { label });
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

// The host packs a unit or a short note into the label's trailing full-width brackets, and uses `pending` both for
// "预留 · …" (saved but not yet wired) and for plain explanations. The page shows them apart, each part translated
// from the host's Chinese text (GUI-14), so the host schema stays the single source of the fields.
export interface FieldMeta { label: string; unit: string; hint: string; reserved: string }
// Host marker for saved-only fields, matched on the host's (Chinese) text before translation.
export const RESERVED_MARK = '预留';
const reservedPrefix = new RegExp(`^${RESERVED_MARK}\\s*·\\s*`);
export function fieldMeta(field: SettingField): FieldMeta {
  const match = /^(.*?)（([^）]+)）$/.exec(field.label);
  const numeric = typeof field.initial === 'number';
  const reserved = field.pending.startsWith(RESERVED_MARK) ? field.pending.replace(reservedPrefix, '') : '';
  const notes = [match && !numeric ? match[2] : '', reserved ? '' : field.pending].filter(Boolean).map(note => t(note));
  return { label: t(match ? match[1] : field.label), unit: match && numeric ? t(match[2]) : '', hint: notes.join(t('；')), reserved: reserved && t(reserved) };
}
const labels: Record<string, string> = {
  dark: '深色', light: '浅色', system: '跟随系统', compact: '紧凑', comfortable: '舒适',
  'zh-CN': '简体中文', 'en-US': 'English', switch: 'Switch', threaded: 'Threaded',
  windowed: '窗口', fullscreen: '全屏', aspect: '保持比例', stretch: '拉伸', automatic: '自动', 'hardware-only': '仅硬件', 'software-only': '仅软件',
  disabled: '禁用', loopback: '仅回环', allow: '允许网络', realtime: '实时', fixed: '固定步进', portrait: '竖屏', landscape: '横屏', unlimited: '不限',
  '3d': '3D 模型', css: 'CSS 轻量', console: '主机之夜', glass: '玻璃紫',
};
export function choiceLabel(field: SettingField, value: string): string {
  return field.key === 'clock_rate' ? `${value}×` : value in labels ? t(labels[value]) : value;
}
export function displayValue(field: SettingField, value: SettingValue | undefined): string {
  if (typeof value === 'boolean') return value ? t('开') : t('关');
  if (value === undefined || value === '') return field.directory ? t('默认') : t('未设置');
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
    const step = fieldMeta(field).unit === '%' ? 5 : 1;
    return Math.min(field.max, Math.max(field.min, Math.round(current) + (d || 1) * step));
  }
  return value ?? field.initial;
}
