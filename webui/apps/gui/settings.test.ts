import { describe, expect, it } from 'vitest';
import { isDirty, resolvedTheme, settingsError, type SettingField } from './settings';
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
  it('resolves explicit and system themes', () => {
    expect(resolvedTheme('light', false)).toBe('light');
    expect(resolvedTheme('dark', true)).toBe('dark');
    expect(resolvedTheme('system', true)).toBe('light');
    expect(resolvedTheme('system', false)).toBe('dark');
  });
});
