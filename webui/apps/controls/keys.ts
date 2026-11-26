import { t } from './i18n';
// Physical key names shared with input::ParseInputSource; KeyboardEvent.code is layout independent.
const named: Record<string, string> = {
  Enter: 'enter', Escape: 'escape', Backspace: 'backspace', Tab: 'tab', Space: 'space',
  Minus: 'minus', Equal: 'equals', BracketLeft: 'left_bracket', BracketRight: 'right_bracket',
  Backslash: 'backslash', Semicolon: 'semicolon', Quote: 'apostrophe', Backquote: 'grave',
  Comma: 'comma', Period: 'period', Slash: 'slash', CapsLock: 'caps_lock', Insert: 'insert',
  Home: 'home', PageUp: 'page_up', Delete: 'delete', End: 'end', PageDown: 'page_down',
  ArrowRight: 'right', ArrowLeft: 'left', ArrowDown: 'down', ArrowUp: 'up',
  ControlLeft: 'left_ctrl', ShiftLeft: 'left_shift', AltLeft: 'left_alt',
  ControlRight: 'right_ctrl', ShiftRight: 'right_shift', AltRight: 'right_alt',
};
// F1/F2 and Cmd/Ctrl combinations belong to the host (ADR-0134).
const reserved = new Set(['f1', 'f2']);

export type Captured = { source: string } | { error: string };

export function keyName(code: string): string | undefined {
  const letter = /^Key([A-Z])$/.exec(code);
  if (letter) return letter[1].toLowerCase();
  const digit = /^Digit([0-9])$/.exec(code);
  if (digit) return digit[1];
  const fn = /^F([1-9]|1[0-2])$/.exec(code);
  if (fn) return `f${fn[1]}`;
  return named[code];
}

export function captureKey(event: { code: string; ctrlKey: boolean; metaKey: boolean }): Captured {
  if (event.metaKey || event.code.startsWith('Meta')) return { error: t('controls.key.reservedMeta') };
  if (event.ctrlKey && !event.code.startsWith('Control')) return { error: t('controls.key.reservedCtrl') };
  const name = keyName(event.code);
  if (!name) return { error: t('controls.key.unsupported', { key: event.code || t('controls.key.unknown') }) };
  if (reserved.has(name)) return { error: t('controls.key.reserved', { key: name.toUpperCase() }) };
  return { source: `key:${name}` };
}

const mouseNames = ['left', 'middle', 'right', 'x1', 'x2'];
// MouseEvent.button: 0 primary, 1 middle, 2 secondary, 3/4 back/forward.
export function captureMouse(button: number): Captured {
  const name = mouseNames[button];
  return name ? { source: `mouse:${name}` } : { error: t('controls.mouse.unsupported', { button }) };
}

// WheelEvent.deltaY < 0 scrolls up, matching the host's +delta_y.
export function captureWheel(deltaY: number): Captured {
  if (deltaY < 0) return { source: 'wheel:up' };
  if (deltaY > 0) return { source: 'wheel:down' };
  return { error: t('controls.mouse.scrollPrompt') };
}

const labels: Record<string, string> = {
  space: 'Space', enter: 'Enter', escape: 'Esc', backspace: '⌫', tab: 'Tab', up: '↑', down: '↓',
  left: '←', right: '→', left_shift: 'LShift', right_shift: 'RShift', left_ctrl: 'LCtrl',
  right_ctrl: 'RCtrl', left_alt: 'LAlt', right_alt: 'RAlt', caps_lock: 'Caps', page_up: 'PgUp',
  page_down: 'PgDn', delete: 'Del', insert: 'Ins', home: 'Home', end: 'End', minus: '-',
  equals: '=', left_bracket: '[', right_bracket: ']', backslash: '\\', semicolon: ';',
  apostrophe: "'", grave: '~', comma: ',', period: '.', slash: '/',
};
const mouseLabels = (): Record<string, string> => ({ left: t('controls.mouse.left'), middle: t('controls.mouse.middle'), right: t('controls.mouse.right'), x1: t('controls.mouse.side1'), x2: t('controls.mouse.side2') });

export function sourceLabel(source: string): string {
  if (source === 'mouse:motion') return t('controls.mouse.motion');
  if (source === 'wheel:up') return t('controls.mouse.wheelUp');
  if (source === 'wheel:down') return t('controls.mouse.wheelDown');
  if (source.startsWith('mouse:')) return mouseLabels()[source.slice(6)] ?? source;
  const name = source.startsWith('key:') ? source.slice(4) : source;
  return labels[name] ?? name.toUpperCase();
}

// A spoken name for keys whose cap is only a symbol, so "~" reads as the tilde key.
const captions = (): Record<string, string> => ({
  grave: t('controls.keyName.grave'), minus: t('controls.keyName.minus'), equals: t('controls.keyName.equal'), left_bracket: t('controls.keyName.bracketLeft'), right_bracket: t('controls.keyName.bracketRight'),
  backslash: t('controls.keyName.backslash'), semicolon: t('controls.keyName.semicolon'), apostrophe: t('controls.keyName.quote'), comma: t('controls.keyName.comma'), period: t('controls.keyName.period'), slash: t('controls.keyName.slash'),
});
export function keyCaption(source: string): string | undefined {
  return source.startsWith('key:') ? captions()[source.slice(4)] : undefined;
}

// "keys:w,a,s,d" -> ["key:w", ...]; any other source is one entry.
export function splitSources(source: string): string[] {
  return source.startsWith('keys:') ? source.slice(5).split(',').map(name => `key:${name.trim()}`) : [source];
}
export function joinStickSources(sources: string[]): string {
  return 'keys:' + sources.map(source => source.slice(4)).join(',');
}
// Rebinds one direction (0 up, 1 left, 2 down, 3 right); undefined when another direction already has that key.
export function replaceStickSource(group: string, direction: number, source: string): string | undefined {
  const sources = splitSources(group);
  if (sources.some((other, index) => index !== direction && other === source)) return undefined;
  sources[direction] = source;
  return joinStickSources(sources);
}
