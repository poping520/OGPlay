import { describe, expect, it } from 'vitest';
import { formatParts, messageArguments, parseMessage } from './icu';

const format = (text: string, params: Record<string, unknown> = {}, locale = 'en-US') =>
  formatParts(parseMessage(text), params, locale).join('');

describe('ICU messages', () => {
  it('fills arguments and leaves unknown ones visible', () => {
    expect(format('Delete “{name}”?', { name: 'Sweet' })).toBe('Delete “Sweet”?');
    expect(format('{a} and {b}', { a: 1 })).toBe('1 and {b}');
  });
  it('picks plural forms by locale, exact matches first, with # as the number', () => {
    const games = '{n, plural, =0 {no games} one {# game} other {# games}}';
    expect([0, 1, 2, 1000].map(n => format(games, { n }))).toEqual(['no games', '1 game', '2 games', '1,000 games']);
    const ru = '{n, plural, one {# игра} few {# игры} many {# игр} other {# игры}}';
    expect([1, 3, 5, 21, 1.5].map(n => format(ru, { n }, 'ru-RU'))).toEqual(['1 игра', '3 игры', '5 игр', '21 игра', '1,5 игры']);
    expect(format('{n, plural, one {un} other {#}}', { n: '1' }, 'fr-FR')).toBe('un');
    expect(format('{n, plural, offset:1 one {you and # other} other {you and # others}}', { n: 3 })).toBe('you and 2 others');
  });
  it('selects by value and keeps quoted text literal', () => {
    expect(format('{kind, select, apk {APK} other {archive}}', { kind: 'apk' })).toBe('APK');
    expect(format('{kind, select, apk {APK} other {archive}}', { kind: 'zip' })).toBe('archive');
    expect(format("game's '{braces}' and '' quote")).toBe("game's {braces} and ' quote");
    expect(format("{n, plural, other {'#' is #}}", { n: 4 })).toBe('# is 4');
  });
  it('keeps rich arguments as parts and lists every argument', () => {
    const bold = { tag: 'b' };
    expect(formatParts(parseMessage('around {path}.'), { path: bold }, 'en-US')).toEqual(['around ', bold, '.']);
    expect([...messageArguments(parseMessage('{a} {n, plural, one {{b}} other {# {c}}}'))].sort()).toEqual(['a', 'b', 'c', 'n']);
  });
  it('rejects malformed messages', () => {
    for (const bad of ['{', '}', '{n, plural, one {x}}', '{n, number}', "'{unclosed", '{n, plural, one {x} one {y} other {z}}'])
      expect(() => parseMessage(bad), bad).toThrow(SyntaxError);
  });
});
