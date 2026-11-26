import { Brand, editable } from './kit';
import { Key } from '../../packages/ui-kit/key';
import { Glyph } from '../../packages/ui-kit/glyph';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc, type LibraryItem } from './shared/rpc';
import { t, tj } from './shared/i18n';
import { has } from './shared/features';
import { cycle, externalText, filters, lastRunText, profileText, statusDetail, statusOf, versionLabel, visibleItems } from './library/library';
import { useLauncher } from './library/launcher';
import { SettingsPage } from './settings/settings-page';
import { GameSettingsPage } from './settings/game-settings-page';
import { CartViewer } from './cartridge/cart-viewer';
import { ImportWizard } from './import/import-wizard';
import { DataHintDialog } from './import/data-hint';
import { ClearDataDialog, InstallInfoDialog, RemoveGameDialog } from './library/game-dialogs';
import { Backdrop, Cartridge } from './cartridge/cartridge';
import { Hero3D, TileCart } from './cartridge/cartridge3d';
import { Modal } from './library/modal';
import { Splash } from './library/splash';
import { Clock } from './library/clock';
import { SortMenu } from './library/sort-menu';
import { GameIcon } from './library/game-icon';
import { ContextMenu, Desk, gridColumns, revealIn } from './library/desk';
import './library/library.css';
import './library/console.css';

