import { describe, expect, it } from 'vitest';
import { changedKeys, displayValue, fieldMeta, isDirty, settingsError, stepValue, type SettingField } from './settings';
const field = (key: string, initial: string | number | boolean, choices: string[] = []): SettingField =>
  ({ key, label: key, initial, choices, min: 1, max: 4, group: 'general', pending: '', directory: false });
describe('settings drafts', () => {
  const fields = [field('theme', 'dark', ['dark', 'light']), field('supersample', 1), field('enabled', false)];
  const values = { theme: 'dark', supersample: 1, enabled: false };
  it('rejects missing, unknown, wrong type, enum and non-finite numeric inputs', () => {
    expect(settingsError(fields, values)).toBeNull();
    for (const bad of [{ ...values, extra: true }, { ...values, theme: 'blue' }, { ...values, enabled: 'true' },
      { ...values, supersample: NaN }, { ...values, supersample: 1.5 }, { ...values, supersample: 5 }, {}])
      expect(settingsError(fields, bad)).not.toBeNull();
  });
  it('keeps edits dirty until the saved values match', () => {
    expect(isDirty(values, { ...values })).toBe(false);
    expect(isDirty(values, { ...values, enabled: true })).toBe(true);
    expect(isDirty(values, { ...values, supersample: NaN })).toBe(true);
    expect(isDirty(values, {})).toBe(true);
  });
  it('lists changed keys including ones present on one side only', () => {
    expect(changedKeys({ a: 1, b: true }, { a: 2, b: true, c: 'x' })).toEqual(['a', 'c']);
    expect(changedKeys({ a: 1 }, {})).toEqual(['a']);
  });
  it('splits units, notes and reserved markers out of host labels', () => {
    const jit = { ...field('jit_cache_mb', 64), label: 'JIT 缓存（MiB）', pending: '预留 · 运行时配置入口尚未接入' };
    expect(fieldMeta(jit)).toEqual({ label: 'JIT 缓存', unit: 'MiB', hint: '', reserved: '运行时配置入口尚未接入' });
    const cores = { ...field('cpu_cores', 1), label: '虚拟 CPU 核数', pending: '游戏查询到的核数；不限制宿主线程' };
    expect(fieldMeta(cores)).toEqual({ label: '虚拟 CPU 核数', unit: '', hint: '游戏查询到的核数；不限制宿主线程', reserved: '' });
    const sandbox = { ...field('ephemeral_sandbox', false), label: '临时沙盒（退出不保留本次存档）' };
    expect(fieldMeta(sandbox)).toEqual({ label: '临时沙盒', unit: '', hint: '退出不保留本次存档', reserved: '' });
    expect(displayValue({ ...jit }, 128)).toBe('128 MiB');
    expect(displayValue({ ...field('dir', ''), directory: true }, '')).toBe('默认');
  });
  it('steps booleans, choices and clamped numbers', () => {
    expect(stepValue(fields[2], false, 0)).toBe(true);
    expect(stepValue(fields[0], 'light', 1)).toBe('dark');
    expect(stepValue(fields[0], 'dark', -1)).toBe('light');
    expect(stepValue(fields[1], 4, 1)).toBe(4);
    expect(stepValue(fields[1], NaN, 1)).toBe(2);
    expect(stepValue({ ...field('volume', 100), label: '主音量（%）', max: 100 }, 50, -1)).toBe(45);
  });
});
