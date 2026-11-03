import { describe, expect, it } from 'vitest';
import { changedKeys, displayValue, factRows, fieldMeta, groups, isDirty, settingsError, shownFields, shownGroups, stepValue, type SettingField } from './settings';
import { gameGroups } from './game-settings';
import { SETTING_ICONS, SHARED_ICONS } from './setting-icons';
const field = (key: string, initial: string | number | boolean, choices: string[] = []): SettingField =>
  ({ key, label: key, initial, choices, min: 1, max: 4, group: 'general', pending: '', directory: false, hidden: false });
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
    const fps = { ...field('max_fps', 'unlimited'), choices: ['30', '60', 'unlimited'] };
    expect(displayValue(fps, '60')).toBe('60 FPS');
    expect(displayValue(fps, 'unlimited')).toBe('不限');
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

// Host field definitions: `{"key", "group", "label", ...}` rows (one line or one field per line, as clang-format
// leaves them) plus the per-game device identity `{key, label}` list.
const hostSettings = Object.values(import.meta.glob(['../../../../src/frontend/library/settings.cpp', '../../../../src/frontend/library/game_settings.cpp'],
  { query: '?raw', import: 'default', eager: true })) as string[];
describe('reserved settings in release builds', () => {
  it('drops reserved rows and the groups they leave empty, keeping groups that never had fields', () => {
    const reserved = { ...field('volume', 100), group: 'audio', pending: '预留 · 运行时未实现' };
    const live = { ...field('supersample', 1), group: 'graphics', pending: '说明文字' };
    const all = [reserved, live];
    expect(shownFields(all, true)).toEqual(all);
    const shown = shownFields(all, false);
    expect(shown).toEqual([live]);
    const pageGroups = [['graphics', '图形', ''], ['audio', '音频', ''], ['about', '关于', '']] as const;
    expect(shownGroups(pageGroups, all, shown).map(([key]) => key)).toEqual(['graphics', 'about']);
    expect(shownGroups(pageGroups, all, all).map(([key]) => key)).toEqual(['graphics', 'audio', 'about']);
  });
  it('never shows rows the host hides, in any build', () => {
    const hidden = { ...field('cartridge', '3d'), hidden: true };
    const live = field('theme', 'dark');
    expect(shownFields([hidden, live], true)).toEqual([live]);
    expect(shownFields([hidden, live], false)).toEqual([live]);
  });
});

describe('setting row icons', () => {
  it('give every host setting its own glyph, repeated only for the same concept', () => {
    const groupKeys = new Set<string>([...groups, ...gameGroups].map(([key]) => key));
    const keys = new Set<string>();
    for (const text of hostSettings) {
      for (const [, key, group] of text.matchAll(/\{\s*"([a-z0-9_]+)",\s*"([a-z]+)",\s*"/g)) if (groupKeys.has(group)) keys.add(key);
      for (const [, key] of text.match(/\[key, label\][\s\S]*?\}\)/)?.[0].matchAll(/\{\s*"([a-z0-9_]+)",\s*"/g) ?? []) keys.add(key);
    }
    expect(hostSettings.length).toBe(2);
    expect(keys.size).toBeGreaterThan(60);
    expect([...keys].filter(key => !SETTING_ICONS[key])).toEqual([]);
    const shared = new Set(SHARED_ICONS.map(pair => pair.join()));
    const repeats = Object.entries(SETTING_ICONS).flatMap(([key, glyph], index, all) =>
      all.slice(index + 1).filter(([, other]) => other === glyph).map(([other]) => [key, other].join()));
    expect(repeats.filter(pair => !shared.has(pair))).toEqual([]);
  });
});

describe('host facts', () => {
  it('labels stable ids, words coded values and keeps data as is', () => {
    expect(factRows({ profile_quirks: '', android_manifest: null, profile_status: 'missing', custom: 'x', artifacts_error: 'boom' }))
      .toEqual([['Android / BootDex / guest JNI 制品清单', '文件不可用或超过读取上限'], ['制品信息不可用', 'boom'],
        ['Profile 状态', '记录的 Profile 在所选目录中不存在。'], ['Profile quirk 列表', '无'], ['custom', 'x']]);
  });
});
