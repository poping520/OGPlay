import { afterEach, describe, expect, it } from 'vitest';
import { catalogs, gameName, initialLanguage, language, LANGUAGE_NAMES, LANGUAGES, resolvedLanguage, setLanguage, t, tj, uiText } from './i18n';
import { fieldMeta, type SettingField } from '../settings/settings';
import { catalogProblems, cppStrings, MESSAGE_ID, scan } from '../../../packages/ui-kit/i18n-test-utils';

const han = /[一-鿿]/;
const source = catalogs['zh-CN'];
// The 3D bundle's decoder error stays as written: cart3d.js has no access to the launcher language.
const sources = Object.entries(import.meta.glob(['../**/*.ts', '../**/*.tsx', '!../**/*.test.ts', '!../**/*.d.ts',
  '!./messages/*.ts', '!../cartridge/cart3d/draco.ts'], { query: '?raw', import: 'default', eager: true })) as [string, string][];
const host = Object.entries(import.meta.glob(['../../../../src/frontend/**/*.cpp', '../../../../src/frontend/**/*.h',
  '../../../../src/hal/**/*.cpp', '../../../../src/hal/**/*.mm'], { query: '?raw', import: 'default', eager: true })) as [string, string][];
// The host's own native text (menus, tray, close dialog) has its own table; nothing else words UI in C++.
const hostText = host.filter(([name]) => !name.endsWith('/frontend/app/ui_text.cpp'));
const hostStrings = hostText.flatMap(([name, text]) => cppStrings(text).map(value => [name, value] as const));
// Host messages are host.<area>.<name>; two-segment host.* strings are RPC methods (host.features).
const hostIds = new Set(hostStrings.map(([, value]) => value)
  .filter(value => value.startsWith('host.') && MESSAGE_ID.test(value) && value.split('.').length >= 3));
// Dotted host strings that are not messages: RPC methods ("library.launch") and the like.
const hostNames = new Set(hostStrings.map(([, value]) => value));
const namespaces = new Set(Object.keys(source).map(id => id.split('.')[0]));
// IDs the page builds from host codes rather than spelling out (see settings.ts fieldMeta).
const derived = /^setting\.(?:[a-z0-9_]+\.(?:label|defaultLabel|hint)|unit\.\w+|reserved\.\w+)$/;

describe('GUI-20 message catalogs', () => {
  it('keeps every language in step with zh-CN as valid ICU', () => {
    expect(Object.keys(catalogs).sort()).toEqual([...LANGUAGES].sort());
    expect(Object.keys(source).length).toBeGreaterThan(600);
    expect(catalogProblems(catalogs)).toEqual([]);
    expect(Object.keys(LANGUAGE_NAMES).sort()).toEqual([...LANGUAGES].sort());
  });

  it('names only existing messages, uses every one and writes no Chinese outside the catalogs', () => {
    expect(sources.length).toBeGreaterThan(10);
    const used = new Set<string>(), unknown: string[] = [], stray: string[] = [];
    for (const [name, text] of sources) {
      const { literals, code } = scan(text);
      for (const literal of literals) {
        if (han.test(literal)) stray.push(`${name}: ${literal}`);
        if (!MESSAGE_ID.test(literal) || !namespaces.has(literal.split('.')[0])) continue;
        if (literal in source) used.add(literal);
        else if (!hostNames.has(literal)) unknown.push(`${name}: ${literal}`);
      }
      stray.push(...code.split('\n').filter(line => han.test(line)).map(line => `${name}: ${line.trim()}`));
    }
    expect(unknown).toEqual([]);
    expect(stray).toEqual([]);
    const unused = Object.keys(source).filter(id => !used.has(id) && !hostIds.has(id) && !derived.test(id));
    expect(unused).toEqual([]);
  });

  it('words every host message and leaves C++ free of UI text', () => {
    expect(host.length).toBeGreaterThan(20);
    expect([...hostIds].filter(id => !(id in source))).toEqual([]);
    expect(Object.keys(source).filter(id => id.startsWith('host.') && !hostIds.has(id) &&
      !sources.some(([, text]) => scan(text).literals.includes(id)))).toEqual([]);
    expect(hostStrings.filter(([, value]) => han.test(value)).map(([name, value]) => `${name}: ${value}`)).toEqual([]);
  });
});

