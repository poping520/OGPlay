export type Integer = number | string;
export type Status = 'complete' | 'partial' | 'unavailable';
export interface Section<T = unknown> { status: Status; reason: string; generation: Integer; captured_at_steady_ns: Integer; data: T | null }
export interface Session { lifecycle: string; frame: Integer; guest_ticks: Integer; presented_frame: Integer | null; guest_fault: string | null }
export interface Thread { guest_tid: Integer; context_token: Integer; name: string; status?: string; wait_state?: string; frames?: {method: string; dex_pc: number}[] }
export interface Execution { guest_tid: Integer; context_token: Integer; host_tid: Integer; name: string; arm_pc: number; active: boolean }
export interface Diagnostics { java_threads: Thread[]; executions: Execution[]; sections: {name: string; status: Status; reason: string}[] }
export interface Snapshot { schema_version: 1; stream_id?: Integer; captured_at_steady_ns: Integer; session?: Section<Session>; diagnostics?: Section<Diagnostics>; [key: string]: unknown }
export interface Event { sequence: Integer; kind: string; frame: Integer | null; steady_ns: Integer | null; guest_tid: Integer | null; context_token: Integer | null; detail?: string; method?: string; lifecycle_phase?: string; syscall_nr?: number; result?: number; capability?: string; player?: Integer; generation?: Integer; observed_only?: boolean; observed_at_steady_ns?: Integer; delta?: Integer }
export interface EventPage { schema_version: 1; events: Event[]; next_sequence: Integer; latest_sequence: Integer; dropped: Integer; gap: boolean; status: Status; supported_kinds: string[]; syscalls_source_dropped?: Integer | null; native_source_dropped?: Integer | null }
export interface Selection { snapshotTime?: string; kind?: string; guestTid?: string; contextToken?: string; hostTid?: string; frame?: string; capability?: string; fd?: string; nodeId?: string; player?: string; lifecycle?: string; generation?: string; monitor?: string; futex?: string; steadyStart?: string; steadyEnd?: string }
export type Fact = Record<string, unknown>;
export const facts = (value: unknown): Fact[] => Array.isArray(value) ? value.filter(v => v !== null && typeof v === 'object') as Fact[] : [];
export function sectionData(snapshot: Snapshot | null, name: string): unknown {
  const section = snapshot?.[name] as Section | undefined;
  return section?.status === 'unavailable' ? null : section?.data ?? null;
}
export function selectedSnapshot(store: DashboardStore): Snapshot | null {
  if (!store.selection.frame) return store.latest;
  return [...store.history].reverse().find(s => store.selection.snapshotTime ? key(s.captured_at_steady_ns) === store.selection.snapshotTime : key(s.session?.data?.frame) === store.selection.frame) ?? null;
}
export function identitySelection(column: string, value: unknown, diagnostic: Fact | null): Selection {
  if (value == null) return {};
  const id = String(value);
  if (['guest_tid','context_token','owner_context','host_tid'].includes(column)) {
    if (id === '0') return {};
    const field = column === 'owner_context' ? 'context_token' : column;
    const source = [...facts(diagnostic?.java_threads), ...facts(diagnostic?.executions)].find(r => String(r[field]) === id);
    const context = source?.context_token ?? (field === 'context_token' ? id : undefined);
    const java = context && facts(diagnostic?.java_threads).find(r => String(r.context_token) === String(context));
    return {guestTid: key((java?.guest_tid ?? source?.guest_tid ?? (field === 'guest_tid' ? id : undefined)) as Integer | undefined),
      contextToken: context && String(context) !== '0' ? String(context) : undefined,
      ...(field === 'host_tid' ? {hostTid:id} : {})};
  }
  const fields: Record<string,keyof Selection> = {fd:'fd',node_id:'nodeId',player:'player',id:'capability',address:'futex',object:'monitor',lifecycle_phase:'lifecycle',generation:'generation'};
  return fields[column] ? {[fields[column]]:id} : {};
}
export function matchesFact(row: Fact, selection: Selection): boolean {
  const fields = Object.fromEntries(facts(row.fields).map(f => [String(f.key), f.value]));
  const value = {...fields, ...row};
  const exact = (field: string, selected?: string) => !selected || value[field] != null && String(value[field]) === selected;
  if (selection.guestTid && !exact('guest_tid', selection.guestTid) && !(selection.contextToken && exact('context_token', selection.contextToken))) return false;
  if (selection.contextToken && !selection.guestTid && !exact('context_token', selection.contextToken)) return false;
  if (selection.frame && value.frame == null && selection.steadyStart && selection.steadyEnd) {
    if (value.steady_ns == null || BigInt(String(value.steady_ns)) < BigInt(selection.steadyStart) || BigInt(String(value.steady_ns)) > BigInt(selection.steadyEnd)) return false;
  } else if (!exact('frame', selection.frame)) return false;
  return exact('kind', selection.kind) && (value.host_tid == null && !!selection.guestTid || exact('host_tid', selection.hostTid)) && exact('capability', selection.capability) && exact('fd', selection.fd) && exact('node_id', selection.nodeId) && exact('player', selection.player) && exact('lifecycle_phase', selection.lifecycle) && exact('generation', selection.generation) && exact('monitor_object', selection.monitor) && exact('futex_address', selection.futex);
}
export const key = (value: Integer | null | undefined) => value == null ? undefined : String(value);
export function matches(event: Event, selection: Selection): boolean {
  return matchesFact(event as unknown as Fact, selection);
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

export function health(section?: Section): 'unavailable' | 'warning' | 'available' {
  if (!section || section.status === 'unavailable' || section.data == null) return 'unavailable';
  const data=section.data as Fact;
  if (Number(data.gl_errors)>0 || Number(data.heap_used)>Number(data.heap_target)) return 'warning';
  if (Array.isArray(section.data) && facts(section.data).some(r => Number(r.underrun_count)>0 || Number(r.count)>0)) return 'warning';
  return 'available';
}
