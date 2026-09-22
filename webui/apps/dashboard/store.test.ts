import { identitySelection } from './store';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {DashboardStore, matches, matchesFact, selectedSnapshot, health, threads, type Snapshot, type Event, type EventPage} from './store';
import {parseExact, Poller, RpcError, type Rpc} from './rpc';
const snapshot: Snapshot = {schema_version: 1, captured_at_steady_ns: 100,
  session: {status: 'complete', reason: '', generation: 0, captured_at_steady_ns: 100, data: {frame: 5, guest_ticks: 5000, lifecycle: 'running', presented_frame: null, guest_fault: null}},
  diagnostics: {status: 'partial', reason: '', generation: 1, captured_at_steady_ns: 100, data: {
    java_threads: [{guest_tid: 7, context_token: 11, name: 'worker'}], executions: [{guest_tid: 7, context_token: 11, host_tid: 3, name: 'call', arm_pc: 123, active: true}], sections: []}}};
const event: Event = {sequence: 1, kind: 'native', frame: null, steady_ns: null, guest_tid: 7, context_token: 11};
const page: EventPage = {schema_version: 1, events: [event], next_sequence: 1, latest_sequence: 1, dropped: 0, gap: false, status: 'partial', supported_kinds: ['native']};
afterEach(() => vi.useRealTimers());
describe('Dashboard stores and polling', () => {
  it('filters by shared keys without assigning missing frames', () => {
    expect(matches(event, {guestTid: '7'})).toBe(true);
    expect(matches({...event, guest_tid: null}, {guestTid: '7', contextToken: '11'})).toBe(true);
    expect(matches(event, {guestTid: '8', contextToken: '12'})).toBe(false);
    expect(matches(event, {frame: '5'})).toBe(false);
    expect(matches({...event, frame: 5}, {frame: '5'})).toBe(true);
    expect(matches(event, {capability: 'missing'})).toBe(false);
  });
  it('joins threads and does not retain stale source values', () => {
    expect(threads(snapshot)).toHaveLength(1);
    expect(threads({...snapshot, diagnostics: {...snapshot.diagnostics!, status: 'unavailable', data: null}})).toEqual([]);
    const store = new DashboardStore(); store.publish(snapshot, page); store.failed('offline');
    expect(store.latest).toBeNull(); expect(threads(store.latest)).toEqual([]);
    expect(store.history).toHaveLength(1); expect(store.connection).toBe('disconnected');
  });
  it('bounds history, deduplicates events and exposes coverage loss', () => {
    const store = new DashboardStore(2);
    for (let i = 0; i < 3; i++) store.publish(snapshot, page);
    expect(store.history).toHaveLength(2); expect(store.events).toHaveLength(1);
    store.publish(snapshot, {...page, events: Array.from({length: 4100}, (_, i) => ({...event, sequence: i + 2})), dropped: 9, gap: true});
    expect(store.events).toHaveLength(4096); expect(store.gap).toBe(true); expect(store.dropped).toBe(9);
    store.failed('cursor reset', true); expect(store.cursor).toBe(0); expect(store.events).toEqual([]); expect(store.history).toEqual([]);
  });
  it('preserves large integer shared keys without rewriting strings or floats', () => {
    expect(parseExact('{"id":9007199254740993,"text":"9007199254740993","value":1.5,"exp":1e3,"quote":"\\\"12"}'))
      .toEqual({id: '9007199254740993', text: '9007199254740993', value: 1.5, exp: 1000, quote: '"12'});
  });
  it('serializes polls and stops cleanly', async () => {
    vi.useFakeTimers();
    const store = new DashboardStore();
    let resolveSnapshot!: (value: Snapshot) => void;
    const call = vi.fn((method: string) => method === 'dash.snapshot' ? new Promise<Snapshot>(r => {resolveSnapshot = r;}) : Promise.resolve(page));
    const poller = new Poller(store, call as Rpc); poller.start();
    await vi.advanceTimersByTimeAsync(1000); expect(call).toHaveBeenCalledTimes(1);
    resolveSnapshot(snapshot); await vi.advanceTimersByTimeAsync(0);
    expect(store.latest).toEqual(snapshot); expect(call).toHaveBeenCalledTimes(3);
    poller.stop(); resolveSnapshot(snapshot); await vi.advanceTimersByTimeAsync(1000); expect(call).toHaveBeenCalledTimes(3);
  });
  it('does not publish a late response after the selected thread changes', async () => {
    vi.useFakeTimers(); const store = new DashboardStore(); store.select({guestTid: '7'});
    let resolveThread!: (value: Snapshot) => void;
    const call = vi.fn((method: string) => method === 'dash.thread' ? new Promise<Snapshot>(resolve => {resolveThread = resolve;}) : Promise.resolve(method === 'dash.snapshot' ? snapshot : page));
    const poller = new Poller(store, call as Rpc); poller.start(); await vi.advanceTimersByTimeAsync(0);
    store.select({guestTid: '8'}); resolveThread(snapshot); await vi.advanceTimersByTimeAsync(0);
    expect(store.selectedThread).toBeNull(); expect(store.selection.guestTid).toBe('8'); poller.stop();
  });
  it('resets history and cursors when a different runtime stream reconnects', () => {
    const store = new DashboardStore(); store.beginStream('100'); store.publish(snapshot, page);
    store.select({guestTid: '7'}); store.beginStream('200');
    expect(store.cursor).toBe(0); expect(store.history).toEqual([]); expect(store.events).toEqual([]); expect(store.selection).toEqual({});
  });
  it('resets an obsolete cursor before reconnecting', async () => {
    vi.useFakeTimers(); const store = new DashboardStore(); store.cursor = 50;
    const call = vi.fn((method: string) => method === 'dash.events' ? Promise.reject(new RpcError(-32602, 'cursor ahead')) : Promise.resolve(snapshot));
    const poller = new Poller(store, call as Rpc); poller.start(); await vi.advanceTimersByTimeAsync(0);
    expect(store.cursor).toBe(0); expect(store.connection).toBe('disconnected');
    expect(store.latest).toBeNull(); poller.stop();
  });
  it('aborts slow requests and retries without overlapping', async () => {
    vi.useFakeTimers(); const store = new DashboardStore();
    const call = vi.fn((_m: string, _p?: unknown, signal?: AbortSignal) => new Promise((_resolve, reject) => signal?.addEventListener('abort', () => reject(new Error('timeout')))));
    const poller = new Poller(store, call as Rpc); poller.start(); await vi.advanceTimersByTimeAsync(2500);
    expect(store.connection).toBe('disconnected'); expect(call).toHaveBeenCalledTimes(1);
    await vi.advanceTimersByTimeAsync(1000); expect(call).toHaveBeenCalledTimes(2); poller.stop();
  });
});

