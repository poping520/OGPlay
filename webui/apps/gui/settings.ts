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
  ['general', '常规'], ['storage', '目录与存储'], ['graphics', '图形'], ['audio', '音频'], ['input', '输入'],
  ['vm', '虚拟机'], ['network', '网络'], ['diagnostics', '诊断与日志'], ['control', '控制面 (MCP)'], ['about', '关于'],
] as const;
export function settingsError(fields: SettingField[], values: Record<string, SettingValue>): string | null {
  if (Object.keys(values).some(key => !fields.some(field => field.key === key))) return '包含未知设置。';
  for (const field of fields) {
    const value = values[field.key];
    if (typeof value !== typeof field.initial) return `${field.label}：类型不正确。`;
    if (typeof value === 'number' && (!Number.isInteger(value) || value < field.min || value > field.max))
      return `${field.label}：请输入 ${field.min}–${field.max} 的整数。`;
    if (typeof value === 'string' && field.choices.length && !field.choices.includes(value)) return `${field.label}：选项不支持。`;
  }
  return null;
}
export function isDirty(saved: Record<string, SettingValue>, draft: Record<string, SettingValue>): boolean {
  return Object.keys(saved).length !== Object.keys(draft).length || Object.keys(saved).some(key => saved[key] !== draft[key]);
}
export function resolvedTheme(value: SettingValue | undefined, systemLight: boolean): 'light' | 'dark' {
  return value === 'light' || value === 'system' && systemLight ? 'light' : 'dark';
}
