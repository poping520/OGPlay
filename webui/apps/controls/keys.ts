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
  if (event.metaKey || event.code.startsWith('Meta')) return { error: 'Cmd/Win 组合键保留给宿主' };
  if (event.ctrlKey && !event.code.startsWith('Control')) return { error: 'Ctrl 组合键保留给宿主' };
  const name = keyName(event.code);
  if (!name) return { error: `不支持的按键 ${event.code || '（未知）'}` };
  if (reserved.has(name)) return { error: `${name.toUpperCase()} 保留给宿主` };
  return { source: `key:${name}` };
}

const mouseNames = ['left', 'middle', 'right', 'x1', 'x2'];
// MouseEvent.button: 0 primary, 1 middle, 2 secondary, 3/4 back/forward.
export function captureMouse(button: number): Captured {
  const name = mouseNames[button];
  return name ? { source: `mouse:${name}` } : { error: `不支持的鼠标键 ${button}` };
}

const labels: Record<string, string> = {
  space: 'Space', enter: 'Enter', escape: 'Esc', backspace: '⌫', tab: 'Tab', up: '↑', down: '↓',
  left: '←', right: '→', left_shift: 'LShift', right_shift: 'RShift', left_ctrl: 'LCtrl',
  right_ctrl: 'RCtrl', left_alt: 'LAlt', right_alt: 'RAlt', caps_lock: 'Caps', page_up: 'PgUp',
  page_down: 'PgDn', delete: 'Del', insert: 'Ins', home: 'Home', end: 'End', minus: '-',
  equals: '=', left_bracket: '[', right_bracket: ']', backslash: '\\', semicolon: ';',
  apostrophe: "'", grave: '`', comma: ',', period: '.', slash: '/',
};
const mouseLabels: Record<string, string> = { left: '鼠标左键', middle: '鼠标中键', right: '鼠标右键', x1: '鼠标侧键 1', x2: '鼠标侧键 2' };

export function sourceLabel(source: string): string {
  if (source.startsWith('mouse:')) return mouseLabels[source.slice(6)] ?? source;
  const name = source.startsWith('key:') ? source.slice(4) : source;
  return labels[name] ?? name.toUpperCase();
}

// "keys:w,a,s,d" -> ["key:w", ...]; any other source is one entry.
export function splitSources(source: string): string[] {
  return source.startsWith('keys:') ? source.slice(5).split(',').map(name => `key:${name.trim()}`) : [source];
}
export function joinStickSources(sources: string[]): string {
  return 'keys:' + sources.map(source => source.slice(4)).join(',');
}