it('filters only exact structured shared keys and keeps large identities distinct', () => {
  const fact = {guest_tid:'9007199254740993',host_tid:9,fd:3,node_id:21,player:5,lifecycle_phase:'resumed',generation:7,fields:[{key:'capability',value:'jni.missing'}]};
  expect(matchesFact(fact,{guestTid:'9007199254740993',hostTid:'9',fd:'3',nodeId:'21',player:'5',lifecycle:'resumed',generation:'7',capability:'jni.missing'})).toBe(true);
  expect(matchesFact(fact,{guestTid:'9007199254740992'})).toBe(false);
  expect(matchesFact({message:'jni.missing'}, {capability:'jni.missing'})).toBe(false);
  expect(matchesFact({steady_ns:99},{frame:'5',steadyStart:'90',steadyEnd:'100'})).toBe(true);
  expect(matchesFact({steady_ns:101},{frame:'5',steadyStart:'90',steadyEnd:'100'})).toBe(false);
  expect(matchesFact({},{fd:'3'})).toBe(false);
});
it('frame selection uses historical facts and never substitutes the live snapshot', () => {
  const store=new DashboardStore(); store.history=[snapshot]; store.latest={...snapshot,session:{...snapshot.session!,data:{...snapshot.session!.data!,frame:6}}};
  store.select({frame:'5'}); expect(selectedSnapshot(store)).toBe(snapshot);
  store.select({frame:'3'}); expect(selectedSnapshot(store)).toBeNull();
  store.select({}); expect(selectedSnapshot(store)).toBe(store.latest);
});

it('module warning lights follow their own facts and unavailable never looks healthy', () => {
  expect(health()).toBe('unavailable');
  const section={status:'complete' as const,reason:'',generation:0,captured_at_steady_ns:0,data:{gl_errors:2}};
  expect(health(section)).toBe('warning'); expect(health({...section,status:'unavailable'})).toBe('unavailable');
  expect(health({...section,data:{gl_errors:0}})).toBe('available');
});

it('maps host and monitor owner identities through actual execution contexts', () => {
  const diagnostic = {java_threads:[{guest_tid:2,context_token:12}],executions:[{guest_tid:16384,context_token:12,host_tid:77}]};
  expect(identitySelection('host_tid',77,diagnostic)).toEqual({guestTid:'2',contextToken:'12',hostTid:'77'});
  expect(identitySelection('owner_context',12,diagnostic)).toEqual({guestTid:'2',contextToken:'12'});
  expect(identitySelection('guest_tid',0,diagnostic)).toEqual({});
  expect(identitySelection('fd',0,diagnostic)).toEqual({fd:'0'});
  expect(matchesFact({futex_address:42},{futex:'42'})).toBe(true);
  expect(matchesFact({},{monitor:'42'})).toBe(false);
});
