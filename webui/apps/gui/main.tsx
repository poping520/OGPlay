import { render, type ComponentChildren } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { rpc, type Library, type LibraryItem } from './rpc';
import { cycle, filters, statuses, validatePackageSelection, versionLabel, visibleItems, type Filter, type Sort } from './library';
import { SettingsPage } from './settings-page';
import { GameSettingsPage } from './game-settings-page';
import type { Settings } from './settings';
import { ImportWizard } from './import-wizard';
import { Cartridge, shellStyle, useIconColor } from './cartridge';
import { Hero3D, TileCart, useCart3D, type CartMode } from './cartridge3d';
import logo from './assets/logo.png';
import '../../packages/ui-kit/theme.css';
import './console.css';
interface DashboardInfo { installation_id: string; process_id: number; port: number | null; status: string; detail: string; key_editor_status: string; key_editor_detail: string }

const statusOf = (item: LibraryItem) => statuses[item.status] ?? { label: '状态未知', tone: 'warning' };
const editable = (target: EventTarget | null) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName));
function Key({ children }: { children: ComponentChildren }) { return <span class="key" aria-hidden="true">{children}</span>; }
function Modal({ title, onDismiss, children }: { title: string; onDismiss: () => void; children: ComponentChildren }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  return <dialog ref={dialog} aria-label={title} onCancel={event => { event.preventDefault(); onDismiss(); }}>
    <h2>{title}</h2>{children}<div class="modal-actions"><button autoFocus onClick={onDismiss}>关闭</button></div>
  </dialog>;
}
function Backdrop({ item }: { item: LibraryItem | undefined }) {
  const color = useIconColor(item?.icon ?? '');
  return <div class="backdrop" aria-hidden="true">
    <div class={`backdrop-art ${item ? '' : 'welcome'}`} style={item ? { ...shellStyle(color), backgroundImage: item.icon ? `url("${item.icon}")` : '' } : {}} />
    <div class="backdrop-fade" /><div class="backdrop-grain" />
  </div>;
}
// Launch splash: icon and wordmark stay up at least SPLASH_MIN_MS and until the main screen is ready (library,
// settings, and with 3D the hero's first frame and the first rail stills), never longer than SPLASH_MAX_MS.
const SPLASH_MIN_MS = 900, SPLASH_MAX_MS = 8000;
function Splash({ ready, onDone }: { ready: boolean; onDone: () => void }) {
  const started = useRef(performance.now());
  const [expired, setExpired] = useState(false);
  const [leaving, setLeaving] = useState(false);
  useEffect(() => { const timer = window.setTimeout(() => setExpired(true), SPLASH_MAX_MS); return () => window.clearTimeout(timer); }, []);
  useEffect(() => {
    if (!(ready || expired) || leaving) return;
    const timer = window.setTimeout(() => setLeaving(true), Math.max(0, SPLASH_MIN_MS - (performance.now() - started.current)));
    return () => window.clearTimeout(timer);
  }, [ready, expired, leaving]);
  useEffect(() => {
    if (!leaving) return;
    const timer = window.setTimeout(onDone, matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 450);
    return () => window.clearTimeout(timer);
  }, [leaving]);
  return <div class={`splash ${leaving ? 'leaving' : ''}`} role="status" aria-label="OGPlay 正在启动">
    <img src={logo} alt="" /><b><span>OG</span>Play</b><small>安卓老游戏兼容层</small><i class="splash-bar" />
  </div>;
}
function Clock() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const timer = window.setInterval(() => setNow(new Date()), 10000); return () => window.clearInterval(timer); }, []);
  return <span class="clock">{now.toTimeString().slice(0, 5)}</span>;
}
function App() {
  const [library, setLibrary] = useState<Library>({ items: [], library_root: '' });
  const [dashboards, setDashboards] = useState<DashboardInfo[]>([]);
  const [removing, setRemoving] = useState<LibraryItem | null>(null);
  const [dashboardListOpen, setDashboardListOpen] = useState(false);
  const [infoOpen, setInfoOpen] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [searchOpen, setSearchOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
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
  const applySettings = (value: Settings) => { settingsRef.current = value; setSettings(value); };
  // Pending until settings load, so a CSS preference never starts loading the 3D bundle; unreadable settings use the default.
  const cartMode: CartMode = settings ? (settings.values.cartridge === 'css' ? 'css' : '3d') : settingsFailed ? '3d' : 'pending';
  const cart3d = useCart3D(cartMode, report);
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
      if (detail.exit_code !== 0 && settingsRef.current?.values.show_exit_log !== false) report(`${detail.installation_id} 退出码 ${detail.exit_code}\n${detail.log_tail}`);
      void refresh().catch(report);
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
    const dashboardError = (event: Event) => report((event as CustomEvent<string>).detail);
    window.addEventListener('ogplay-dashboard-error', dashboardError);
    window.addEventListener('ogplay-exit', exited);
    return () => { disposed = true; window.clearInterval(timer); window.removeEventListener('ogplay-dashboard-error', dashboardError); ++generation.current; window.removeEventListener('ogplay-exit', exited); };
  }, []);
  useEffect(() => { setMenuOpen(false); }, [item?.installation_id]);
  useEffect(() => {
    strip.current?.querySelector('.tile[aria-pressed=true]')?.scrollIntoView({ inline: 'center', block: 'nearest', behavior: 'smooth' });
  }, [item?.installation_id, filter]);
  async function act(method: string, game: LibraryItem, kind?: string) {
    if (inFlight.current) return;
    inFlight.current = true; setBusy(true);
    try {
      await rpc(method, { installation_id: game.installation_id, ...(kind ? { kind } : {}), ...(method === "library.remove" ? { confirmed: true } : {}) });
      await refresh();
      return true;
    } catch (reason) { report(reason); return false; }
    finally { inFlight.current = false; setBusy(false); }
  }
  const remove = async (game: LibraryItem) => {
    if (await act('library.remove', game)) { setRemoving(null); setSelected(''); }
  };
  const requestRemoval = (game: LibraryItem) => {
    setMenuOpen(false);
    if (game.running || busy || importing) return;
    if (settingsRef.current?.values.confirm_delete === false) void remove(game);
    else setRemoving(game);
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
  const openGameSettings = (game: LibraryItem) => { if (!busy && !importing) { setMenuOpen(false); setGameSettingsId(game.installation_id); } };

  // Keyboard stands in for the gamepad legend: ←/→ select, Enter = A, Esc = B, X/Y shortcuts, Q/E = L/R.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (settingsOpen || gameSettingsId) return;   // the settings pages own the keyboard while open
      if (event.key === 'Escape') {
        if (menuOpen) { setMenuOpen(false); return; }
        if (searchOpen && !query && document.activeElement === search.current) { setSearchOpen(false); search.current?.blur(); }
        else if (!item && !document.querySelector('dialog[open]') && (query || filter !== 'all')) resetFilters();
        return;
      }
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') { event.preventDefault(); openSearch(); return; }
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

  if (settingsOpen) return <SettingsPage onBack={() => { setSettingsOpen(false); void refresh().catch(report); }} onSaved={applySettings} />;
  if (gameSettingsId) return <GameSettingsPage id={gameSettingsId} item={library.items.find(game => game.installation_id === gameSettingsId)}
    skin={settings?.values.settings_style === 'glass' ? 'glass' : 'console'} cart3d={cart3d}
    onBack={() => { setGameSettingsId(''); void refresh().catch(report); }} />;

  const filterLabel = filters.find(([value]) => value === filter)![1];
  const moreMenu = (game: LibraryItem) => <div class="more">
    <button class="pill" aria-haspopup="menu" aria-expanded={menuOpen} onClick={() => setMenuOpen(open => !open)}><Key>…</Key>更多</button>
    {menuOpen && <div class="menu" role="menu">
      {game.running && <button role="menuitem" disabled={busy || importing} onClick={() => openGameSettings(game)}>游戏设置<small>保存后下次启动生效</small></button>}
      <button role="menuitem" disabled={busy} onClick={() => { setMenuOpen(false); void act('library.open_dir', game, 'sandbox'); }}>打开沙盒目录</button>
      <button role="menuitem" disabled={busy} onClick={() => { setMenuOpen(false); void act('library.open_dir', game, 'log'); }}>打开日志目录</button>
      <button role="menuitem" onClick={() => { setMenuOpen(false); setInfoOpen(true); }}>安装信息</button>
      <hr />
      <button role="menuitem" class="danger" disabled={busy || importing || game.running} title={game.running ? '游戏运行中，退出后才能移除。' : undefined} onClick={() => requestRemoval(game)}>移除库中实例<small>保留沙盒存档与外部数据包</small></button>
    </div>}
  </div>;
  const actions = (game: LibraryItem) => {
    if (game.running) {
      const dashboard = instance?.port ? <button ref={primary} class="pill main" disabled={busy || instance.status !== 'ready'} title={instance.detail} onClick={() => void act('dashboard.open', game)}><Key>A</Key>打开 Dashboard</button> : null;
      const controls = <button ref={dashboard ? undefined : primary} class={`pill ${dashboard ? '' : 'main'}`} disabled={busy || !editorReady} title={instance?.key_editor_detail ?? '游戏内也可按 F2 打开按键设置。'} onClick={() => openControls(game)}><Key>{dashboard ? 'X' : 'A'}</Key>按键设置</button>;
      return <>{dashboard}{controls}{moreMenu(game)}</>;
    }
    return <>
      <button ref={primary} class="pill main" disabled={busy || !game.can_launch} title={game.can_launch ? '启动游戏' : game.detail} onClick={() => void act('library.launch', game)}><Key>A</Key>{game.status === 'missing_profile' ? '尝试启动' : '开始游戏'}</button>
      <button class="pill" disabled={busy || importing} onClick={() => openGameSettings(game)}><Key>Y</Key>游戏设置</button>
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
    if (loading) return <div class="hero-text"><h1 class="small">正在读取游戏库…</h1></div>;
    if (library.items.length === 0) return <>
      <div class="hero-text"><div class="kicker"><i />Welcome to OGPlay</div><h1>把经典游戏<br />带回来</h1>
        <div class="sub">安卓老游戏，在桌面上原生运行</div>
        <div class="cta"><button ref={primary} class="pill main" disabled={importing} onClick={startImport}><Key>A</Key>选择安装包</button></div>
        <div class="state">也可以直接把 APK 拖进窗口 · 支持单体 APK，分包格式暂不支持</div></div>
      <div class="showcase"><button class="ghost-cart" aria-label="选择安装包" disabled={importing} onClick={startImport}>＋</button><div class="floor" /></div>
    </>;
    if (!item) return <div class="hero-text"><h1 class="small">{query ? '没有符合条件的游戏' : '这个分类里没有游戏'}</h1>
      <div class="sub">按 Q / E 切换分类，或换个关键词。</div><div class="cta"><button class="pill" onClick={resetFilters}><Key>B</Key>清除筛选</button></div></div>;
    const status = statusOf(item);
    const position = items.indexOf(item) + 1;
    const fix = item.status === 'missing_external' ? <button class="link" onClick={() => openGameSettings(item)}>选择数据包 →</button> :
      item.status === 'damaged' && !item.running ? <button class="link" onClick={() => requestRemoval(item)}>移除后重新导入 →</button> : null;
    const runtime = instance ? ` · PID ${instance.process_id}${instance.port ? ` · MCP 端口 ${instance.port}` : ''}` : '';
    return <>
      <div class="hero-text" key={item.installation_id}>
        <div class="kicker"><i />No.{String(position).padStart(2, '0')} / {String(items.length).padStart(2, '0')}{item.imported_at ? ` · 导入于 ${item.imported_at.slice(0, 10)}` : ''}</div>
        <h1 title={item.display_name}>{item.display_name}</h1>
        <div class="sub"><span class="mono">{item.package || '包名不可用'}</span></div>
        <div class="stats">
          <div><b>{item.version_name || (item.version_code === null ? item.version : String(item.version_code))}</b><small>{item.version_code === null ? '版本' : `版本 · versionCode ${item.version_code}`}</small></div>
          <div title={item.profile.detail}><b>{item.profile.value || '—'}</b><small>Profile</small></div>
          <div title={item.external.detail}><b>{item.external.value || '—'}</b><small>数据包</small></div>
        </div>
        <div class="state"><span class={`led ${item.running ? 'running' : status.tone}`} /><span>{status.label} · {item.detail}{runtime}</span>{fix}</div>
        <div class="cta">{actions(item)}</div>
      </div>
      <div class="showcase" onPointerMove={tilt} onPointerLeave={tilt}>
        {(item.running || status.tone !== 'ready') && <span class={`flag ${item.running ? 'running' : status.tone}`}>{item.running ? '● 运行中' : status.label}</span>}
        {cart3d ? <Hero3D api={cart3d} item={item} tone={item.running ? 'running' : status.tone} shown={() => setHeroShown(true)} /> :
          <><div class="float"><div class="tilt"><Cartridge item={item} tone={item.running ? 'running' : status.tone} hero /></div></div><div class="floor" /></>}
      </div>
    </>;
  };
  return <div class={`console ${dragging ? 'dragging' : ''}`}
    onDragEnter={event => { if (isFileDrag(event)) { event.preventDefault(); ++dragDepth.current; setDragging(true); } }}
    onDragOver={event => { if (isFileDrag(event)) { event.preventDefault(); if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'; } }}
    onDragLeave={event => { event.preventDefault(); if (--dragDepth.current <= 0) { dragDepth.current = 0; setDragging(false); } }}
    onDrop={event => { event.preventDefault(); dragDepth.current = 0; setDragging(false); if (isFileDrag(event)) intake(Array.from(event.dataTransfer?.files ?? [])); }}>
    <Backdrop item={loading ? undefined : item} />
    <header class="top">
      <div class="brand"><img src={logo} alt="" />OGPlay</div>
      <nav class="tabs" aria-label="筛选游戏">
        <span class="shoulder" aria-hidden="true" title="Q">L</span>
        {filters.map(([value, label]) => <button key={value} aria-pressed={filter === value} onClick={() => setFilter(value)}>
          {label}<span class="count">{visibleItems(library.items, '', value, 'name').length}</span></button>)}
        <span class="shoulder" aria-hidden="true" title="E">R</span>
      </nav>
      <div class="tools">
        <div class={`search ${searchOpen || query ? 'open' : ''}`}>
          <button class="round" aria-label="搜索游戏" title="搜索（⌘K / Ctrl+K）" onClick={() => searchOpen || query ? search.current?.focus() : openSearch()}>⌕</button>
          <input ref={search} type="search" placeholder="名称、包名或版本" aria-label="搜索游戏" value={query} tabIndex={searchOpen || query ? 0 : -1}
            onInput={event => setQuery(event.currentTarget.value)} onBlur={() => { if (!query) setSearchOpen(false); }} />
        </div>
        <button class={`running-pill ${runningCount ? 'live' : ''}`} onClick={() => setDashboardListOpen(true)}><span class={`led ${runningCount ? 'running' : 'idle'}`} />{runningCount} 运行中</button>
        <button class="round" aria-label="刷新游戏库" title="刷新" disabled={busy || loading} onClick={() => void refresh().catch(report)}>⟳</button>
        <button class="round" aria-label="设置" title="设置" disabled={busy || importing} onClick={() => setSettingsOpen(true)}>⚙</button>
        <Clock />
      </div>
    </header>
    <section class="stage" aria-label="游戏详情" aria-busy={loading}>{hero()}</section>
    <section class="rail" aria-label="游戏条目">
      <div class="rail-head"><b>{filterLabel}</b><span role="status">{items.length} 个安装实例{query ? ` · 搜索“${query}”` : ''}</span>
        <label class="sort">排序<select aria-label="排序" value={sort} onChange={event => setSort(event.currentTarget.value as Sort)}><option value="name">名称</option><option value="newest">最近导入</option><option value="oldest">最早导入</option></select></label>
      </div>
      <div class="strip" ref={strip} onWheel={event => { if (strip.current && Math.abs(event.deltaY) > Math.abs(event.deltaX)) strip.current.scrollLeft += event.deltaY; }}>
        {items.map(game => {
          const status = statusOf(game);
          const tone = game.running ? 'running' : status.tone;
          return <button key={game.installation_id} class="tile" data-instance={game.installation_id}
            aria-pressed={item?.installation_id === game.installation_id} aria-label={`${game.display_name}，${status.label}`} title={`${game.display_name} · ${versionLabel(game)}`}
            onClick={() => setSelected(game.installation_id)} onDblClick={() => { if (game.can_launch && !game.running) void act('library.launch', game); }}>
            <TileCart api={cart3d} item={game} tone={tone} ready={() => tileReady(game.installation_id)} /><span class="name">{game.display_name}</span>
          </button>;
        })}
        <button class="tile add" disabled={importing} onClick={startImport}><b>＋</b>制作卡带<small>导入 APK</small></button>
      </div>
    </section>
    <footer class="foot">
      <span class="hint"><Key>← →</Key>选择</span><span class="hint"><Key>A</Key>开始<kbd>Enter</kbd></span><span class="hint"><Key>B</Key>返回<kbd>Esc</kbd></span>
      <span class="hint"><Key>X</Key>按键设置</span><span class="hint"><Key>Y</Key>游戏设置</span><span class="hint"><Key>L R</Key>切换分类<kbd>Q E</kbd></span>
      <span class="path" title={library.library_root}><span dir="ltr">{library.library_root}</span></span>
    </footer>
    {splash && <Splash ready={startReady} onDone={() => setSplash(false)} />}
    {dragging && <div class="drop" aria-live="polite"><div class="slot" /><h2>松开，插入卡槽</h2><p>单体 APK · 一次一个文件</p></div>}
    {importing && <ImportWizard defaultExternal={String(settings?.values.default_external_dir ?? '')} file={pendingFile} onClose={() => { setImporting(false); setPendingFile(null); }} onImported={async id => {
      setQuery(''); setFilter('all'); setSearchOpen(false); await refresh(); setSelected(id); setImporting(false); setPendingFile(null);
    }} />}
    {removing && <Modal title="移除库中实例" onDismiss={() => { if (!busy) setRemoving(null); }}>
      <p>确认移除「{removing.display_name}」？</p><p class="mono">{removing.installation_id}</p>
      <p>删除该实例的库内 APK、设置和日志。沙盒存档、外部数据包及原始 APK 均保留；重新导入会创建新实例，不会自动恢复旧存档。</p>
      <button disabled={busy} onClick={() => void remove(removing)}>确认移除</button>
    </Modal>}
    {infoOpen && item && <Modal title="安装信息" onDismiss={() => setInfoOpen(false)}>
      <dl class="info"><dt>游戏</dt><dd>{item.display_name}<small>{versionLabel(item)}</small></dd>
        <dt>安装实例</dt><dd class="mono">{item.installation_id}</dd><dt>包名</dt><dd class="mono">{item.package || '包名不可用'}</dd>
        <dt>Profile</dt><dd>{item.profile.value}<small>{item.profile.detail}</small></dd><dt>数据包</dt><dd>{item.external.value}<small>{item.external.detail}</small></dd>
        <dt>沙盒路径</dt><dd class="mono">{item.sandbox_path}</dd><dt>日志目录</dt><dd class="mono">{item.log_directory}</dd><dt>导入时间</dt><dd>{item.imported_at || '未记录'}</dd>
        <dt>Dashboard</dt><dd>{instance?.detail ?? '游戏尚未运行'}</dd>
        <dt>按键设置</dt><dd>{instance?.key_editor_detail ?? '先启动游戏；运行中按 F1 查看键位、F2 打开按键设置。'}</dd></dl>
    </Modal>}
    {dashboardListOpen && <Modal title="运行实例" onDismiss={() => setDashboardListOpen(false)}>
      {dashboards.length === 0 ? <p>当前启动器没有运行中的游戏。</p> : dashboards.map(instance => <section key={instance.installation_id}>
        <h3>{library.items.find(game => game.installation_id === instance.installation_id)?.display_name ?? instance.installation_id}</h3>
        <p class="mono">PID {instance.process_id} · {instance.port ? `端口 ${instance.port}` : 'MCP 未启用'}</p>
        <p>{instance.detail}</p>
        <button disabled={instance.status !== 'ready'} onClick={() => void rpc('dashboard.open', { installation_id: instance.installation_id }).catch(report)}>打开 Dashboard</button>
        <button disabled={instance.key_editor_status !== 'ready'} title={instance.key_editor_detail} onClick={() => void rpc('controls.open', { installation_id: instance.installation_id }).catch(report)}>按键设置</button>
      </section>)}
    </Modal>}
    {!importing && errors.length > 0 && <Modal title="操作提示" onDismiss={() => setErrors(queue => queue.slice(1))}><pre>{errors[0]}</pre></Modal>}
  </div>;
}
render(<App />, document.getElementById('app')!);
