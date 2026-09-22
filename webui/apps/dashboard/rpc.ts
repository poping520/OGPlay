import { DashboardStore, type EventPage, type Snapshot, type Integer } from './store';
export class RpcError extends Error { constructor(readonly code: number, message: string) { super(message); } }
// Preserve 64-bit shared keys without changing JSON strings or floating-point tokens.
export function parseExact(text: string): unknown {
  return JSON.parse(text.replace(/"(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g, token =>
    !token.startsWith('"') && /^-?\d+$/.test(token) && !Number.isSafeInteger(Number(token)) ? `"${token}"` : token));
}
export type Rpc = <T>(method: string, params?: Record<string, unknown>, signal?: AbortSignal) => Promise<T>;
let id = 0;
export const rpc: Rpc = async <T>(method: string, params = {}, signal?: AbortSignal): Promise<T> => {
  const requestId = ++id;
  const response = await fetch('/dash/rpc', {method: 'POST', headers: {'Content-Type': 'application/json', Accept: 'application/json'},
    body: JSON.stringify({jsonrpc: '2.0', id: requestId, method, params}), signal, credentials: 'omit'});
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const text = await response.text();
  if (text.length > 1024 * 1024) throw new Error('响应超过大小限制');
  const value = parseExact(text) as {id?: number; result?: T; error?: {code: number; message: string}};
  if (value.id !== requestId) throw new Error('响应序号不匹配');
  if (value.error) throw new RpcError(value.error.code, value.error.message);
  if (!value.result || (value.result as {schema_version?: number}).schema_version !== 1) throw new Error('不支持的快照版本');
  return value.result;
};
function wireInteger(value: Integer): number {
  const result = Number(value);
  // Current method parameters are numeric JSON. Refuse loss rather than query another key.
  if (!Number.isSafeInteger(result) || result < 0) throw new Error('查询键超出浏览器安全整数范围');
  return result;
}
export class Poller {
  private stopped = true;
  private timer?: ReturnType<typeof setTimeout>;
  private abort?: AbortController;
  constructor(readonly store: DashboardStore, readonly call: Rpc = rpc) {}
  start() { if (!this.stopped) return; this.stopped = false; void this.tick(); }
  stop() { this.stopped = true; clearTimeout(this.timer); this.abort?.abort(); }
  private async tick() {
    const started = performance.now();
    const controller = this.abort = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2500);
    let delay = 125;
    try {
      const snapshot = await this.call<Snapshot>('dash.snapshot', {}, controller.signal);
      if (this.stopped) return;
      this.store.beginStream(snapshot.stream_id);
      const page = await this.call<EventPage>('dash.events', {since_sequence: wireInteger(this.store.cursor), limit: 1000}, controller.signal);
      if (this.stopped) return;
      this.store.publish(snapshot, page);
      const tid = this.store.selection.guestTid;
      if (tid) {
        const focused = await this.call<Snapshot>('dash.thread', {guest_tid: wireInteger(tid)}, controller.signal);
        if (!this.stopped && this.store.selection.guestTid === tid) { this.store.selectedThread = focused; this.store.changed(); }
      }
    } catch (error) {
      if (!this.stopped) this.store.failed(error instanceof Error ? error.message : String(error), error instanceof RpcError && error.code === -32602);
      delay = 1000;
    } finally {
      clearTimeout(timeout);
      if (!this.stopped) this.timer = setTimeout(() => void this.tick(), delay === 125 ? Math.max(0, 125 - (performance.now() - started)) : delay);
    }
  }
}