describe('GUI-14 language switching', () => {
  afterEach(() => setLanguage('zh-CN'));

  it('resolves system using the host language', () => {
    expect(resolvedLanguage('system', 'en-US')).toBe('en-US');
    expect(resolvedLanguage('system', 'zh-CN')).toBe('zh-CN');
    expect(resolvedLanguage('zh-CN', 'en-US')).toBe('zh-CN');
    expect(resolvedLanguage('en-US', 'zh-CN')).toBe('en-US');
    expect(resolvedLanguage('system', 'ja-JP')).toBe('ja-JP');
    expect(resolvedLanguage('pt-BR', 'zh-CN')).toBe('pt-BR');
    expect(resolvedLanguage('de-DE', 'en-US')).toBe('zh-CN');
  });
  it('starts from the remembered language, else the system language', () => {
    expect(initialLanguage(null, 'en-GB')).toBe('en-US');
    expect(initialLanguage(null, 'zh-Hant-TW')).toBe('zh-TW');
    expect(initialLanguage(null, 'zh-Hans-CN')).toBe('zh-CN');
    expect(initialLanguage(null, 'ko')).toBe('ko-KR');
    expect(initialLanguage('fr-FR', 'en-US')).toBe('fr-FR');
    expect(initialLanguage('zh-CN', 'en-US')).toBe('zh-CN');
    expect(initialLanguage('en-US', 'zh-CN')).toBe('en-US');
    expect(initialLanguage('system', 'en-US')).toBe('en-US');
  });
  it('looks messages up by ID with plurals, rich arguments and fallbacks', () => {
    expect(t('library.play')).toBe('开始游戏');
    expect(t('library.gameCount', { n: 3 })).toBe('3 款游戏');
    setLanguage('en-US');
    expect(language()).toBe('en-US');
    expect(t('library.play')).toBe('Play');
    expect([1, 2].map(n => t('library.gameCount', { n }))).toEqual(['1 game', '2 games']);
    expect(tj('dataHint.readsAround', { path: 42 })).toEqual(['The game seems to read data around ', 42, '.']);
    expect(t('no.such.message')).toBe('no.such.message');
    setLanguage('ru-RU');
    expect([1, 3, 5].map(n => t('library.gameCount', { n }))).toEqual(['1 игра', '3 игры', '5 игр']);
    setLanguage('fr-FR');
    expect(t('library.runningCount', { n: 2 })).toBe('2 en cours');
    setLanguage('zh-TW');
    expect(t('library.play')).toBe('開始遊戲');
    setLanguage('de-DE');
    expect(language()).toBe('zh-CN');
  });
  it('shows host messages by ID and diagnostic text as it is', () => {
    setLanguage('en-US');
    expect(uiText({ text: 'host.error.importBusy', id: 'host.error.importBusy' })).toBe('An import is in progress. Try again later.');
    expect(uiText({ text: 'x', id: 'host.error.iconMissing', params: { path: '/a.png' } })).toBe('Icon asset missing: /a.png');
    expect(uiText({ text: 'No such file or directory' })).toBe('No such file or directory');
  });

  it('words host fields from their key, unit and reserved codes', () => {
    const field = (key: string, unit: string, reserved: string, global_default = false): SettingField =>
      ({ key, group: 'g', unit, reserved, global_default, initial: 1, choices: [], min: 0, max: 10, directory: false, hidden: false });
    setLanguage('en-US');
    expect(fieldMeta(field('stall_timeout', 'seconds', 'launchArgs')))
      .toEqual({ label: 'Stall snapshot threshold', unit: 's', hint: '', reserved: 'Not wired to launch arguments yet' });
    expect(fieldMeta(field('ephemeral_sandbox', '', '')).hint).toBe('saves from this run are discarded on exit');
    expect(fieldMeta(field('interpreter', '', '', true)).label).toBe('Default interpreter');
    expect(fieldMeta(field('interpreter', '', '')).label).toBe('Interpreter');
  });
  it('names games in the launcher language before the APK default', () => {
    const names = { en: 'Sweet Shop', 'zh-TW': '糖果舖' };
    expect(gameName({ display_name: '糖果铺', localized_names: names }, 'en-US')).toBe('Sweet Shop');
    expect(gameName({ display_name: 'Candy', localized_names: names }, 'zh-CN')).toBe('糖果舖');
    expect(gameName({ display_name: '糖果铺', localized_names: { en: 'Sweet Shop' } }, 'zh-CN')).toBe('糖果铺');
    expect(gameName({ display_name: 'X', localized_names: { 'en-GB': 'GB', 'en-US': 'US' } }, 'en-US')).toBe('US');
  });
});