// Console layout (desktop windows, Android tablets): the library screen, handing the window to a full-screen view
// (global or per-game settings, cartridge viewer) while one is open. The state it shows is useLauncher's. The library
// is the cartridge hero and rail, or in the icon view (the simple one) a desktop of app icons over the selected game's bar.
export function App() {
  const launcher = useLauncher();
  const { library, items, loading, refresh, act, busy, query, setQuery, filter, setFilter, sort, setSort, view, setView, settings, applySettings, skin, cartMode, cart3d, viewer,
    dashboards, errors, report, importing, pendingFile, startImport, intake, dataHint, removing, clearing, setClearing, clearData } = launcher;
  const [dashboardListOpen, setDashboardListOpen] = useState(false);
  const [infoOpen, setInfoOpen] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const [context, setContext] = useState<{ id: string; x: number; y: number } | null>(null);
  const [searchOpen, setSearchOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [viewing, setViewing] = useState(false);
  const [splash, setSplash] = useState(true);
  const [heroShown, setHeroShown] = useState(false);
  const readyTiles = useRef(new Set<string>());
  const [, setReadyTileCount] = useState(0);
  const [selected, setSelected] = useState('');
  const [gameSettingsId, setGameSettingsId] = useState('');
  const [dragging, setDragging] = useState(false);
  const search = useRef<HTMLInputElement>(null);
  const strip = useRef<HTMLDivElement>(null);
  const deskGrid = useRef<HTMLDivElement>(null);
  const primary = useRef<HTMLButtonElement>(null);
  const dragDepth = useRef(0);
  const item = items.find(game => game.installation_id === selected) ?? items[0];
  const instance = item && dashboards.find(value => value.installation_id === item.installation_id);
  const runningCount = library.items.filter(game => game.running).length;
  // The icon desktop needs games to show; loading and the empty library keep the hero's messages and welcome.
  const desk = view === 'icons' && !loading && library.items.length > 0;
  const tileReady = (id: string) => { if (!readyTiles.current.has(id)) { readyTiles.current.add(id); setReadyTileCount(readyTiles.current.size); } };
  const startReady = !loading && cartMode !== 'pending' && (desk || cart3d !== 'waiting' && (cart3d === null ||
    (!item || heroShown) && items.slice(0, 12).every(game => readyTiles.current.has(game.installation_id))));
  useEffect(() => { setMenuOpen(false); }, [item?.installation_id]);
  useEffect(() => {
    if (desk) {
      const grid = deskGrid.current, tile = grid?.querySelector<HTMLElement>('.app[aria-pressed=true]');
      if (grid && tile) revealIn(grid, tile);
      return;
    }
    // Scroll only the strip: scrollIntoView would also shift the page once the strip hits its end.
    const rail = strip.current, tile = rail?.querySelector<HTMLElement>('.tile[aria-pressed=true]');
    if (!rail || !tile) return;
    const offset = tile.getBoundingClientRect().left - rail.getBoundingClientRect().left;
    rail.scrollTo({ left: rail.scrollLeft + offset - (rail.clientWidth - tile.offsetWidth) / 2, behavior: 'smooth' });
  }, [item?.installation_id, filter, desk]);
  const remove = async (game: LibraryItem, withSaves = false) => { if (await launcher.remove(game, withSaves)) setSelected(''); };
  const closeMenus = () => { setMenuOpen(false); setContext(null); };
  const requestRemoval = (game: LibraryItem) => { closeMenus(); launcher.requestRemoval(game); };
  const isFileDrag = (event: DragEvent) => Array.from(event.dataTransfer?.types ?? []).includes('Files');
  const resetFilters = () => { launcher.resetFilters(); setSearchOpen(false); };
  const openSearch = () => { setSearchOpen(true); requestAnimationFrame(() => search.current?.focus()); };
  const editorReady = instance?.key_editor_status === 'ready';
  const openControls = (game: LibraryItem) => { if (has('key_editor') && dashboards.find(value => value.installation_id === game.installation_id)?.key_editor_status === 'ready') void act('controls.open', game); };
  const openViewer = () => { if (viewer) { closeMenus(); setViewing(true); } };
  const openGameSettings = (game: LibraryItem) => { if (!busy && !importing) { closeMenus(); setGameSettingsId(game.installation_id); } };

  const launch = (game: LibraryItem) => { if (game.can_launch && !game.running) void act('library.launch', game); };

  // Keyboard stands in for the gamepad legend: ←/→ select (↑/↓ by a row on the icon desktop), Enter = A, Esc = B, X/Y shortcuts, Q/E = L/R; ⌘K/Ctrl+K
  // searches and ⌘O/Ctrl+O imports from anywhere in the library.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (settingsOpen || gameSettingsId || viewing) return;   // full-screen views own the keyboard while open
      if (event.key === 'Escape') {
        if (menuOpen || context) { closeMenus(); return; }
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
      const onTile = !!target?.closest?.('[data-instance]');
      const row = desk && deskGrid.current && (event.key === 'ArrowUp' || event.key === 'ArrowDown')
        ? (event.key === 'ArrowDown' ? 1 : -1) * gridColumns(deskGrid.current) : 0;
      if (event.key === 'ArrowLeft' || event.key === 'ArrowRight' || row) {
        if (!items.length) return;
        event.preventDefault();
        // Functional update so key repeat before the next render still advances one step per press. ←/→ wrap round
        // the library; ↑/↓ stop at the first and last rows.
        setSelected(current => {
          const from = items.find(game => game.installation_id === current) ?? items[0];
          const next = (row ? items[items.indexOf(from) + row] ?? from : cycle(items, from, event.key === 'ArrowRight' ? 1 : -1)).installation_id;
          if (onTile) requestAnimationFrame(() => (desk ? deskGrid : strip).current?.querySelector<HTMLElement>(`[data-instance="${CSS.escape(next)}"]`)?.focus());
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
  // The Android back key (the host's cancelable ogplay-back): the top dialog first, then a full-screen view, the menu
  // or the filters; when nothing here uses it, the host leaves the launcher.
  useEffect(() => {
    const back = (event: Event) => {
      const dialogs = document.querySelectorAll<HTMLDialogElement>('dialog[open]');
      if (dialogs.length) dialogs[dialogs.length - 1].dispatchEvent(new Event('cancel', { cancelable: true }));
      else if (settingsOpen || gameSettingsId || viewing) window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', cancelable: true }));
      else if (menuOpen || context) closeMenus();
      else if (searchOpen || query || filter !== 'all') { resetFilters(); search.current?.blur(); }
      else return;
      event.preventDefault();
    };
    window.addEventListener('ogplay-back', back);
    return () => window.removeEventListener('ogplay-back', back);
  });
  useEffect(() => {
    if (!menuOpen) return;
    const close = (event: PointerEvent) => { if (!(event.target as HTMLElement).closest?.('.more')) setMenuOpen(false); };
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [menuOpen]);
  // The right-click menu closes on a press or wheel outside it and when the window resizes.
  useEffect(() => {
    if (!context) return;
    const close = (event: Event) => { if (!(event.target as HTMLElement).closest?.('.context')) setContext(null); };
    window.addEventListener('pointerdown', close); window.addEventListener('wheel', close); window.addEventListener('resize', close);
    return () => { window.removeEventListener('pointerdown', close); window.removeEventListener('wheel', close); window.removeEventListener('resize', close); };
  }, [context]);

  if (viewing && viewer && item) return <CartViewer api={viewer} items={items} selected={item} skin={skin} select={setSelected} onBack={() => setViewing(false)} />;
  if (settingsOpen) return <SettingsPage onBack={() => { setSettingsOpen(false); void refresh().catch(report); }} onSaved={applySettings} />;
  if (gameSettingsId) return <GameSettingsPage id={gameSettingsId} item={library.items.find(game => game.installation_id === gameSettingsId)}
    skin={skin} cart3d={cart3d}
    onBack={() => { setGameSettingsId(''); void refresh().catch(report); }}
    clearData={() => { const game = library.items.find(entry => entry.installation_id === gameSettingsId); setGameSettingsId(''); if (game) setClearing(game); }} />;

  const filterLabel = t(filters.find(([value]) => value === filter)![1]);
  const railHead = <div class="rail-head"><b>{filterLabel}</b><span role="status">{t('library.gameCount', { n: items.length })}{query ? t('library.search.summary', { query }) : ''}</span>
    <SortMenu value={sort} change={setSort} />
  </div>;
  // The game's own entries: the 'common.more' menu, and its right-click menu after the actions.
  const menuEntries = (game: LibraryItem) => <>
    {game.running && <button role="menuitem" disabled={busy || importing} onClick={() => openGameSettings(game)}>{t('common.gameSettings')}<small>{t('common.appliesNextLaunch')}</small></button>}
    <button role="menuitem" disabled={!viewer} onClick={openViewer}>{t('library.viewCartridge')}<small>{viewer ? t('library.viewCartridgeHint') : t('cartridge.needs3d')}</small></button>
    {has('open_directory') && <>
      <button role="menuitem" disabled={busy} onClick={() => { closeMenus(); void act('library.open_dir', game, 'sandbox'); }}>{t('common.openSandboxFolder')}</button>
      <button role="menuitem" disabled={busy} onClick={() => { closeMenus(); void act('library.open_dir', game, 'log'); }}>{t('common.openLogFolder')}</button></>}
    <button role="menuitem" onClick={() => { closeMenus(); setInfoOpen(true); }}>{t('common.installInfo')}</button>
    <hr />
      <button role="menuitem" class="danger" disabled={busy || importing || game.running} title={game.running ? t('library.clear.running') : undefined} onClick={() => { closeMenus(); setClearing(game); }}>{t('common.clearGameData')}<small>{t('library.clearDataHint')}</small></button>
    <button role="menuitem" class="danger" disabled={busy || importing || game.running} title={game.running ? t('library.delete.running') : undefined} onClick={() => requestRemoval(game)}>{t('common.deleteGame')}<small>{t('library.savesKeptOnDelete')}</small></button>
  </>;
  const moreMenu = (game: LibraryItem) => <div class="more">
    <button class="pill" aria-haspopup="menu" aria-expanded={menuOpen} onClick={() => setMenuOpen(open => !open)}><Key>…</Key>{t('common.more')}</button>
    {menuOpen && <div class="menu" role="menu">{menuEntries(game)}</div>}
  </div>;
  // Right click on the desktop: what the action pills offer for this game, then its own entries.
  const contextMenu = (game: LibraryItem, x: number, y: number) => {
    const running = dashboards.find(value => value.installation_id === game.installation_id);
    return <ContextMenu x={x} y={y}>
      {game.running ? <>
        {has('dashboard') && running?.port && <button role="menuitem" disabled={busy || running.status !== 'ready'} onClick={() => { closeMenus(); void act('dashboard.open', game); }}>{t('library.openDashboard')}</button>}
        {has('key_editor') && <button role="menuitem" disabled={busy || running?.key_editor_status !== 'ready'} onClick={() => { closeMenus(); openControls(game); }}>{t('common.keySettings')}</button>}
      </> : <>
        <button role="menuitem" disabled={busy || !game.can_launch} onClick={() => { closeMenus(); launch(game); }}>{t('library.play')}{!game.can_launch && <small>{statusDetail(game)}</small>}</button>
        <button role="menuitem" disabled={busy || importing} onClick={() => openGameSettings(game)}>{t('common.gameSettings')}<small>{t('common.appliesNextLaunch')}</small></button>
      </>}
      {(!game.running || has('dashboard') || has('key_editor')) && <hr />}
      {menuEntries(game)}
    </ContextMenu>;
  };
  const actions = (game: LibraryItem) => {
    if (game.running) {
      const dashboard = has('dashboard') && instance?.port ? <button ref={primary} class="pill main" disabled={busy || instance.status !== 'ready'} title={t(instance.detail_id)} onClick={() => void act('dashboard.open', game)}><Key>A</Key>{t('library.openDashboard')}</button> : null;
      const controls = has('key_editor') && <button ref={dashboard ? undefined : primary} class={`pill ${dashboard ? '' : 'main'}`} disabled={busy || !editorReady} title={t(instance?.key_editor_detail_id ?? 'library.keySettingsInGame')} onClick={() => openControls(game)}><Key>{dashboard ? 'X' : 'A'}</Key>{t('common.keySettings')}</button>;
      // Without either window the running game has only its menu here (Android shows the game itself).
      return <>{dashboard}{controls}{!dashboard && !controls && <button ref={primary} class="pill main" disabled><Key>A</Key>{t('library.status.running')}</button>}{moreMenu(game)}</>;
    }
    return <>
      <button ref={primary} class="pill main" disabled={busy || !game.can_launch} title={game.can_launch ? t('library.startGame') : statusDetail(game)} onClick={() => void act('library.launch', game)}><Key>A</Key>{t('library.play')}</button>
      <button class="pill" disabled={busy || importing} onClick={() => openGameSettings(game)}><Key>Y</Key>{t('common.gameSettings')}</button>
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
  // The selected game's status (with its fix and the running instance), and a failed last run beside it; the hero
  // and the desktop's bar share it. The bar has no cartridge flag, so it says 运行中 itself.
  const stateRow = (game: LibraryItem) => {
    // A running game's state needs no reason; that text only explains why 开始游戏 is unavailable.
    const status = statusOf(game), detail = game.status === 'running' ? '' : statusDetail(game), lastRun = game.running ? null : lastRunText(game);
    const fix = game.status === 'missing_external' ? <button class="link" onClick={() => openGameSettings(game)}>{t('library.chooseDataPack')}</button> :
      game.status === 'damaged' && !game.running ? <button class="link" onClick={() => requestRemoval(game)}>{t('library.deleteAndReimport')}</button> : null;
    const runtime = instance ? ` · PID ${instance.process_id}${instance.port ? t('library.mcpPort', { port: instance.port }) : ''}` : '';
    return <div class="state-row">
      <div class="state"><span class={`led ${game.running ? 'running' : status.tone}`} /><span>{desk && game.running ? t('library.status.running') : status.label}{detail ? ` · ${detail}` : ''}{runtime}</span>{fix}</div>
      {lastRun && <div class="state last-run" title={lastRun.error || undefined}><span class="led danger" />
        <b>{lastRun.label}</b>
        {has('open_directory') && <button class="link" disabled={busy} onClick={() => void act('library.open_dir', game, 'log')}>{t('library.lastRun.log')}</button>}</div>}
    </div>;
  };
  const noMatch = () => <div class="hero-text"><h1 class="small">{query ? t('library.filter.emptyMatches') : t('library.filter.emptyCategory')}</h1>
    <div class="sub">{t('library.filter.emptyHint')}</div><div class="cta"><button class="pill" onClick={resetFilters}><Key>B</Key>{t('library.filter.clear')}</button></div></div>;
  // Under the icon desktop: the selected game, its status and the same actions as the hero.
  const dock = (game: LibraryItem) => <section class="dock" aria-label={t('library.gameDetails')}>
    <GameIcon game={game} />
    <div class="dock-title"><b title={game.display_name}>{game.display_name}</b><span class="mono">{game.package || t('library.packageUnavailable')} · {versionLabel(game)}</span></div>
    {stateRow(game)}
    <div class="cta">{actions(game)}</div>
  </section>;
  const hero = () => {
    if (loading) return <div class="hero-text"><h1 class="small">{t('library.loading')}</h1></div>;
    if (library.items.length === 0) return <>
      <div class="hero-text"><div class="kicker"><i />Welcome to OGPlay</div><h1>{tj('library.empty.headline', { br: <br /> })}</h1>
        <div class="sub">{has('file_upload') ? t('library.empty.chooseOrDrop') : t('library.empty.choose')}</div>
        <div class="cta"><button ref={primary} class="pill main" disabled={importing} onClick={startImport}><Key>A</Key>{t('library.empty.chooseFile')}</button></div>
        <div class="state">{t('library.empty.singleApkOnly')}</div></div>
      <div class="showcase"><button class="ghost-cart" aria-label={t('library.empty.chooseFile')} disabled={importing} onClick={startImport}>＋</button><div class="floor" /></div>
    </>;
    if (!item) return noMatch();
    const status = statusOf(item), profile = profileText(item), external = externalText(item);
    const position = items.indexOf(item) + 1;
    return <>
      <div class="hero-text" key={item.installation_id}>
        <div class="kicker"><i />No.{String(position).padStart(2, '0')} / {String(items.length).padStart(2, '0')}{item.imported_at ? t('library.importedOnSuffix', { date: item.imported_at.slice(0, 10) }) : ''}</div>
        <h1 title={item.display_name}>{item.display_name}</h1>
        <div class="sub"><span class="mono">{item.package || t('library.packageUnavailable')}</span></div>
        <div class="stats">
          <div title={item.version_code === null ? undefined : `versionCode ${item.version_code}`}><b>{item.version_name || (item.version_code === null ? t('library.status.metadataUnavailable') : String(item.version_code))}</b><small>{item.version_name && item.version_code !== null ? t('library.versionCode', { code: item.version_code }) : t('common.version')}</small></div>
          {profile && <div title={profile.detail}><b>{profile.value}</b><small>Profile</small></div>}
          <div title={external.detail}><b>{external.value}</b><small>{t('common.dataPack')}</small></div>
        </div>
        {stateRow(item)}
        <div class="cta">{actions(item)}</div>
      </div>
      <div class="showcase" onPointerMove={tilt} onPointerLeave={tilt}>
        {(item.running || status.tone !== 'ready') && <span class={`flag ${item.running ? 'running' : status.tone}`}>{item.running ? t('library.status.runningBadge') : status.label}</span>}
        {cart3d ? <Hero3D api={cart3d} item={item} tone={item.running ? 'running' : status.tone} shown={() => setHeroShown(true)} inspect={openViewer}
          neighbours={[items[(position - 2 + items.length) % items.length], items[position % items.length]].filter(game => game !== item)
            .map(game => [game, game.running ? 'running' : statusOf(game).tone] as [LibraryItem, string])} /> :
          <><div class="float"><div class="tilt"><Cartridge item={item} tone={item.running ? 'running' : status.tone} hero /></div></div><div class="floor" /></>}
      </div>
    </>;
  };
  return <div class={`console skin-${skin} ${dragging ? 'dragging' : ''}`}
    onDragEnter={event => { if (has('file_upload') && isFileDrag(event)) { event.preventDefault(); ++dragDepth.current; setDragging(true); } }}
    onDragOver={event => { if (has('file_upload') && isFileDrag(event)) { event.preventDefault(); if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'; } }}
    onDragLeave={event => { event.preventDefault(); if (--dragDepth.current <= 0) { dragDepth.current = 0; setDragging(false); } }}
    onDrop={event => { event.preventDefault(); dragDepth.current = 0; setDragging(false); if (has('file_upload') && isFileDrag(event)) intake(Array.from(event.dataTransfer?.files ?? [])); }}>
    <Backdrop item={loading ? undefined : item} skin={skin} />
    <header class="top">
      <Brand />
      <nav class="tabs" aria-label={t('library.filter.label')}>
        <span class="shoulder" aria-hidden="true" title="Q">L</span>
        {filters.map(([value, label]) => <button key={value} aria-pressed={filter === value} onClick={() => setFilter(value)}>
          {t(label)}<span class="count">{visibleItems(library.items, '', value, 'name').length}</span></button>)}
        <span class="shoulder" aria-hidden="true" title="E">R</span>
      </nav>
      <div class="tools">
        <div class={`search ${searchOpen || query ? 'open' : ''}`}>
          <button class="round" aria-label={t('library.search.label')} title={t('library.search.shortcut')} onClick={() => searchOpen || query ? search.current?.focus() : openSearch()}><Glyph name="search" /></button>
          <input ref={search} type="search" placeholder={t('library.search.placeholder')} aria-label={t('library.search.label')} value={query} tabIndex={searchOpen || query ? 0 : -1}
            onInput={event => setQuery(event.currentTarget.value)} onBlur={() => { if (!query) setSearchOpen(false); }} />
        </div>
        <button class={`running-pill ${runningCount ? 'live' : ''}`} disabled={!has('dashboard')} onClick={() => setDashboardListOpen(true)}><span class={`led ${runningCount ? 'running' : 'idle'}`} />{t('library.runningCount', { n: runningCount })}</button>
        <button class="round" aria-label={view === 'icons' ? t('library.viewCartridges') : t('library.viewIcons')} title={view === 'icons' ? t('library.viewCartridges') : t('library.viewIcons')}
          disabled={!library.items.length} onClick={() => setView(view === 'icons' ? 'carts' : 'icons')}><Glyph name={view === 'icons' ? 'cartridge' : 'icons'} /></button>
        <button class="round" aria-label={t('library.refresh.label')} title={t('library.refresh.short')} disabled={busy || loading} onClick={() => void refresh().catch(report)}><Glyph name="reload" /></button>
        <button class="round" aria-label={t('common.settings')} title={t('common.settings')} disabled={busy || importing} onClick={() => setSettingsOpen(true)}><Glyph name="settings" /></button>
        <Clock />
      </div>
    </header>
    {desk ? <>
      <section class="desk" aria-label={t('library.games')}>
        {railHead}
        {items.length ? <Desk items={items} selected={item?.installation_id} select={setSelected} launch={launch} importing={importing} startImport={startImport} grid={deskGrid}
          menu={(game, x, y) => { setMenuOpen(false); setContext({ id: game.installation_id, x, y }); }} /> : <div class="desk-empty">{noMatch()}</div>}
      </section>
      {item && dock(item)}
    </> : <>
      <section class="stage" aria-label={t('library.gameDetails')} aria-busy={loading}>{hero()}</section>
      <section class="rail" aria-label={t('library.games')}>
        {railHead}
        {/* The import tile sits beside the scrolling strip, so it is in view however long the library is. */}
        <div class="rail-body"><div class="strip" ref={strip} onWheel={event => { if (strip.current && Math.abs(event.deltaY) > Math.abs(event.deltaX)) strip.current.scrollLeft += event.deltaY; }}>
          {items.map(game => {
            const status = statusOf(game);
            const tone = game.running ? 'running' : status.tone;
            return <button key={game.installation_id} class="tile" data-instance={game.installation_id}
              aria-pressed={item?.installation_id === game.installation_id} aria-label={t('{name}，{status}', { name: game.display_name, status: status.label })} title={`${game.display_name} · ${versionLabel(game)}`}
              onClick={() => setSelected(game.installation_id)} onDblClick={() => launch(game)}>
              <TileCart api={cart3d} item={game} tone={tone} ready={() => tileReady(game.installation_id)} /><span class="name">{game.display_name}</span>
            </button>;
          })}
        </div>
        <button class="tile add" title={t('library.import.shortcut')} disabled={importing} onClick={startImport}><b>＋</b>{t('common.importGame')}<small>{t('common.chooseApk')}</small></button></div>
      </section>
    </>}
    <footer class="foot">
      <span class="hint"><Key>{desk ? '↑ ↓ ← →' : '← →'}</Key>{t('common.select')}</span><span class="hint"><Key>A</Key>{t('common.start')}<kbd>Enter</kbd></span><span class="hint"><Key>B</Key>{t('common.back')}<kbd>Esc</kbd></span>
      {has('key_editor') && <span class="hint"><Key>X</Key>{t('common.keySettings')}</span>}<span class="hint"><Key>Y</Key>{t('common.gameSettings')}</span>
      {viewer && <span class="hint"><Key>⧉</Key>{t('library.viewCartridge')}<kbd>V</kbd></span>}<span class="hint"><Key>L R</Key>{t('library.filter.switchCategory')}<kbd>Q E</kbd></span>
    </footer>
    {splash && <Splash ready={startReady} onDone={() => setSplash(false)} />}
    {dragging && <div class="drop" aria-live="polite"><div class="slot" /><h2>{t('library.drop.release')}</h2><p>{t('library.drop.singleApk')}</p></div>}
    {importing && <ImportWizard defaultExternal={String(settings?.values.default_external_dir ?? '')} file={pendingFile} onClose={launcher.closeImport} onImported={async id => {
      setSearchOpen(false); await launcher.imported(); setSelected(id);
    }} />}
    {dataHint && <DataHintDialog id={dataHint.id} initial={dataHint.hint}
      name={library.items.find(game => game.installation_id === dataHint.id)?.display_name ?? dataHint.id}
      onClose={launcher.closeDataHint} onApplied={launcher.applyDataHint} />}
    {clearing && <ClearDataDialog item={library.items.find(game => game.installation_id === clearing.installation_id) ?? clearing} busy={busy}
      onConfirm={() => void clearData(clearing)} onClose={() => { if (!busy) setClearing(null); }} />}
    {removing && <RemoveGameDialog item={removing} busy={busy} onConfirm={withSaves => void remove(removing, withSaves)} onClose={() => { if (!busy) launcher.cancelRemoval(); }} />}
    {infoOpen && item && <InstallInfoDialog item={item} busy={busy} openDir={has('open_directory') ? kind => void act('library.open_dir', item, kind) : undefined} onClose={() => setInfoOpen(false)} />}
    {dashboardListOpen && <Modal title={t('library.runningGames')} onDismiss={() => setDashboardListOpen(false)}>
      {dashboards.length === 0 ? <p>{t('library.noRunningGames')}</p> : dashboards.map(instance => <section key={instance.installation_id}>
        <h3>{library.items.find(game => game.installation_id === instance.installation_id)?.display_name ?? instance.installation_id}</h3>
        <p class="mono">PID {instance.process_id} · {instance.port ? t('library.port', { port: instance.port }) : t('library.mcpDisabled')}</p>
        <p>{t(instance.detail_id)}</p>
        <button disabled={instance.status !== 'ready'} onClick={() => void rpc('dashboard.open', { installation_id: instance.installation_id }).catch(report)}>{t('library.openDashboard')}</button>
        <button disabled={instance.key_editor_status !== 'ready'} title={t(instance.key_editor_detail_id)} onClick={() => void rpc('controls.open', { installation_id: instance.installation_id }).catch(report)}>{t('common.keySettings')}</button>
      </section>)}
    </Modal>}
    {context && library.items.some(game => game.installation_id === context.id) &&
      contextMenu(library.items.find(game => game.installation_id === context.id)!, context.x, context.y)}
    {!importing && errors.length > 0 && <Modal title={errors[0].title ?? t('common.notice')} onDismiss={launcher.dismissError}><pre>{errors[0].text}</pre></Modal>}
  </div>;
}
