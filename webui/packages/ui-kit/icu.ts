// ICU MessageFormat subset the catalogs use: {name}, {n, plural, =0 {…} one {…} other {…}} with #, {x, select, … other {…}}
// and apostrophe quoting ('' is a quote; '{…}' is literal text). Plural categories come from Intl.PluralRules.
export type MessageNode =
  | string
  | { kind: 'argument'; name: string }
  | { kind: 'pound' }
  | { kind: 'plural' | 'select'; name: string; offset: number; options: Record<string, MessageNode[]> };

const special = (char: string | undefined, plural: boolean) => char === '{' || char === '}' || (plural && char === '#');

export function parseMessage(text: string): MessageNode[] {
  let i = 0;
  const fail = (why: string): never => { throw new SyntaxError(`${why} at ${i} in "${text}"`); };
  const skipSpace = () => { while (/\s/.test(text[i] ?? '')) ++i; };
  const word = () => { const start = i; while (i < text.length && /[\w=.:-]/.test(text[i])) ++i; return text.slice(start, i); };
  const nodes = (plural: boolean, nested: boolean): MessageNode[] => {
    const out: MessageNode[] = [];
    let literal = '';
    const flush = () => { if (literal) out.push(literal); literal = ''; };
    while (i < text.length) {
      const char = text[i];
      if (char === "'") {
        if (text[i + 1] === "'") { literal += "'"; i += 2; continue; }
        if (!special(text[i + 1], plural)) { literal += "'"; ++i; continue; }
        for (++i; i < text.length; ++i) {
          if (text[i] === "'" && text[i + 1] === "'") { literal += "'"; ++i; }
          else if (text[i] === "'") break;
          else literal += text[i];
        }
        if (i >= text.length) fail('unterminated quote');
        ++i; continue;
      }
      if (char === '}') { if (!nested) fail('unbalanced }'); flush(); return out; }
      if (char === '#' && plural) { flush(); out.push({ kind: 'pound' }); ++i; continue; }
      if (char !== '{') { literal += char; ++i; continue; }
      flush(); ++i; skipSpace();
      const name = word();
      if (!name) fail('missing argument name');
      skipSpace();
      if (text[i] === '}') { ++i; out.push({ kind: 'argument', name }); continue; }
      if (text[i] !== ',') fail('expected , or }');
      ++i; skipSpace();
      const type = word();
      if (type !== 'plural' && type !== 'select') fail(`unsupported argument type "${type}"`);
      skipSpace();
      if (text[i] !== ',') fail('expected ,');
      ++i;
      const options: Record<string, MessageNode[]> = {};
      let offset = 0;
      for (;;) {
        skipSpace();
        if (text[i] === '}') { ++i; break; }
        const selector = word();
        if (!selector) fail('missing selector');
        if (type === 'plural' && selector.startsWith('offset:')) { offset = Number(selector.slice(7)); continue; }
        skipSpace();
        if (text[i] !== '{') fail(`expected { after ${selector}`);
        ++i;
        if (selector in options) fail(`duplicate selector ${selector}`);
        options[selector] = nodes(type === 'plural', true);
        ++i;
      }
      if (!('other' in options)) fail(`${name} needs an other option`);
      out.push({ kind: type as 'plural' | 'select', name, offset, options });
    }
    if (nested) fail('unterminated option');
    flush();
    return out;
  };
  return nodes(false, false);
}

/** Every argument a message reads, whatever branch shows it. */
export function messageArguments(nodes: MessageNode[], into = new Set<string>()): Set<string> {
  for (const node of nodes) {
    if (typeof node === 'string' || node.kind === 'pound') continue;
    into.add(node.name);
    if (node.kind !== 'argument') for (const option of Object.values(node.options)) messageArguments(option, into);
  }
  return into;
}

const pluralRules = new Map<string, Intl.PluralRules>();
function pluralCategory(locale: string, value: number): string {
  let rules = pluralRules.get(locale);
  if (!rules) { rules = new Intl.PluralRules(locale); pluralRules.set(locale, rules); }
  return rules.select(value);
}

/** Formats to parts so arguments may stay rich values (JSX); missing arguments print as {name}. */
export function formatParts(nodes: MessageNode[], params: Record<string, unknown>, locale: string, pound?: number): unknown[] {
  const out: unknown[] = [];
  for (const node of nodes) {
    if (typeof node === 'string') out.push(node);
    else if (node.kind === 'pound') out.push(pound === undefined ? '#' : new Intl.NumberFormat(locale).format(pound));
    else if (node.kind === 'argument') out.push(node.name in params ? params[node.name] : `{${node.name}}`);
    else if (node.kind === 'select') {
      const value = String(params[node.name]);
      out.push(...formatParts(node.options[value] ?? node.options.other, params, locale, pound));
    } else {
      const value = Number(params[node.name]) - node.offset;
      const branch = node.options[`=${value + node.offset}`] ?? node.options[pluralCategory(locale, value)] ?? node.options.other;
      out.push(...formatParts(branch, params, locale, value));
    }
  }
  return out;
}
