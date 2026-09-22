import { expect, it } from 'vitest';
import { gameSettingsError, inheritSetting } from './game-settings';
import { isDirty, type SettingField } from './settings';
const fields: SettingField[] = [
  { key: 'supersample', initial: 1, min: 1, max: 4 },
  { key: 'mcp_enabled', initial: false }, { key: 'mcp_manual_step', initial: false },
].map(field => ({ choices: [], directory: false, label: field.key, group: '', pending: '', min: 0, max: 0, ...field }));
const saved = { fields, inherited: { supersample: 2, mcp_enabled: false, mcp_manual_step: false } };
it('uses inherited values for validation without persisting inherited keys', () => {
  expect(gameSettingsError(saved, {})).toBeNull();
  expect(gameSettingsError(saved, { supersample: 4 })).toBeNull();
  expect(gameSettingsError(saved, { supersample: 5 })).not.toBeNull();
  expect(gameSettingsError(saved, { unexpected: true })).not.toBeNull();
});
it('removes overrides while preserving false and zero for unrelated fields', () => {
  const draft = { supersample: 3, mute: false, volume: 0 };
  expect(inheritSetting(draft, 'supersample')).toEqual({ mute: false, volume: 0 });
  expect(draft.supersample).toBe(3);
  expect(isDirty({}, { mcp_enabled: false })).toBe(true);
  expect(isDirty({}, inheritSetting({ supersample: 3 }, 'supersample'))).toBe(false);
});
it('requires MCP for manual stepping using merged effective values', () => {
  expect(gameSettingsError(saved, { mcp_manual_step: true })).not.toBeNull();
  expect(gameSettingsError(saved, { mcp_manual_step: true, mcp_enabled: true })).toBeNull();
});
