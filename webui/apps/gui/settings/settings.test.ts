import { describe, expect, it } from 'vitest';
import { changedKeys, displayValue, factRows, fieldMeta, globalOnlyInSharedGroup, groups, isDirty, isLauncherGroup, overlayFields, settingsError, shownFields, shownGroups, stepValue, type SettingField } from './settings';
import { gameGroups } from './game-settings';
import { SETTING_ICONS, SHARED_ICONS } from './setting-icons';
import { creditSummary, creditTree, type CreditProject } from './credits';
const field = (key: string, initial: string | number | boolean, choices: string[] = []): SettingField =>
  ({ key, initial, choices, min: 1, max: 4, group: 'general', unit: '', reserved: '', scope: 'shared', directory: false, hidden: false });
describe('settings drafts', () => {
  it('shows and validates the shared app-language choices', () => {
    const locale = field('locale', 'ui', ['ui', 'zh-CN', 'zh-TW', 'en-US', 'ja-JP']);
    expect(displayValue(locale, 'ui')).toBe('跟随界面语言');
    expect(displayValue(locale, 'zh-CN')).toBe('简体中文');
    expect(displayValue(locale, 'zh-TW')).toBe('繁體中文');
    expect(displayValue(locale, 'en-US')).toBe('English');
    expect(displayValue(locale, 'ja-JP')).toBe('日本語');
    expect(settingsError([locale], { locale: 'ui' })).toBeNull();
    expect(settingsError([locale], { locale: 'invalid' })).not.toBeNull();
  });
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
  it('words fields from their key with unit and reserved codes', () => {
    const jit = { ...field('jit_cache_mb', 64), unit: 'mb', reserved: 'runtimeConfig' };
    expect(fieldMeta(jit)).toEqual({ label: 'JIT 缓存', unit: 'MB', hint: '', reserved: '运行时配置入口尚未接入' });
    expect(fieldMeta(field('cpu_cores', 1))).toEqual({ label: '虚拟 CPU 核数', unit: '', hint: '游戏查询到的核数；不限制宿主线程', reserved: '' });
    expect(fieldMeta(field('ephemeral_sandbox', false))).toEqual({ label: '临时沙盒', unit: '', hint: '退出不保留本次存档', reserved: '' });
    expect(fieldMeta(field('max_fps', 'unlimited')).label).toBe('帧率上限');
    expect(displayValue({ ...jit }, 128)).toBe('128 MB');
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
    expect(stepValue({ ...field('volume', 100), unit: 'percent', max: 100 }, 50, -1)).toBe(45);
  });
});

// Host field definitions: `{"key", "group", "label", ...}` rows (one line or one field per line, as clang-format
// leaves them).
const hostSettings = Object.values(import.meta.glob(['../../../../src/frontend/library/settings.cpp', '../../../../src/frontend/library/game_settings.cpp'],
  { query: '?raw', import: 'default', eager: true })) as string[];
describe('reserved settings in release builds', () => {
  it('drops reserved rows and the groups they leave empty, keeping groups that never had fields', () => {
    const reserved = { ...field('volume', 100), group: 'audio', reserved: 'runtime' };
    const live = { ...field('supersample', 1), group: 'display' };
    const all = [reserved, live];
    expect(shownFields(all, true)).toEqual(all);
    const shown = shownFields(all, false);
    expect(shown).toEqual([live]);
    const pageGroups = [['display', '显示', ''], ['audio', '音频', ''], ['about', '关于', '']] as const;
    expect(shownGroups(pageGroups, all, shown).map(([key]) => key)).toEqual(['display', 'about']);
    expect(shownGroups(pageGroups, all, all).map(([key]) => key)).toEqual(['display', 'audio', 'about']);
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
    expect(factRows({ android_manifest: null, profile_status: 'missing', custom: 'x', artifacts_error: 'boom' }))
      .toEqual([['Android / BootDex / guest JNI 制品清单', '文件不可用或超过读取上限'], ['制品信息不可用', 'boom'],
        ['Profile 状态', '记录的 Profile 在所选目录中不存在。'], ['custom', 'x']]);
  });
});
describe('open-source credits', () => {
  const project = (id: string, parent?: string): CreditProject => ({ id, name: id, version: '1.0', url: '', license: 'MIT', parent });
  const tree = creditTree([project('dynarmic'), project('oaknut', 'dynarmic'), project('zydis', 'dynarmic'), project('zycore', 'zydis'), project('boost'), project('three'), project('draco', 'three')]);
  it('keeps direct projects on top and folds the rest inside their parent, in list order', () => {
    expect(tree.map(node => node.project.id)).toEqual(['dynarmic', 'boost', 'three']);
    expect(tree[0].children.map(node => node.project.id)).toEqual(['oaknut', 'zydis']);
    expect(tree[0].children[1].children.map(node => node.project.id)).toEqual(['zycore']);
  });
  it('counts nested dependencies in the row summary', () => {
    expect(creditSummary(tree[0])).toBe('1.0 · MIT · 3 个依赖');
    expect(creditSummary(tree[1])).toBe('1.0 · MIT');
    expect(creditSummary(tree[2])).toBe('1.0 · MIT · 1 个依赖');
  });
});
describe('fps overlay rows', () => {
  it('shows the corner only while the counter is on', () => {
    const fields = [field('fps_overlay', false), field('fps_overlay_position', 'top-left', ['top-left', 'top-right', 'bottom-left', 'bottom-right'])];
    expect(overlayFields(fields, { fps_overlay: false }).map(item => item.key)).toEqual(['fps_overlay']);
    expect(overlayFields(fields, { fps_overlay: true }).map(item => item.key)).toEqual(['fps_overlay', 'fps_overlay_position']);
    expect(displayValue(fields[1], 'bottom-right')).toBe('右下');
  });
});
describe('play license row', () => {
  it('shows the license answer only while Google Play is on', () => {
    const fields = [field('google_play', true), field('google_play_license', 'not-licensed', ['not-licensed', 'licensed', 'error-contacting-server'])];
    expect(overlayFields(fields, { google_play: false }).map(item => item.key)).toEqual(['google_play']);
    expect(overlayFields(fields, { google_play: true }).map(item => item.key)).toEqual(['google_play', 'google_play_license']);
    expect(displayValue(fields[1], 'error-contacting-server')).toBe('无法连接服务器 (0x101)');
  });
});
describe('setting sections', () => {
  it('gives game pages the global sections in the same order, without the launcher ones', () => {
    expect(gameGroups.map(([key]) => key)).toEqual(['display', 'data', 'input', 'audio', 'device', 'performance', 'network', 'developer']);
    expect(groups.map(([key]) => key).filter(key => !isLauncherGroup(key))).toEqual(gameGroups.map(([key]) => key));
  });
  it('marks launcher-only fields only where games share the section', () => {
    expect(globalOnlyInSharedGroup({ ...field('audio_device', ''), group: 'audio', scope: 'global' })).toBe(true);
    expect(globalOnlyInSharedGroup({ ...field('theme', 'dark'), group: 'general', scope: 'global' })).toBe(false);
    expect(globalOnlyInSharedGroup({ ...field('volume', 100), group: 'audio', scope: 'shared' })).toBe(false);
  });
});
