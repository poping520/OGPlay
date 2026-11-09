import { Brand, skinOf } from './kit';
import { Key } from '../../packages/ui-kit/key';
import { Glyph } from '../../packages/ui-kit/glyph';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { rpc, type Library, type LibraryItem } from './shared/rpc';
import { gameName, resolvedLanguage, setLanguage, t, useLanguage } from './shared/i18n';
import { recall, remember } from './shared/prefs';
import { cycle, externalText, filters, lastRunText, profileText, statusDetail, statusOf, validatePackageSelection, versionLabel, visibleItems, type Filter, type Sort } from './library/library';
import { SettingsPage } from './settings/settings-page';
import { GameSettingsPage } from './settings/game-settings-page';
import { CartViewer } from './cartridge/cart-viewer';
import type { Settings } from './settings/settings';
import { ImportWizard } from './import/import-wizard';
import { DataHintDialog } from './import/data-hint';
import { ClearDataDialog, InstallInfoDialog, RemoveGameDialog } from './library/game-dialogs';
import type { DataHint } from './import/import';
import { Backdrop, Cartridge } from './cartridge/cartridge';
import { Hero3D, TileCart, useCart3D, type CartMode } from './cartridge/cartridge3d';
import { Modal } from './library/modal';
import { Splash } from './library/splash';
import { Clock } from './library/clock';
import { SortMenu } from './library/sort-menu';
import './library/library.css';

// Launcher shell: owns the library state, shows the library screen and hands the window to a full-screen view
// (global or per-game settings, cartridge viewer) while one is open.
interface DashboardInfo { installation_id: string; process_id: number; port: number | null; status: string; detail: string; key_editor_status: string; key_editor_detail: string }

