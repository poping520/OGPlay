// The host's settings decide; the last language and skin are remembered so the splash does not flash the other one.
export function remember(key: string, value: unknown) {
  try { localStorage.setItem(key, String(value)); } catch { /* storage unavailable: the setting still applies */ }
}
export function recall(key: string) {
  try { return localStorage.getItem(key); } catch { return null; /* first run or blocked storage: defaults until settings load */ }
}
