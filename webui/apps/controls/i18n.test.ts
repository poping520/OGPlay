import { afterEach, expect, it } from 'vitest';
import ts from 'typescript';
import { catalogs, navigatorLanguage, setLanguage, t } from './i18n';
import { catalogProblems, MESSAGE_ID, scan } from '../../packages/ui-kit/i18n-test-utils';
const han = /[一-鿿]/;
const source = catalogs['zh-CN'];
const sources = Object.entries(import.meta.glob(['./*.ts', './*.tsx', '!./*.test.ts'],
  { query: '?raw', import: 'default', eager: true })) as [string, string][];
afterEach(() => setLanguage('zh-CN'));
it('keeps every language in step with zh-CN as valid ICU', () => {
  expect(Object.keys(source).length).toBeGreaterThan(100);
  expect(catalogProblems(catalogs)).toEqual([]);
});
it('names only existing messages, uses every one and writes no Chinese outside the catalogs', () => {
  const used = new Set<string>(), unknown: string[] = [], stray: string[] = [];
  expect(sources.length).toBeGreaterThan(3);
  for (const [name, text] of sources) {
    const { literals, code } = scan(text);
    for (const literal of literals) {
      if (han.test(literal)) stray.push(`${name}: ${literal}`);
      if (MESSAGE_ID.test(literal) && literal.startsWith('controls.')) { if (literal in source) used.add(literal); else unknown.push(`${name}: ${literal}`); }
    }
    if (han.test(code)) stray.push(name);
    // Every t()/tj() names its message with a literal ID, so the catalog check above sees all of them.
    const ast = ts.createSourceFile(name, text, ts.ScriptTarget.Latest, true, name.endsWith('tsx') ? ts.ScriptKind.TSX : ts.ScriptKind.TS);
    const visit = (node: ts.Node) => {
      if (ts.isCallExpression(node) && ts.isIdentifier(node.expression) && ['t', 'tj'].includes(node.expression.text)) {
        const [first] = node.arguments;
        if (first && (ts.isStringLiteral(first) || ts.isNoSubstitutionTemplateLiteral(first)) && !(first.text in source))
          unknown.push(`${name}: ${first.text}`);
      }
      ts.forEachChild(node, visit);
    };
    visit(ast);
  }
  expect(unknown).toEqual([]);
  expect(stray).toEqual([]);
  expect(Object.keys(source).filter(id => !used.has(id))).toEqual([]);
});
it('looks messages up by ID in the chosen language', () => {
  setLanguage('en-US'); expect(t('controls.title')).toBe('Key Settings');
  expect(t('controls.saveLocation', { path: '/input.toml' })).toBe('Save location: /input.toml');
  expect(t('controls.binding.mode', { mode: 'fly' })).toBe('Mode fly');
  setLanguage('ko-KR'); expect(t('controls.title')).toBe('키 설정');
});
it('uses navigator languages before the session language arrives', () => {
  for (const locale of ['zh', 'zh-CN', 'zh-SG', 'ZH-hans']) expect(navigatorLanguage(locale)).toBe('zh-CN');
  for (const locale of ['zh-TW', 'zh-HK', 'zh-Hant-CN']) expect(navigatorLanguage(locale)).toBe('zh-TW');
  for (const [locale, tag] of [['ja', 'ja-JP'], ['ko-KR', 'ko-KR'], ['ru-UA', 'ru-RU'], ['es-MX', 'es-ES'], ['pt-PT', 'pt-BR'], ['fr-CA', 'fr-FR']])
    expect(navigatorLanguage(locale)).toBe(tag);
  for (const locale of ['', 'en-US', 'de-DE', 'zho']) expect(navigatorLanguage(locale)).toBe('en-US');
});
