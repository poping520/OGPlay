import { afterEach, describe, expect, it } from 'vitest';
import { en } from './i18n-en';
import { gameName, initialLanguage, language, resolvedLanguage, setLanguage, t, tj } from './i18n';
import { fieldMeta, type SettingField } from '../settings/settings';

const han = /[一-鿿]/;

import { scan, cppStrings } from '../../../packages/ui-kit/i18n-test-utils';
// The 3D bundle's decoder error stays as written: cart3d.js has no access to the launcher language.
const sources = Object.entries(import.meta.glob(['../**/*.ts', '../**/*.tsx', '!../**/*.test.ts', '!../**/*.d.ts', '!./i18n*.ts', '!../cartridge/cart3d/draco.ts'],
  { query: '?raw', import: 'default', eager: true }));
const hostSettings = Object.entries(import.meta.glob(['../../../../src/frontend/library/settings.cpp', '../../../../src/frontend/library/game_settings.cpp'],
  { query: '?raw', import: 'default', eager: true }));

const hostMessages = Object.entries(import.meta.glob([
  '../../../../src/frontend/gui/rpc.cpp', '../../../../src/frontend/gui/rpc_import.cpp',
  '../../../../src/frontend/gui/rpc_settings.cpp', '../../../../src/frontend/gui/rpc_data_hint.cpp',
  '../../../../src/frontend/gui/dashboard.cpp', '../../../../src/frontend/gui/dashboard.h',
  '../../../../src/frontend/gui/process_manager.cpp', '../../../../src/frontend/gui/webview_shell.cpp',
  '../../../../src/frontend/library/resolve_launch.cpp',
], { query: '?raw', import: 'default', eager: true }));
const nativeSources = Object.entries(import.meta.glob([
  '../../../../src/hal/macos/webview_host.mm', '../../../../src/hal/windows/webview_host.cpp',
  '../../../../src/hal/sdl_window_input.cpp', '../../../../src/frontend/game/run_game.cpp',
], { query: '?raw', import: 'default', eager: true }));

describe('GUI-20 translation table', () => {
  it('covers host messages and keeps native hosts free of Chinese strings', () => {
    expect(hostMessages).toHaveLength(9);
    expect(nativeSources).toHaveLength(4);
    const missing = hostMessages.flatMap(([name, source]) => cppStrings(source)
      .filter(text => han.test(text) && !(text in en)).map(text => `${name}: ${text}`));
    expect(missing).toEqual([]);
    expect(nativeSources.flatMap(([name, source]) => cppStrings(source)
      .filter(text => han.test(text)).map(text => `${name}: ${text}`))).toEqual([]);
  });

  it('covers every Chinese literal and leaves no Chinese outside t()', () => {
    const missing: string[] = [], stray: string[] = [];
    expect(sources.length).toBeGreaterThan(10);
    for (const [name, text] of sources) {
      const { literals, code } = scan(text);
      for (const literal of literals) if (han.test(literal) && !(literal in en)) missing.push(`${name}: ${literal}`);
      const outside = code.split('\n').filter(line => han.test(line));
      stray.push(...outside.map(line => `${name}: ${line.trim()}`));
    }
    expect(missing).toEqual([]);
    expect(stray).toEqual([]);
  });

  it('covers the host setting labels and notes as the settings page splits them', () => {
    const missing: string[] = [];
    expect(hostSettings.length).toBe(2);
    for (const [file, text] of hostSettings) {
      const literals = cppStrings(text);
      for (const literal of literals.filter(value => han.test(value))) {
        const bracket = /^(.*?)（([^）]+)）$/.exec(literal);
        const parts = bracket ? [bracket[1], bracket[2]] : [literal.replace(/^预留\s*·\s*/, '')];
        for (const part of parts) {
          for (const key of [part, part.replace(/^默认\s*/, '')]) if (han.test(key) && !(key in en)) missing.push(`${file}: ${key}`);
        }
      }
    }
    expect([...new Set(missing)]).toEqual([]);
  });

  it('keeps placeholders and leaves no Chinese in English text', () => {
    const names = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map(match => match[1]).sort();
    for (const [key, value] of Object.entries(en)) {
      expect(names(value), key).toEqual(names(key));
      if (key !== '简体中文') expect(han.test(value), key).toBe(false);
    }
  });
});

describe('GUI-14 language switching', () => {
  afterEach(() => setLanguage('zh-CN'));

  it('resolves system using the host language', () => {
    expect(resolvedLanguage('system', 'en-US')).toBe('en-US');
    expect(resolvedLanguage('system', 'zh-CN')).toBe('zh-CN');
    expect(resolvedLanguage('zh-CN', 'en-US')).toBe('zh-CN');
    expect(resolvedLanguage('en-US', 'zh-CN')).toBe('en-US');
  });
  it('starts from the remembered language, else the system language', () => {
    expect(initialLanguage(null, 'en-GB')).toBe('en-US');
    expect(initialLanguage(null, 'zh-Hant-TW')).toBe('zh-CN');
    expect(initialLanguage('zh-CN', 'en-US')).toBe('zh-CN');
    expect(initialLanguage('en-US', 'zh-CN')).toBe('en-US');
    expect(initialLanguage('system', 'en-US')).toBe('en-US');
  });
  it('translates in English, falls back to the source text and fills parameters', () => {
    expect(t('开始游戏')).toBe('开始游戏');
    setLanguage('en-US');
    expect(language()).toBe('en-US');
    expect(t('开始游戏')).toBe('Play');
    expect(t('{n} 运行中', { n: 2 })).toBe('2 running');
    expect(t('宿主给出的未知文字')).toBe('宿主给出的未知文字');
    expect(tj('游戏似乎在 {path} 附近读取数据。', { path: 42 })).toEqual(['The game seems to read data around ', 42, '.']);
    setLanguage('fr-FR');
    expect(language()).toBe('zh-CN');
  });

  it('translates host field labels, units and reserved notes', () => {
    const field = (label: string, pending: string, initial: SettingField['initial']): SettingField =>
      ({ key: 'k', group: 'g', label, pending, initial, choices: [], min: 0, max: 10, directory: false, hidden: false });
    setLanguage('en-US');
    expect(fieldMeta(field('停滞快照阈值（秒）', '预留 · 启动参数尚未接入', 30)))
      .toEqual({ label: 'Stall snapshot threshold', unit: 's', hint: '', reserved: 'Not wired to launch arguments yet' });
    expect(fieldMeta(field('临时沙盒（退出不保留本次存档）', '', false)).hint).toBe('saves from this run are discarded on exit');
  });
  it('names games in the launcher language before the APK default', () => {
    const names = { en: 'Sweet Shop', 'zh-TW': '糖果舖' };
    expect(gameName({ display_name: '糖果铺', localized_names: names }, 'en-US')).toBe('Sweet Shop');
    expect(gameName({ display_name: 'Candy', localized_names: names }, 'zh-CN')).toBe('糖果舖');
    expect(gameName({ display_name: '糖果铺', localized_names: { en: 'Sweet Shop' } }, 'zh-CN')).toBe('糖果铺');
    expect(gameName({ display_name: 'X', localized_names: { 'en-GB': 'GB', 'en-US': 'US' } }, 'en-US')).toBe('US');
  });
});
