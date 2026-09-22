export type Integer = number | string;
export type Status = 'complete' | 'partial' | 'unavailable';
export interface Section<T = unknown> { status: Status; reason: string; generation: Integer; captured_at_steady_ns: Integer; data: T | null }
export interface Session { lifecycle: string; frame: Integer; guest_ticks: Integer; presented_frame: Integer | null; guest_fault: string | null }
export interface Thread { guest_tid: Integer; context_token: Integer; name: string; status?: string; wait_state?: string; frames?: {method: string; dex_pc: number}[] }
export interface Execution { guest_tid: Integer; context_token: Integer; host_tid: Integer; name: string; arm_pc: number; active: boolean }
export interface Diagnostics { java_threads: Thread[]; executions: Execution[]; sections: {name: string; status: Status; reason: string}[] }
export interface Snapshot { schema_version: 1; stream_id?: Integer; captured_at_steady_ns: Integer; session?: Section<Session>; diagnostics?: Section<Diagnostics>; [key: string]: unknown }
export interface Event { sequence: Integer; kind: string; frame: Integer | null; steady_ns: Integer | null; guest_tid: Integer | null; context_token: Integer | null; detail?: string; method?: string; lifecycle_phase?: string; syscall_nr?: number; result?: number; capability?: string }
export interface EventPage { schema_version: 1; events: Event[]; next_sequence: Integer; latest_sequence: Integer; dropped: Integer; gap: boolean; status: Status; supported_kinds: string[]; syscalls_source_dropped?: Integer | null; native_source_dropped?: Integer | null }
export interface Selection { guestTid?: string; contextToken?: string; frame?: string; capability?: string }
export const key = (value: Integer | null | undefined) => value == null ? undefined : String(value);
export function matches(event: Event, selection: Selection): boolean {
  if (selection.guestTid && key(event.guest_tid) !== selection.guestTid &&
      (!selection.contextToken || key(event.context_token) !== selection.contextToken)) return false;
  if (selection.frame && key(event.frame) !== selection.frame) return false;
  if (selection.capability && event.capability !== selection.capability) return false;
  return true;
}
export function threads(snapshot: Snapshot | null): Thread[] {
  const diagnostic = snapshot?.diagnostics;
  if (!diagnostic || diagnostic.status === 'unavailable' || !diagnostic.data) return [];
  const rows = new Map<string, Thread>();
  for (const t of diagnostic.data.java_threads) rows.set(String(t.guest_tid), t);
  for (const e of diagnostic.data.executions) if (!rows.has(String(e.guest_tid)))
    rows.set(String(e.guest_tid), {...e, status: e.active ? 'native active' : 'native returned'});
  return [...rows.values()];
}
export class DashboardStore {
  latest: Snapshot | null = null;
  history: Snapshot[] = [];
  events: Event[] = [];
  selection: Selection = {};
  cursor: Integer = 0;
  streamId?: string;
  dropped: Integer = 0;
  sourceDropped: string = '—';
  gap = false;
  connection = 'connecting';
  error = '';
  selectedThread: Snapshot | null = null;
  readonly listeners = new Set<() => void>();
  constructor(readonly capacity = 600) {}
  subscribe = (listener: () => void) => { this.listeners.add(listener); return () => { this.listeners.delete(listener); }; };
  changed() { this.listeners.forEach(fn => fn()); }
  select(selection: Selection) { this.selection = selection; this.selectedThread = null; this.changed(); }
  beginStream(id: Integer | undefined) {
    if (id === undefined) return;
    if (this.streamId !== undefined && this.streamId !== String(id)) {
      this.cursor = 0; this.events = []; this.history = []; this.selection = {}; this.selectedThread = null; this.gap = false;
    }
    this.streamId = String(id);
  }
  publish(snapshot: Snapshot, page: EventPage) {
    this.latest = snapshot;
    this.history = [...this.history, snapshot].slice(-this.capacity);
    const seen = new Set(this.events.map(e => String(e.sequence)));
    this.events = [...this.events, ...page.events.filter(e => !seen.has(String(e.sequence)))].slice(-4096);
    this.cursor = page.next_sequence; this.dropped = page.dropped; this.gap ||= page.gap;
    this.sourceDropped = `${page.syscalls_source_dropped ?? '—'} / ${page.native_source_dropped ?? '—'}`;
    this.connection = 'connected'; this.error = ''; this.changed();
  }
  failed(message: string, resetCursor = false) {
    this.latest = null; this.selectedThread = null; this.connection = 'disconnected'; this.error = message;
    if (resetCursor) { this.cursor = 0; this.events = []; this.history = []; this.selection = {}; this.gap = false; }
    this.changed();
  }
}
