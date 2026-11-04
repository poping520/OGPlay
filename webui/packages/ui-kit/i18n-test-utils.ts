// Splits source into string literals (template text without its ${} parts) and the remaining code,
// dropping comments, so Chinese can be checked wherever it appears.
export function scan(source: string): { literals: string[]; code: string } {
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

// C++/Objective-C++ lexer: comments and character constants are not UI strings. Raw strings
// terminate only at their delimiter; comment markers/quotes inside a string remain text.
export function cppStrings(source: string): string[] {
  const strings: string[] = [];
  let i = 0;
  const quoted = (quote: string): string => {
    let value = '';
    ++i;
    while (i < source.length) {
      const char = source[i++];
      if (char === quote) return value;
      if (char !== '\\') { value += char; continue; }
      const escaped = source[i++];
      if (escaped === '\n') continue;
      if (escaped === 'u' || escaped === 'U') {
        const size = escaped === 'u' ? 4 : 8, hex = source.slice(i, i + size);
        value += String.fromCodePoint(parseInt(hex, 16)); i += size;
      } else value += ({ n: '\n', r: '\r', t: '\t', '0': '\0' } as Record<string, string>)[escaped] ?? escaped;
    }
    throw new Error('Unterminated C++ quoted literal');
  };
  while (i < source.length) {
    if (source.startsWith('//', i)) { const end = source.indexOf('\n', i + 2); i = end < 0 ? source.length : end + 1; continue; }
    if (source.startsWith('/*', i)) { const end = source.indexOf('*/', i + 2); if (end < 0) throw new Error('Unterminated C++ comment'); i = end + 2; continue; }
    const raw = /^(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(/.exec(source.slice(i));
    if (raw) {
      const start = i + raw[0].length, end = source.indexOf(`)${raw[1]}"`, start);
      if (end < 0) throw new Error('Unterminated C++ raw string');
      strings.push(source.slice(start, end)); i = end + raw[1].length + 2; continue;
    }
    if (source[i] === '"') { strings.push(quoted('"')); continue; }
    // Apostrophes inside numeric preprocessing tokens are digit separators, not characters.
    if (/[0-9]/.test(source[i])) {
      ++i; while (i < source.length && /[\w.']/.test(source[i])) ++i; continue;
    }
    if (source[i] === "'") { quoted("'"); continue; }
    ++i;
  }
  return strings;
}
