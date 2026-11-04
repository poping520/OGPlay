import { afterEach, expect, it } from 'vitest';
import ts from 'typescript';
import { en } from './i18n-en';
import { navigatorLanguage, setLanguage, t } from './i18n';
import { scan } from '../../packages/ui-kit/i18n-test-utils';
const han = /[一-鿿]/;
const sources = Object.entries(import.meta.glob(['./*.ts', './*.tsx', '!./*.test.ts', '!./i18n-en.ts'],
  { query: '?raw', import: 'default', eager: true }));
afterEach(() => setLanguage('zh-CN'));
it('covers controls strings, with no Chinese outside translation calls', () => {
  const missing: string[] = [], stray: string[] = [];
  expect(sources.length).toBeGreaterThan(3);
  for (const [name, source] of sources) {
    const { literals, code } = scan(source);
    for (const text of literals) if (han.test(text) && !(text in en)) missing.push(`${name}: ${text}`);
    if (han.test(code)) stray.push(name);
    const ast = ts.createSourceFile(name, source, ts.ScriptTarget.Latest, true, name.endsWith('tsx') ? ts.ScriptKind.TSX : ts.ScriptKind.TS);
    const visit = (node: ts.Node) => {
      if ((ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) && han.test(node.text)) {
        const parent = node.parent;
        if (!ts.isCallExpression(parent) || !ts.isIdentifier(parent.expression) || parent.expression.text !== 't' || parent.arguments[0] !== node)
          stray.push(`${name}: ${node.text}`);
      }
      ts.forEachChild(node, visit);
    };
    visit(ast);
  }
  expect(missing).toEqual([]); expect(stray).toEqual([]);
});
it('preserves placeholders and uses English without CJK', () => {
  const names = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map(match => match[1]).sort();
  for (const [key, value] of Object.entries(en)) {
    expect(names(value), key).toEqual(names(key)); expect(han.test(value), key).toBe(false);
  }
  setLanguage('en-US'); expect(t('按键设置')).toBe('Key Settings');
  expect(t('保存位置：{path}', { path: '/input.toml' })).toBe('Save location: /input.toml');
});
it('uses navigator Chinese variants before the session language arrives', () => {
  for (const locale of ['zh', 'zh-CN', 'zh-TW', 'ZH-hans']) expect(navigatorLanguage(locale)).toBe('zh-CN');
  for (const locale of ['', 'en-US', 'fr-FR']) expect(navigatorLanguage(locale)).toBe('en-US');
});
