import { afterEach, describe, expect, it } from 'vitest';
import { en } from './i18n-en';
import { language, setLanguage, t, tj } from './i18n';
import { fieldMeta, type SettingField } from './settings';

const han = /[一-鿿]/;

// Splits source into string literals (template text without its ${} parts) and the remaining code,
// dropping comments, so Chinese can be checked wherever it appears.
function scan(source: string): { literals: string[]; code: string } {
  const literals: string[] = [];
  let code = '', i = 0;
  const quoted = (quote: string) => {
    let value = '';
    for (++i; i < source.length && source[i] !== quote && source[i] !== '\n'; ++i) {
      if (source[i] === '\\') { value += source[i] + source[i + 1]; ++i; } else value += source[i];
    }
    ++i;
    return value;
  };
  const template = () => {
    let value = '';
    for (++i; i < source.length && source[i] !== '`';) {
      if (source[i] === '\\') { value += source.slice(i, i + 2); i += 2; }
      else if (source[i] === '$' && source[i + 1] === '{') { i += 2; block(true); ++i; }
      else value += source[i++];
    }
    ++i;
    return value;
  };
  const block = (nested: boolean) => {
    let depth = 0;
    while (i < source.length) {
      const char = source[i];
      if (nested && char === '}' && depth === 0) return;
      if (char === '/' && source[i + 1] === '/') { while (i < source.length && source[i] !== '\n') ++i; continue; }
      if (char === '/' && source[i + 1] === '*') { const end = source.indexOf('*/', i + 2); i = end < 0 ? source.length : end + 2; continue; }
      if (char === '\'' || char === '"') { literals.push(quoted(char)); continue; }
      if (char === '`') { literals.push(template()); continue; }
      if (char === '{') ++depth; else if (char === '}') --depth;
      code += char; ++i;
    }
  };
  block(false);
  return { literals, code };
}

// The 3D bundle's decoder error stays as written: cart3d.js has no access to the launcher language.
const sources = Object.entries(import.meta.glob(['./*.ts', './*.tsx', './cart3d/*.ts', '!./*.test.ts', '!./*.d.ts', '!./i18n*.ts', '!./cart3d/draco.ts'],
  { query: '?raw', import: 'default', eager: true }));
const hostSettings = Object.entries(import.meta.glob(['../../../src/frontend/library/settings.cpp', '../../../src/frontend/library/game_settings.cpp'],
  { query: '?raw', import: 'default', eager: true }));

describe('GUI-14 translation table', () => {
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
      const { literals } = scan(text);
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
      ({ key: 'k', group: 'g', label, pending, initial, choices: [], min: 0, max: 10, directory: false });
    setLanguage('en-US');
    expect(fieldMeta(field('停滞快照阈值（秒）', '预留 · 启动参数尚未接入', 30)))
      .toEqual({ label: 'Stall snapshot threshold', unit: 's', hint: '', reserved: 'Not wired to launch arguments yet' });
    expect(fieldMeta(field('临时沙盒（退出不保留本次存档）', '', false)).hint).toBe('saves from this run are discarded on exit');
  });
});
