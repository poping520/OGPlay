import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { rpc, type Library, type LibraryItem } from '../shared/rpc';
import { gameName, resolvedLanguage, setLanguage, t, useLanguage } from '../shared/i18n';
import { recall, remember } from '../shared/prefs';
import { has } from '../shared/features';
import { validatePackageSelection, visibleItems, type Filter, type Sort } from './library';
import type { Settings } from '../settings/settings';
import type { DataHint } from '../import/import';
import { skinOf } from '../kit';
import { useCart3D, type CartMode } from '../cartridge/cartridge3d';

// Launcher state both layouts share (the console screen in app.tsx, the phone screens in phone/): the library and its
// filters, settings and skin, the 3D cartridge bundle, running instances, entry actions, and the import, data-hint,
// removal and clear-data flows with their errors. Each layout keeps only how it shows them.
export interface DashboardInfo { installation_id: string; process_id: number; port: number | null; status: string; detail: string; key_editor_status: string; key_editor_detail: string }

export function useLauncher() {
  const lang = useLanguage();
  const [hostLibrary, setLibrary] = useState<Library>({ items: [], library_root: '' });
  // Names follow the launcher language everywhere below: tiles, cartridges, dialogs, sorting.
  const library = useMemo(() => ({ ...hostLibrary, items: hostLibrary.items.map(game => ({ ...game, display_name: gameName(game, lang) })) }),
    [hostLibrary, lang]);
  const [dashboards, setDashboards] = useState<DashboardInfo[]>([]);
  const [removing, setRemoving] = useState<LibraryItem | null>(null);
  const [clearing, setClearing] = useState<LibraryItem | null>(null);
  const [dataHint, setDataHint] = useState<{ id: string; hint: DataHint } | null>(null);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [settingsFailed, setSettingsFailed] = useState(false);
  const settingsRef = useRef<Settings | null>(null);
  const [query, setQuery] = useState('');
  const [filter, setFilter] = useState<Filter>('all');
  const [sort, setSort] = useState<Sort>('name');
  const [errors, setErrors] = useState<string[]>([]);
  const [pendingFile, setPendingFile] = useState<File | null>(null);
  const [importing, setImporting] = useState(false);
  const [busy, setBusy] = useState(false);
  const [loading, setLoading] = useState(true);
  const inFlight = useRef(false);
  const generation = useRef(0);
  const items = useMemo(() => visibleItems(library.items, query, filter, sort), [library.items, query, filter, sort]);
  const report = (reason: unknown) => setErrors(queue => [...queue, reason instanceof Error ? reason.message : String(reason)]);
  const applySettings = (value: Settings) => {
    settingsRef.current = value; setSettings(value); applyLanguage(value); remember('ogplay.skin', value.values.settings_style);
  };
  const skin = settings ? skinOf(settings.values.settings_style) : rememberedSkin;
  // Pending until settings load, so a CSS preference never starts loading the 3D bundle; unreadable settings use the default.
  const cartMode: CartMode = settings ? (settings.values.cartridge === 'css' ? 'css' : '3d') : settingsFailed ? '3d' : 'pending';
  const cart3d = useCart3D(cartMode, report);
  // The cartridge viewer needs the 3D bundle; with the CSS cartridge its entries stay disabled and say why.
  const viewer = cart3d && cart3d !== 'waiting' ? cart3d : null;
  async function refresh() {
    const request = ++generation.current;
    try {
      const result = await rpc<Library>('library.list');
      if (request === generation.current) setLibrary(result);
    } finally { if (request === generation.current) setLoading(false); }
  }
  useEffect(() => {
    let disposed = false;
    const initial = async () => {
      for (let i = 0; i < Math.max(1, window.__ogplaySmoke ?? 0) && !disposed; ++i) {
        await refresh();
        await new Promise<void>(resolve => requestAnimationFrame(() => resolve()));
      }
    };
    void initial().catch(report);
    void rpc<Settings>('settings.get').then(value => { if (!disposed) applySettings(value); })
      .catch(reason => { if (!disposed) setSettingsFailed(true); report(reason); });
    const exited = (event: Event) => {
      const detail = (event as CustomEvent<{ installation_id: string; exit_code: number; log_tail: string }>).detail;
      setDashboards(current => current.filter(instance => instance.installation_id !== detail.installation_id));
      if (detail.exit_code !== 0 && settingsRef.current?.values.show_exit_log !== false) report(`${t('{id} 退出码 {code}', { id: detail.installation_id, code: detail.exit_code })}\n${detail.log_tail}`);
      void refresh().catch(report);
      // ADR-0146: what the run failed to find may point at a better data mount.
      void rpc<DataHint>('library.data_hint', { installation_id: detail.installation_id })
        .then(hint => { if (!disposed && hint.kind !== 'none') setDataHint({ id: detail.installation_id, hint }); })
        .catch(() => { /* a missing or unreadable report only means there is nothing to suggest */ });
    };
    let polling = false;
    const poll = async () => {
      if (polling || disposed) return;
      polling = true;
      try { const result = await rpc<{ items: DashboardInfo[] }>('dashboard.list'); if (!disposed) setDashboards(result.items); }
      catch { /* Host errors are reported by explicit actions; retry on the next poll. */ }
      finally { polling = false; }
    };
    // Running instances are tracked for their Dashboard and key editor; a host without either has nothing to list.
    const tracked = !__OGPLAY_PHONE__ && (has('dashboard') || has('key_editor'));
    if (tracked) void poll();
    const timer = tracked ? window.setInterval(() => void poll(), 1000) : 0;
    const dashboardError = (event: Event) => report(t((event as CustomEvent<string>).detail));
    window.addEventListener('ogplay-dashboard-error', dashboardError);
    window.addEventListener('ogplay-exit', exited);
    return () => { disposed = true; window.clearInterval(timer); window.removeEventListener('ogplay-dashboard-error', dashboardError); ++generation.current; window.removeEventListener('ogplay-exit', exited); };
  }, []);
  // One entry action at a time; the library is read again after it. False when the host refused (already reported).
  async function act(method: string, game: LibraryItem, kind?: string, extra: Record<string, unknown> = {}) {
    if (inFlight.current) return false;
    inFlight.current = true; setBusy(true);
    try {
      await rpc(method, { installation_id: game.installation_id, ...(kind ? { kind } : {}), ...(method === "library.remove" ? { confirmed: true } : {}), ...extra });
      await refresh();
      return true;
    } catch (reason) { report(reason); return false; }
    finally { inFlight.current = false; setBusy(false); }
  }
  const remove = async (game: LibraryItem, withSaves = false) => {
    const done = await act('library.remove', game, undefined, withSaves ? { delete_saves: true } : {});
    if (done) setRemoving(null);
    return done;
  };
  // Always confirmed in its dialog: unlike removal, cleared saves cannot be restored by a re-import.
  const clearData = async (game: LibraryItem) => {
    if (await act('library.clear_data', game, undefined, { confirmed: true })) {
      setClearing(null); report(t('已清空「{name}」的游戏数据。', { name: game.display_name }));
    }
  };
  const requestRemoval = (game: LibraryItem) => { if (!game.running && !busy && !importing) setRemoving(game); };
  const intake = (files: File[]) => {
    const error = validatePackageSelection(files);
    if (importing) return;
    if (error) report(error); else { setPendingFile(files[0]); setImporting(true); }
  };
  const startImport = () => { setPendingFile(null); setImporting(true); };
  const closeImport = () => { setImporting(false); setPendingFile(null); };
  // A new entry is shown whatever the filters were; the layout then selects it.
  const imported = async () => { setQuery(''); setFilter('all'); await refresh(); closeImport(); };
  const resetFilters = () => { setQuery(''); setFilter('all'); };
  const applyDataHint = async (relaunch: boolean) => {
    if (!dataHint) return;
    const id = dataHint.id;
    setDataHint(null); await refresh();
    if (relaunch) await rpc('library.launch', { installation_id: id });
  };
  return {
    library, items, loading, refresh, act, busy,
    query, setQuery, filter, setFilter, sort, setSort, resetFilters,
    settings, applySettings, skin, cartMode, cart3d, viewer,
    dashboards, errors, report, dismissError: () => setErrors(queue => queue.slice(1)),
    importing, pendingFile, startImport, intake, closeImport, imported,
    dataHint, closeDataHint: () => setDataHint(null), applyDataHint,
    removing, requestRemoval, cancelRemoval: () => setRemoving(null), remove,
    clearing, setClearing, clearData,
  };
}
export type Launcher = ReturnType<typeof useLauncher>;

// The host's settings decide; the last language and skin are remembered so the splash does not flash the other one.
function applyLanguage(settings: Settings) { const value = resolvedLanguage(settings.values.language, settings.system_language); setLanguage(value); remember('ogplay.language', value); }
const rememberedSkin = skinOf(recall('ogplay.skin'));