export const editable = (target: EventTarget | null) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName));
export function App() {
  const lang = useLanguage();
  const [hostLibrary, setLibrary] = useState<Library>({ items: [], library_root: '' });
  // Names follow the launcher language everywhere below: tiles, cartridges, dialogs, sorting.
  const library = useMemo(() => ({ ...hostLibrary, items: hostLibrary.items.map(game => ({ ...game, display_name: gameName(game, lang) })) }),
    [hostLibrary, lang]);
  const [dashboards, setDashboards] = useState<DashboardInfo[]>([]);
  const [removing, setRemoving] = useState<LibraryItem | null>(null);
  const [clearing, setClearing] = useState<LibraryItem | null>(null);
  const [dataHint, setDataHint] = useState<{ id: string; hint: DataHint } | null>(null);
  const [dashboardListOpen, setDashboardListOpen] = useState(false);
  const [infoOpen, setInfoOpen] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [searchOpen, setSearchOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [viewing, setViewing] = useState(false);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [settingsFailed, setSettingsFailed] = useState(false);
  const [splash, setSplash] = useState(true);
  const [heroShown, setHeroShown] = useState(false);
  const readyTiles = useRef(new Set<string>());
  const [, setReadyTileCount] = useState(0);
  const settingsRef = useRef<Settings | null>(null);
  const [selected, setSelected] = useState('');
  const [query, setQuery] = useState('');
  const [filter, setFilter] = useState<Filter>('all');
  const [sort, setSort] = useState<Sort>('name');
  const [gameSettingsId, setGameSettingsId] = useState('');
  const [errors, setErrors] = useState<string[]>([]);
  const [pendingFile, setPendingFile] = useState<File | null>(null);
  const [importing, setImporting] = useState(false);
  const [dragging, setDragging] = useState(false);
  const [busy, setBusy] = useState(false);
  const [loading, setLoading] = useState(true);
  const inFlight = useRef(false);
  const generation = useRef(0);
  const search = useRef<HTMLInputElement>(null);
  const strip = useRef<HTMLDivElement>(null);
  const primary = useRef<HTMLButtonElement>(null);
  const dragDepth = useRef(0);
  const items = useMemo(() => visibleItems(library.items, query, filter, sort), [library.items, query, filter, sort]);
  const item = items.find(game => game.installation_id === selected) ?? items[0];
  const instance = item && dashboards.find(value => value.installation_id === item.installation_id);
  const runningCount = library.items.filter(game => game.running).length;
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
  const tileReady = (id: string) => { if (!readyTiles.current.has(id)) { readyTiles.current.add(id); setReadyTileCount(readyTiles.current.size); } };
  const startReady = !loading && cartMode !== 'pending' && cart3d !== 'waiting' && (cart3d === null ||
    (!item || heroShown) && items.slice(0, 12).every(game => readyTiles.current.has(game.installation_id)));
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
    void poll();
    const timer = window.setInterval(() => void poll(), 1000);
    const dashboardError = (event: Event) => report(t((event as CustomEvent<string>).detail));
    window.addEventListener('ogplay-dashboard-error', dashboardError);
    window.addEventListener('ogplay-exit', exited);
    return () => { disposed = true; window.clearInterval(timer); window.removeEventListener('ogplay-dashboard-error', dashboardError); ++generation.current; window.removeEventListener('ogplay-exit', exited); };
  }, []);
  useEffect(() => { setMenuOpen(false); }, [item?.installation_id]);
  useEffect(() => {
    // Scroll only the strip: scrollIntoView would also shift the page once the strip hits its end.
    const rail = strip.current, tile = rail?.querySelector<HTMLElement>('.tile[aria-pressed=true]');
    if (!rail || !tile) return;
    const offset = tile.getBoundingClientRect().left - rail.getBoundingClientRect().left;
    rail.scrollTo({ left: rail.scrollLeft + offset - (rail.clientWidth - tile.offsetWidth) / 2, behavior: 'smooth' });
  }, [item?.installation_id, filter]);
  async function act(method: string, game: LibraryItem, kind?: string, extra: Record<string, unknown> = {}) {
    if (inFlight.current) return;
    inFlight.current = true; setBusy(true);
    try {
      await rpc(method, { installation_id: game.installation_id, ...(kind ? { kind } : {}), ...(method === "library.remove" ? { confirmed: true } : {}), ...extra });
      await refresh();
      return true;
    } catch (reason) { report(reason); return false; }
    finally { inFlight.current = false; setBusy(false); }
  }
  const remove = async (game: LibraryItem, withSaves = false) => {
    if (await act('library.remove', game, undefined, withSaves ? { delete_saves: true } : {})) { setRemoving(null); setSelected(''); }
  };
  // Always confirmed in its dialog: unlike removal, cleared saves cannot be restored by a re-import.
  const clearData = async (game: LibraryItem) => {
    if (await act('library.clear_data', game, undefined, { confirmed: true })) {
      setClearing(null); report(t('已清空「{name}」的游戏数据。', { name: game.display_name }));
    }
  };
  const requestRemoval = (game: LibraryItem) => {
    setMenuOpen(false);
    if (game.running || busy || importing) return;
    setRemoving(game);
  };
  const intake = (files: File[]) => {
    const error = validatePackageSelection(files);
    if (importing) return;
    if (error) report(error); else { setPendingFile(files[0]); setImporting(true); }
  };
  const startImport = () => { setPendingFile(null); setImporting(true); };
  const isFileDrag = (event: DragEvent) => Array.from(event.dataTransfer?.types ?? []).includes('Files');
  const resetFilters = () => { setQuery(''); setFilter('all'); setSearchOpen(false); };
  const openSearch = () => { setSearchOpen(true); requestAnimationFrame(() => search.current?.focus()); };
  const editorReady = instance?.key_editor_status === 'ready';
  const openControls = (game: LibraryItem) => { if (dashboards.find(value => value.installation_id === game.installation_id)?.key_editor_status === 'ready') void act('controls.open', game); };
  const openViewer = () => { if (viewer) { setMenuOpen(false); setViewing(true); } };
  const openGameSettings = (game: LibraryItem) => { if (!busy && !importing) { setMenuOpen(false); setGameSettingsId(game.installation_id); } };

  // Keyboard stands in for the gamepad legend: ←/→ select, Enter = A, Esc = B, X/Y shortcuts, Q/E = L/R; ⌘K/Ctrl+K
  // searches and ⌘O/Ctrl+O imports from anywhere in the library.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (settingsOpen || gameSettingsId || viewing) return;   // full-screen views own the keyboard while open
      if (event.key === 'Escape') {
        if (menuOpen) { setMenuOpen(false); return; }
        if (searchOpen && !query && document.activeElement === search.current) { setSearchOpen(false); search.current?.blur(); }
        else if (!item && !document.querySelector('dialog[open]') && (query || filter !== 'all')) resetFilters();
        return;
      }
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') { event.preventDefault(); openSearch(); return; }
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'o') {
        event.preventDefault();
        if (!importing && !document.querySelector('dialog[open]')) startImport();
        return;
      }
      if (event.metaKey || event.ctrlKey || event.altKey || editable(event.target) || document.querySelector('dialog[open]') || importing) return;
      const target = event.target as HTMLElement | null;
      const onTile = !!target?.closest?.('.tile');
      if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
        if (!items.length) return;
        event.preventDefault();
        // Functional update so key repeat before the next render still advances one step per press.
        setSelected(current => {
          const next = cycle(items, items.find(game => game.installation_id === current) ?? items[0], event.key === 'ArrowRight' ? 1 : -1).installation_id;
          if (onTile) requestAnimationFrame(() => strip.current?.querySelector<HTMLElement>(`.tile[data-instance="${CSS.escape(next)}"]`)?.focus());
          return next;
        });
      } else if (event.key === 'Enter' && (onTile || target === document.body)) {
        event.preventDefault(); primary.current?.click();
      } else if (event.key === 'q' || event.key === 'e') {
        setFilter(current => cycle(filters.map(([value]) => value), current, event.key === 'e' ? 1 : -1));
      } else if (event.key === 'x' && item) openControls(item);
      else if (event.key === 'y' && item) openGameSettings(item);
      else if (event.key === 'v' && item) openViewer();
      else if (event.key === '/') { event.preventDefault(); openSearch(); }
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });
  useEffect(() => {
    if (!menuOpen) return;
    const close = (event: PointerEvent) => { if (!(event.target as HTMLElement).closest?.('.more')) setMenuOpen(false); };
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [menuOpen]);

  if (viewing && viewer && item) return <CartViewer api={viewer} items={items} selected={item} skin={skin} select={setSelected} onBack={() => setViewing(false)} />;
  if (settingsOpen) return <SettingsPage onBack={() => { setSettingsOpen(false); void refresh().catch(report); }} onSaved={applySettings} />;
  if (gameSettingsId) return <GameSettingsPage id={gameSettingsId} item={library.items.find(game => game.installation_id === gameSettingsId)}
    skin={skin} cart3d={cart3d}
    onBack={() => { setGameSettingsId(''); void refresh().catch(report); }}
    clearData={() => { const game = library.items.find(entry => entry.installation_id === gameSettingsId); setGameSettingsId(''); if (game) setClearing(game); }} />;

  const filterLabel = t(filters.find(([value]) => value === filter)![1]);
  const moreMenu = (game: LibraryItem) => <div class="more">
    <button class="pill" aria-haspopup="menu" aria-expanded={menuOpen} onClick={() => setMenuOpen(open => !open)}><Key>…</Key>{t('更多')}</button>
    {menuOpen && <div class="menu" role="menu">
      {game.running && <button role="menuitem" disabled={busy || importing} onClick={() => openGameSettings(game)}>{t('游戏设置')}<small>{t('下次启动生效')}</small></button>}
      <button role="menuitem" disabled={!viewer} onClick={openViewer}>{t('查看卡带')}<small>{viewer ? t('转一转、拆开看看里面') : t('需要 3D 卡带：设置 › 常规 › 主界面卡带')}</small></button>
      <button role="menuitem" disabled={busy} onClick={() => { setMenuOpen(false); void act('library.open_dir', game, 'sandbox'); }}>{t('打开沙盒目录')}</button>
      <button role="menuitem" disabled={busy} onClick={() => { setMenuOpen(false); void act('library.open_dir', game, 'log'); }}>{t('打开日志目录')}</button>
      <button role="menuitem" onClick={() => { setMenuOpen(false); setInfoOpen(true); }}>{t('安装信息')}</button>
      <hr />
      <button role="menuitem" class="danger" disabled={busy || importing || game.running} title={game.running ? t('游戏运行中，退出后才能清空数据。') : undefined} onClick={() => { setMenuOpen(false); setClearing(game); }}>{t('清空游戏数据')}<small>{t('删除存档与进度，保留游戏和数据包')}</small></button>
      <button role="menuitem" class="danger" disabled={busy || importing || game.running} title={game.running ? t('游戏运行中，退出后才能删除。') : undefined} onClick={() => requestRemoval(game)}>{t('删除游戏')}<small>{t('默认保留存档，重新导入时恢复')}</small></button>
    </div>}
  </div>;
  const actions = (game: LibraryItem) => {
    if (game.running) {
      const dashboard = instance?.port ? <button ref={primary} class="pill main" disabled={busy || instance.status !== 'ready'} title={t(instance.detail)} onClick={() => void act('dashboard.open', game)}><Key>A</Key>{t('打开 Dashboard')}</button> : null;
      const controls = <button ref={dashboard ? undefined : primary} class={`pill ${dashboard ? '' : 'main'}`} disabled={busy || !editorReady} title={t(instance?.key_editor_detail ?? '游戏内也可按 F2 打开按键设置。')} onClick={() => openControls(game)}><Key>{dashboard ? 'X' : 'A'}</Key>{t('按键设置')}</button>;
      return <>{dashboard}{controls}{moreMenu(game)}</>;
    }
    return <>
      <button ref={primary} class="pill main" disabled={busy || !game.can_launch} title={game.can_launch ? t('启动游戏') : statusDetail(game)} onClick={() => void act('library.launch', game)}><Key>A</Key>{t('开始游戏')}</button>
      <button class="pill" disabled={busy || importing} onClick={() => openGameSettings(game)}><Key>Y</Key>{t('游戏设置')}</button>
      {moreMenu(game)}
    </>;
  };
  // Pointer tilt for the hero cartridge; CSSOM custom properties stay within the style-src 'self' policy.
  const tilt = (event: PointerEvent) => {
    const stage = event.currentTarget as HTMLElement, target = stage.querySelector<HTMLElement>('.tilt');
    if (!target) return;
    const box = stage.getBoundingClientRect(), inside = event.type === 'pointermove';
    const x = inside ? (event.clientX - box.left) / box.width - 0.5 : 0, y = inside ? (event.clientY - box.top) / box.height - 0.5 : 0;
    target.style.setProperty('--tilt-y', `${(x * 30).toFixed(2)}deg`);
    target.style.setProperty('--tilt-x', `${(-y * 16).toFixed(2)}deg`);
  };
  const hero = () => {
    if (loading) return <div class="hero-text"><h1 class="small">{t('正在读取游戏库…')}</h1></div>;
    if (library.items.length === 0) return <>
      <div class="hero-text"><div class="kicker"><i />Welcome to OGPlay</div><h1>{t('添加你的')}<br />{t('第一款游戏')}</h1>
        <div class="sub">{t('选择安卓游戏安装包（APK），或将它拖入窗口。')}</div>
        <div class="cta"><button ref={primary} class="pill main" disabled={importing} onClick={startImport}><Key>A</Key>{t('选择 APK 文件')}</button></div>
        <div class="state">{t('支持单体 APK，分包格式暂不支持')}</div></div>
      <div class="showcase"><button class="ghost-cart" aria-label={t('选择 APK 文件')} disabled={importing} onClick={startImport}>＋</button><div class="floor" /></div>
    </>;
    if (!item) return <div class="hero-text"><h1 class="small">{query ? t('没有符合条件的游戏') : t('这个分类里没有游戏')}</h1>
      <div class="sub">{t('按 Q / E 切换分类，或换个关键词。')}</div><div class="cta"><button class="pill" onClick={resetFilters}><Key>B</Key>{t('清除筛选')}</button></div></div>;
    const status = statusOf(item), detail = statusDetail(item), profile = profileText(item), external = externalText(item);
    const lastRun = item.running ? null : lastRunText(item);
    const position = items.indexOf(item) + 1;
    const fix = item.status === 'missing_external' ? <button class="link" onClick={() => openGameSettings(item)}>{t('选择数据包 →')}</button> :
      item.status === 'damaged' && !item.running ? <button class="link" onClick={() => requestRemoval(item)}>{t('删除后重新导入 →')}</button> : null;
    const runtime = instance ? ` · PID ${instance.process_id}${instance.port ? t(' · MCP 端口 {port}', { port: instance.port }) : ''}` : '';
    return <>
      <div class="hero-text" key={item.installation_id}>
        <div class="kicker"><i />No.{String(position).padStart(2, '0')} / {String(items.length).padStart(2, '0')}{item.imported_at ? t(' · 导入于 {date}', { date: item.imported_at.slice(0, 10) }) : ''}</div>
        <h1 title={item.display_name}>{item.display_name}</h1>
        <div class="sub"><span class="mono">{item.package || t('包名不可用')}</span></div>
        <div class="stats">
          <div title={item.version_code === null ? undefined : `versionCode ${item.version_code}`}><b>{item.version_name || (item.version_code === null ? t('元数据不可用') : String(item.version_code))}</b><small>{item.version_name && item.version_code !== null ? t('版本 · {code}', { code: item.version_code }) : t('版本')}</small></div>
          {profile && <div title={profile.detail}><b>{profile.value}</b><small>Profile</small></div>}
          <div title={external.detail}><b>{external.value}</b><small>{t('数据包')}</small></div>
        </div>
        <div class="state-row">
          <div class="state"><span class={`led ${item.running ? 'running' : status.tone}`} /><span>{status.label}{detail ? ` · ${detail}` : ''}{runtime}</span>{fix}</div>
          {lastRun && <div class="state last-run" title={lastRun.error || undefined}><span class="led danger" />
            <b>{lastRun.label}</b>
            <button class="link" disabled={busy} onClick={() => void act('library.open_dir', item, 'log')}>{t('日志 →')}</button></div>}
        </div>
        <div class="cta">{actions(item)}</div>
      </div>
      <div class="showcase" onPointerMove={tilt} onPointerLeave={tilt}>
        {(item.running || status.tone !== 'ready') && <span class={`flag ${item.running ? 'running' : status.tone}`}>{item.running ? t('● 运行中') : status.label}</span>}
        {cart3d ? <Hero3D api={cart3d} item={item} tone={item.running ? 'running' : status.tone} shown={() => setHeroShown(true)} inspect={openViewer}
          neighbours={[items[(position - 2 + items.length) % items.length], items[position % items.length]].filter(game => game !== item)
            .map(game => [game, game.running ? 'running' : statusOf(game).tone] as [LibraryItem, string])} /> :
          <><div class="float"><div class="tilt"><Cartridge item={item} tone={item.running ? 'running' : status.tone} hero /></div></div><div class="floor" /></>}
      </div>
    </>;
  };
  return <div class={`console skin-${skin} ${dragging ? 'dragging' : ''}`}
    onDragEnter={event => { if (isFileDrag(event)) { event.preventDefault(); ++dragDepth.current; setDragging(true); } }}
    onDragOver={event => { if (isFileDrag(event)) { event.preventDefault(); if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'; } }}
    onDragLeave={event => { event.preventDefault(); if (--dragDepth.current <= 0) { dragDepth.current = 0; setDragging(false); } }}
    onDrop={event => { event.preventDefault(); dragDepth.current = 0; setDragging(false); if (isFileDrag(event)) intake(Array.from(event.dataTransfer?.files ?? [])); }}>
    <Backdrop item={loading ? undefined : item} skin={skin} />
    <header class="top">
      <Brand />
      <nav class="tabs" aria-label={t('筛选游戏')}>
        <span class="shoulder" aria-hidden="true" title="Q">L</span>
        {filters.map(([value, label]) => <button key={value} aria-pressed={filter === value} onClick={() => setFilter(value)}>
          {t(label)}<span class="count">{visibleItems(library.items, '', value, 'name').length}</span></button>)}
        <span class="shoulder" aria-hidden="true" title="E">R</span>
      </nav>
      <div class="tools">
        <div class={`search ${searchOpen || query ? 'open' : ''}`}>
          <button class="round" aria-label={t('搜索游戏')} title={t('搜索（⌘K / Ctrl+K）')} onClick={() => searchOpen || query ? search.current?.focus() : openSearch()}><Glyph name="search" /></button>
          <input ref={search} type="search" placeholder={t('名称、包名或版本')} aria-label={t('搜索游戏')} value={query} tabIndex={searchOpen || query ? 0 : -1}
            onInput={event => setQuery(event.currentTarget.value)} onBlur={() => { if (!query) setSearchOpen(false); }} />
        </div>
        <button class={`running-pill ${runningCount ? 'live' : ''}`} onClick={() => setDashboardListOpen(true)}><span class={`led ${runningCount ? 'running' : 'idle'}`} />{t('{n} 运行中', { n: runningCount })}</button>
        <button class="round" aria-label={t('刷新游戏库')} title={t('刷新')} disabled={busy || loading} onClick={() => void refresh().catch(report)}><Glyph name="reload" /></button>
        <button class="round" aria-label={t('设置')} title={t('设置')} disabled={busy || importing} onClick={() => setSettingsOpen(true)}><Glyph name="settings" /></button>
        <Clock />
      </div>
    </header>
    <section class="stage" aria-label={t('游戏详情')} aria-busy={loading}>{hero()}</section>
    <section class="rail" aria-label={t('游戏条目')}>
      <div class="rail-head"><b>{filterLabel}</b><span role="status">{t('{n} 款游戏', { n: items.length })}{query ? t(' · 搜索“{query}”', { query }) : ''}</span>
        <SortMenu value={sort} change={setSort} />
      </div>
      {/* The import tile sits beside the scrolling strip, so it is in view however long the library is. */}
      <div class="rail-body"><div class="strip" ref={strip} onWheel={event => { if (strip.current && Math.abs(event.deltaY) > Math.abs(event.deltaX)) strip.current.scrollLeft += event.deltaY; }}>
        {items.map(game => {
          const status = statusOf(game);
          const tone = game.running ? 'running' : status.tone;
          return <button key={game.installation_id} class="tile" data-instance={game.installation_id}
            aria-pressed={item?.installation_id === game.installation_id} aria-label={t('{name}，{status}', { name: game.display_name, status: status.label })} title={`${game.display_name} · ${versionLabel(game)}`}
            onClick={() => setSelected(game.installation_id)} onDblClick={() => { if (game.can_launch && !game.running) void act('library.launch', game); }}>
            <TileCart api={cart3d} item={game} tone={tone} ready={() => tileReady(game.installation_id)} /><span class="name">{game.display_name}</span>
          </button>;
        })}
      </div>
      <button class="tile add" title={t('导入游戏（⌘O / Ctrl+O）')} disabled={importing} onClick={startImport}><b>＋</b>{t('导入游戏')}<small>{t('选择 APK')}</small></button></div>
    </section>
    <footer class="foot">
      <span class="hint"><Key>← →</Key>{t('选择')}</span><span class="hint"><Key>A</Key>{t('开始')}<kbd>Enter</kbd></span><span class="hint"><Key>B</Key>{t('返回')}<kbd>Esc</kbd></span>
      <span class="hint"><Key>X</Key>{t('按键设置')}</span><span class="hint"><Key>Y</Key>{t('游戏设置')}</span>
      {viewer && <span class="hint"><Key>⧉</Key>{t('查看卡带')}<kbd>V</kbd></span>}<span class="hint"><Key>L R</Key>{t('切换分类')}<kbd>Q E</kbd></span>
    </footer>
    {splash && <Splash ready={startReady} onDone={() => setSplash(false)} />}
    {dragging && <div class="drop" aria-live="polite"><div class="slot" /><h2>{t('松开，插入卡槽')}</h2><p>{t('单体 APK · 一次一个文件')}</p></div>}
    {importing && <ImportWizard defaultExternal={String(settings?.values.default_external_dir ?? '')} file={pendingFile} onClose={() => { setImporting(false); setPendingFile(null); }} onImported={async id => {
      setQuery(''); setFilter('all'); setSearchOpen(false); await refresh(); setSelected(id); setImporting(false); setPendingFile(null);
    }} />}
    {dataHint && <DataHintDialog id={dataHint.id} initial={dataHint.hint}
      name={library.items.find(game => game.installation_id === dataHint.id)?.display_name ?? dataHint.id}
      onClose={() => setDataHint(null)} onApplied={async relaunch => {
        const id = dataHint.id;
        setDataHint(null); await refresh();
        if (relaunch) await rpc('library.launch', { installation_id: id });
      }} />}
    {clearing && <ClearDataDialog item={library.items.find(game => game.installation_id === clearing.installation_id) ?? clearing} busy={busy}
      onConfirm={() => void clearData(clearing)} onClose={() => { if (!busy) setClearing(null); }} />}
    {removing && <RemoveGameDialog item={removing} busy={busy} onConfirm={withSaves => void remove(removing, withSaves)} onClose={() => { if (!busy) setRemoving(null); }} />}
    {infoOpen && item && <InstallInfoDialog item={item} busy={busy} openDir={kind => void act('library.open_dir', item, kind)} onClose={() => setInfoOpen(false)} />}
    {dashboardListOpen && <Modal title={t('运行中的游戏')} onDismiss={() => setDashboardListOpen(false)}>
      {dashboards.length === 0 ? <p>{t('当前启动器没有运行中的游戏。')}</p> : dashboards.map(instance => <section key={instance.installation_id}>
        <h3>{library.items.find(game => game.installation_id === instance.installation_id)?.display_name ?? instance.installation_id}</h3>
        <p class="mono">PID {instance.process_id} · {instance.port ? t('端口 {port}', { port: instance.port }) : t('MCP 未启用')}</p>
        <p>{t(instance.detail)}</p>
        <button disabled={instance.status !== 'ready'} onClick={() => void rpc('dashboard.open', { installation_id: instance.installation_id }).catch(report)}>{t('打开 Dashboard')}</button>
        <button disabled={instance.key_editor_status !== 'ready'} title={t(instance.key_editor_detail)} onClick={() => void rpc('controls.open', { installation_id: instance.installation_id }).catch(report)}>{t('按键设置')}</button>
      </section>)}
    </Modal>}
    {!importing && errors.length > 0 && <Modal title={t('操作提示')} onDismiss={() => setErrors(queue => queue.slice(1))}><pre>{errors[0]}</pre></Modal>}
  </div>;
}
// The host's settings decide; the last language and skin are remembered so the splash does not flash the other one.
function applyLanguage(settings: Settings) { const value = resolvedLanguage(settings.values.language, settings.system_language); setLanguage(value); remember('ogplay.language', value); }
const rememberedSkin = skinOf(recall('ogplay.skin'));
