import { Brand } from '../kit';
import { Glyph } from '../../../packages/ui-kit/glyph';
import { useEffect, useRef, useState } from 'preact/hooks';
import type { LibraryItem } from '../shared/rpc';
import { t } from '../shared/i18n';
import { has } from '../shared/features';
import { externalText, filters as allFilters, lastRunText, profileText, statusDetail, statusOf, versionLabel, visibleItems } from '../library/library';
import { useLauncher, type Launcher } from '../library/launcher';
import { sorts } from '../library/sort-menu';
import { SettingsPage } from '../settings/settings-page';
import { GameSettingsPage } from '../settings/game-settings-page';
import { CartViewer } from '../cartridge/cart-viewer';
import { ImportWizard } from '../import/import-wizard';
import { DataHintDialog } from '../import/data-hint';
import { ClearDataDialog, InstallInfoDialog, RemoveGameDialog } from '../library/game-dialogs';
import { Backdrop, Cartridge } from '../cartridge/cartridge';
import { Hero3D, TileCart } from '../cartridge/cartridge3d';
import { Modal } from '../library/modal';
import { Splash } from '../library/splash';
import { GameIcon } from '../library/game-icon';
import { BackButton, MoreIcon, NextIcon, Sheet, SheetItem } from './parts';
import '../library/library.css';
import './phone.css';

// Phone layout (Android phones, portrait, touch): the library as a grid of icons (four a row) or cartridges (three), one page per game with the
// actions at the thumb, full-screen settings and import, bottom sheets for menus and confirmations. Pages stack;
// the back key closes the top dialog first, then the top page. State and actions are useLauncher's, as on the console.
type Page = { kind: 'home' } | { kind: 'game'; id: string } | { kind: 'settings' } | { kind: 'game-settings'; id: string } | { kind: 'viewer'; id: string };
type SheetState = { kind: 'actions'; id: string } | { kind: 'sort' } | { kind: 'stop'; id: string }
  | { kind: 'switch'; from: string; to: string } | null;
// Waiting for a game asked to quit before the next one starts (Android runs one game at a time).
const SWITCH_TIMEOUT_MS = 15000;
// 'library.filter.launchable' is left to the console: the phone keeps all games and the ones needing attention.
const filters = allFilters.filter(([value]) => value !== 'launchable');

export function PhoneApp() {
  const launcher = useLauncher();
  const { library, items, loading, refresh, act, busy, query, setQuery, filter, setFilter, sort, setSort, view, setView, applySettings, skin, cartMode, cart3d, viewer,
    errors, importing, pendingFile, startImport, dataHint, removing, clearing, setClearing, clearData } = launcher;
  const [stack, setStack] = useState<Page[]>([{ kind: 'home' }]);
  const [sheet, setSheet] = useState<SheetState>(null);
  const [info, setInfo] = useState('');
  const [searchOpen, setSearchOpen] = useState(false);
  const [splash, setSplash] = useState(true);
  useEffect(() => { if (!filters.some(([value]) => value === filter)) setFilter('all'); }, [filter]);
  // The filter row scrolls away with the games and slides back on the first scroll up (sticky, moved off screen).
  // Over the page background it is bare, like the top bar; only games scrolled under it get a backing.
  const [chipsAway, setChipsAway] = useState(false);
  const [chipsOver, setChipsOver] = useState(false);
  const lastScroll = useRef(0);
  const scrolled = (event: Event) => {
    const top = (event.currentTarget as HTMLElement).scrollTop, delta = top - lastScroll.current;
    setChipsOver(top > 2);
    if (top < 56) setChipsAway(false);
    else if (Math.abs(delta) > 6) setChipsAway(delta > 0);
    else return;
    lastScroll.current = top;
  };
  const search = useRef<HTMLInputElement>(null);
  const page = stack[stack.length - 1];
  const find = (id: string) => library.items.find(game => game.installation_id === id);
  const push = (next: Page) => setStack(current => [...current, next]);
  const pop = () => setStack(current => current.length > 1 ? current.slice(0, -1) : current);
  // Pages of a game that left the library (removed here or elsewhere) go with it.
  useEffect(() => {
    if (!loading) setStack(current => current.filter(entry => !('id' in entry) || library.items.some(game => game.installation_id === entry.id)));
  }, [library.items, loading]);
  const closeSearch = () => { setQuery(''); setSearchOpen(false); search.current?.blur(); };
  const openSearch = () => { setSearchOpen(true); requestAnimationFrame(() => search.current?.focus()); };
  const openGameSettings = (game: LibraryItem) => { if (!busy && !importing) push({ kind: 'game-settings', id: game.installation_id }); };
  const openViewer = (game: LibraryItem) => { if (viewer) push({ kind: 'viewer', id: game.installation_id }); };
  // Android runs one game at a time, in its own task: a running game is gone back to (it keeps its place), and
  // starting another first asks whether to go back to the running one or close it.
  const running = library.items.find(entry => entry.running);
  const resume = (game: LibraryItem) => void act('library.resume', game);
  const launch = (game: LibraryItem) => {
    if (game.running) resume(game);
    else if (running) setSheet({ kind: 'switch', from: running.installation_id, to: game.installation_id });
    else if (game.can_launch) void act('library.launch', game);
  };
  const [switching, setSwitching] = useState<{ from: string; to: string; until: number } | null>(null);
  const stopThenStart = async (from: LibraryItem, to: LibraryItem) => {
    if (await act('library.stop', from)) setSwitching({ from: from.installation_id, to: to.installation_id, until: Date.now() + SWITCH_TIMEOUT_MS });
  };
  // The library is read again until the closed game has left it running (its exit event may not come to a launcher
  // started after it), then the next one starts.
  useEffect(() => {
    if (!switching) return;
    const from = find(switching.from), to = find(switching.to);
    if (!from?.running) { setSwitching(null); if (to) void act('library.launch', to); return; }
    if (Date.now() > switching.until) {
      setSwitching(null); launcher.report(t('library.running.didNotQuit', { name: from.display_name })); return;
    }
    const timer = window.setTimeout(() => void refresh().catch(launcher.report), 700);
    return () => window.clearTimeout(timer);
  }, [switching, library.items]);

  // The back key (the host's cancelable ogplay-back): the top dialog or sheet, then a full-screen view (which reads
  // it as Esc: a settings group goes back to the group list first), then the top page, the search and the filters.
  useEffect(() => {
    const back = (event: Event) => {
      const dialogs = document.querySelectorAll<HTMLDialogElement>('dialog[open]');
      if (dialogs.length) dialogs[dialogs.length - 1].dispatchEvent(new Event('cancel', { cancelable: true }));
      else if (page.kind === 'settings' || page.kind === 'game-settings' || page.kind === 'viewer')
        window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', cancelable: true }));
      else if (page.kind === 'game') pop();
      else if (searchOpen || query) closeSearch();
      else if (filter !== 'all') setFilter('all');
      else return;
      event.preventDefault();
    };
    window.addEventListener('ogplay-back', back);
    return () => window.removeEventListener('ogplay-back', back);
  });
  // A tap on the dimmed screen around a dialog or sheet dismisses it, as the back key does.
  useEffect(() => {
    const tap = (event: MouseEvent) => {
      const dialog = event.target;
      if (!(dialog instanceof HTMLDialogElement) || !dialog.open) return;
      const box = dialog.getBoundingClientRect();
      if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom)
        dialog.dispatchEvent(new Event('cancel', { cancelable: true }));
    };
    window.addEventListener('click', tap);
    return () => window.removeEventListener('click', tap);
  }, []);

  const overlays = <>
    {splash && <Splash ready={!loading && cartMode !== 'pending'} onDone={() => setSplash(false)} />}
    {importing && <ImportWizard file={pendingFile} onClose={launcher.closeImport} onImported={async id => {
      closeSearch(); await launcher.imported(); setStack([{ kind: 'home' }, { kind: 'game', id }]);
    }} />}
    {dataHint && <DataHintDialog id={dataHint.id} initial={dataHint.hint} name={find(dataHint.id)?.display_name ?? dataHint.id}
      onClose={launcher.closeDataHint} onApplied={launcher.applyDataHint} />}
    {clearing && <ClearDataDialog item={find(clearing.installation_id) ?? clearing} busy={busy}
      onConfirm={() => void clearData(clearing)} onClose={() => { if (!busy) setClearing(null); }} />}
    {removing && <RemoveGameDialog item={removing} busy={busy} onConfirm={withSaves => void launcher.remove(removing, withSaves)}
      onClose={() => { if (!busy) launcher.cancelRemoval(); }} />}
    {info && find(info) && <InstallInfoDialog item={find(info)!} busy={busy} onClose={() => setInfo('')} />}
    {sheet?.kind === 'sort' && <Sheet title={t('library.sort.label')} onClose={() => setSheet(null)}>{sorts.map(([key, label]) =>
      <SheetItem key={key} label={t(label)} checked={key === sort} onClick={() => { setSort(key); setSheet(null); }} />)}</Sheet>}
    {sheet?.kind === 'actions' && find(sheet.id) && <GameSheet launcher={launcher} game={find(sheet.id)!} close={() => setSheet(null)}
      start={launch} settings={openGameSettings} view={viewer ? openViewer : undefined} info={game => setInfo(game.installation_id)}
      stop={game => setSheet({ kind: 'stop', id: game.installation_id })} />}
    {sheet?.kind === 'stop' && find(sheet.id) && <Sheet title={t('library.running.closeTitle', { name: find(sheet.id)!.display_name })} onClose={() => setSheet(null)}>
      <SheetItem label={t('library.running.close')} note={t('library.running.unsavedLost')} danger disabled={busy}
        onClick={() => { const game = find(sheet.id)!; setSheet(null); void act('library.stop', game); }} />
      <SheetItem label={t('common.cancel')} onClick={() => setSheet(null)} /></Sheet>}
    {sheet?.kind === 'switch' && find(sheet.from) && find(sheet.to) && <Sheet title={t('library.running.isRunning', { name: find(sheet.from)!.display_name })} onClose={() => setSheet(null)}>
      <SheetItem label={t('library.running.backTo', { name: find(sheet.from)!.display_name })} disabled={busy}
        onClick={() => { const game = find(sheet.from)!; setSheet(null); resume(game); }} />
      <SheetItem label={t('library.running.closeAndStart', { name: find(sheet.to)!.display_name })} note={t('library.running.oneAtATime')} danger disabled={busy}
        onClick={() => { const from = find(sheet.from)!, to = find(sheet.to)!; setSheet(null); void stopThenStart(from, to); }} />
      <SheetItem label={t('common.cancel')} onClick={() => setSheet(null)} /></Sheet>}
    {!importing && errors.length > 0 && <Modal title={errors[0].title ?? t('common.notice')} onDismiss={launcher.dismissError}><pre>{errors[0].text}</pre></Modal>}
  </>;

  // Full-screen views take the whole window, as on the console; the back key reaches them as Esc.
  if (page.kind === 'settings') return <SettingsPage onBack={() => { pop(); void refresh().catch(launcher.report); }} onSaved={applySettings} />;
  if (page.kind === 'game-settings') return <GameSettingsPage id={page.id} item={find(page.id)} skin={skin} cart3d={cart3d}
    onBack={() => { pop(); void refresh().catch(launcher.report); }}
    clearData={() => { const game = find(page.id); pop(); if (game) setClearing(game); }} />;
  const viewed = page.kind === 'viewer' ? find(page.id) : undefined;
  if (viewed && viewer) return <CartViewer api={viewer} items={items.some(game => game === viewed) ? items : [viewed]} selected={viewed} skin={skin}
    select={id => setStack(current => [...current.slice(0, -1), { kind: 'viewer', id }])} onBack={pop} />;

  const game = page.kind === 'game' ? find(page.id) : undefined;
  return <div class={`console phone skin-${skin}`}>
    <Backdrop item={game} skin={skin} />
    {game ? <GamePage key={game.installation_id} launcher={launcher} game={game} icons={view === 'icons'} back={pop} more={() => setSheet({ kind: 'actions', id: game.installation_id })}
      start={launch} settings={openGameSettings} view={viewer ? openViewer : undefined} /> :
    <div class="ph-page ph-home">
      <header class="ph-bar">
        <Brand />
        <span class="ph-spacer" />
        {library.items.length > 0 && <button class="ph-icon" aria-label={view === 'carts' ? t('library.viewIcons') : t('library.viewCartridges')}
          onClick={() => setView(view === 'carts' ? 'icons' : 'carts')}><Glyph name={view === 'carts' ? 'icons' : 'cartridge'} /></button>}
        <button class="ph-icon" aria-label={t('library.search.label')} aria-pressed={searchOpen || !!query} onClick={() => searchOpen || query ? closeSearch() : openSearch()}><Glyph name="search" /></button>
        <button class="ph-icon" aria-label={t('common.settings')} disabled={busy || importing} onClick={() => push({ kind: 'settings' })}><Glyph name="settings" /></button>
      </header>
      {(searchOpen || query) && <div class="ph-search">
        <Glyph name="search" />
        <input ref={search} type="search" enterKeyHint="search" placeholder={t('library.search.placeholder')} aria-label={t('library.search.label')} value={query}
          onInput={event => setQuery(event.currentTarget.value)} onKeyDown={event => { if (event.key === 'Enter') event.currentTarget.blur(); }} />
        <button onClick={closeSearch}>{t('common.cancel')}</button>
      </div>}
      <main class="ph-scroll" aria-busy={loading} onScroll={scrolled}>
        <nav class={`ph-chips ${chipsAway ? 'away' : ''} ${chipsOver ? 'over' : ''}`} aria-label={t('library.filter.label')}>
          {filters.map(([value, label]) => <button key={value} aria-pressed={filter === value} onClick={() => setFilter(value)}>
            {t(label)}<span class="count">{visibleItems(library.items, '', value, 'name').length}</span></button>)}
          <span class="ph-spacer" />
          <button class="sort" aria-label={`${t('library.sort.label')} · ${t(sorts.find(([key]) => key === sort)![1])}`} onClick={() => setSheet({ kind: 'sort' })}>
            {t(sorts.find(([key]) => key === sort)![1])}<Glyph name="down" /></button>
        </nav>
        {running && <button class="ph-running" disabled={busy} onClick={() => resume(running)}>
          <i class="led running" /><span><b>{running.display_name}</b>{switching ? t('library.running.closing') : t('library.status.running')}</span>
          {!switching && <em>{t('library.running.back')}<NextIcon /></em>}</button>}
        {loading ? <p class="ph-empty">{t('library.loading')}</p> :
          library.items.length === 0 ? <div class="ph-welcome">
            <button class="ghost-cart" aria-label={t('library.empty.chooseFile')} disabled={importing} onClick={startImport}>＋</button>
            <div class="kicker"><i />Welcome to OGPlay</div>
            <h1>{t('library.empty.headline', { br: '' })}</h1>
            <p>{t('library.empty.choose')}</p>
            <button class="pill main" disabled={importing} onClick={startImport}>{t('library.empty.chooseFile')}</button>
            <small>{t('library.empty.singleApkOnly')}</small>
          </div> :
          items.length === 0 ? <div class="ph-welcome">
            <h1 class="small">{query ? t('library.filter.emptyMatches') : t('library.filter.emptyCategory')}</h1>
            <button class="pill" onClick={() => { closeSearch(); launcher.resetFilters(); }}>{t('library.filter.clear')}</button>
          </div> :
          <div class={`ph-grid ${view}`}>{items.map(entry => {
            const status = statusOf(entry), tone = entry.running ? 'running' : status.tone;
            return <button key={entry.installation_id} class="ph-card" aria-label={t('{name}，{status}', { name: entry.display_name, status: status.label })}
              onClick={() => push({ kind: 'game', id: entry.installation_id })}
              onContextMenu={event => { event.preventDefault(); setSheet({ kind: 'actions', id: entry.installation_id }); }}>
              {view === 'carts' ? <span class="ph-cart"><TileCart api={cart3d} item={entry} tone={tone} /></span> : <GameIcon game={entry} />}
              <b class="ph-name">{entry.display_name}</b>
              {/* Ready games say so with the cartridge's own LED; anything else is written out. */}
              {tone !== 'ready' && <small class={`ph-status ${tone}`}><i class={`led ${tone}`} />{entry.running ? t('library.status.running') : status.label}</small>}
            </button>;
          })}</div>}
      </main>
      {library.items.length > 0 && <button class="ph-fab pill main" disabled={importing} onClick={startImport}><b aria-hidden="true">＋</b>{t('common.importGame')}</button>}
    </div>}
    {overlays}
  </div>;
}

interface GameActions {
  start: (game: LibraryItem) => void; settings: (game: LibraryItem) => void; view?: (game: LibraryItem) => void;
}

// One game: its cartridge, the facts the console hero shows (written out here, not left to tooltips) and the actions
// in a bar under the thumb. The 3D cartridge turns under a drag; a tap opens the viewer. Picked from the icon view,
// the page leads with the game's icon instead, as the library did; the cartridge stays one tap away in "⋯".
function GamePage({ launcher, game, icons, back, more, start, settings, view }: GameActions & {
  launcher: Launcher; game: LibraryItem; icons: boolean; back: () => void; more: () => void;
}) {
  const { busy, importing, cart3d, items } = launcher;
  const status = statusOf(game), tone = game.running ? 'running' : status.tone;
  const detail = game.status === 'running' ? '' : statusDetail(game), profile = profileText(game), external = externalText(game), lastRun = game.running ? null : lastRunText(game);
  const position = items.findIndex(entry => entry.installation_id === game.installation_id) + 1;
  return <div class="ph-page ph-game">
    <header class="ph-bar">
      <BackButton onClick={back} />
      <span class="ph-spacer" />
      <button class="ph-icon" aria-label={t('common.more')} onClick={more}><MoreIcon /></button>
    </header>
    <main class="ph-scroll">
      {icons ? <div class="ph-icon-hero">
        <GameIcon game={game} />
        {(game.running || status.tone !== 'ready') && <span class={`flag ${tone}`}>{game.running ? t('library.status.runningBadge') : status.label}</span>}
      </div> :
      <div class="ph-showcase">
        {(game.running || status.tone !== 'ready') && <span class={`flag ${tone}`}>{game.running ? t('library.status.runningBadge') : status.label}</span>}
        {cart3d ? <Hero3D api={cart3d} item={game} tone={tone} shown={() => { /* no splash waits on it here */ }} neighbours={[]} inspect={() => view?.(game)} /> :
          <><div class="float"><div class="tilt"><Cartridge item={game} tone={tone} hero /></div></div><div class="floor" /></>}
      </div>}
      <section class="ph-info">
        <div class="kicker"><i />{position > 0 ? `No.${String(position).padStart(2, '0')} / ${String(items.length).padStart(2, '0')}` : 'OGPlay'}</div>
        <h1>{game.display_name}</h1>
        <p class="mono">{game.package || t('library.packageUnavailable')}</p>
        <dl class="ph-stats">
          <div><dt>{t('common.version')}</dt><dd>{game.version_name || (game.version_code === null ? t('library.status.metadataUnavailable') : String(game.version_code))}</dd></div>
          {profile && <div><dt>Profile</dt><dd>{profile.value}</dd></div>}
          <div><dt>{t('common.dataPack')}</dt><dd>{external.value}</dd></div>
        </dl>
        <div class={`ph-state ${tone}`}><i class={`led ${tone}`} />
          <div><b>{game.running ? t('library.status.running') : status.label}</b>{detail && <small>{detail}</small>}</div></div>
        {lastRun && <div class="ph-state danger"><i class="led danger" />
          <div><b>{lastRun.label}</b>{lastRun.error && <small>{lastRun.error}</small>}</div></div>}
        {game.status === 'damaged' && !game.running && <button class="pill ph-fix" disabled={busy} onClick={() => launcher.requestRemoval(game)}>{t('library.deleteAndReimport')}</button>}
        <ul class="ph-facts">
          {external.detail && game.external.status !== 'not_required' && <li><small>{t('common.dataPack')}</small><span>{external.detail}</span></li>}
          {game.imported_at && <li><span>{t('library.importedOn', { date: game.imported_at.slice(0, 10) })}</span></li>}
          <li><small>{t('common.version')}</small><span class="mono">{versionLabel(game)}</span></li>
        </ul>
      </section>
    </main>
    <footer class="ph-actions">
      <button class="pill ph-square" aria-label={t('common.gameSettings')} disabled={busy || importing} onClick={() => settings(game)}><Glyph name="settings" /></button>
      {game.running
        ? <button class="pill main" disabled={busy} onClick={() => start(game)}><i class="led running" />{t('library.running.back')}</button>
        : <button class="pill main" disabled={busy || !game.can_launch} onClick={() => start(game)}>{t('library.play')}</button>}
    </footer>
  </div>;
}

// Long press on a card, or 'common.more' on a game page: everything one entry offers here.
function GameSheet({ launcher, game, close, start, settings, view, info, stop }: GameActions & {
  launcher: Launcher; game: LibraryItem; close: () => void; info: (game: LibraryItem) => void; stop: (game: LibraryItem) => void;
}) {
  const { busy, importing } = launcher;
  const then = (action: () => void) => () => { close(); action(); };
  return <Sheet title={game.display_name} onClose={close}>
    {game.running ? <>
      <SheetItem label={t('library.running.back')} disabled={busy} onClick={then(() => start(game))} />
      <SheetItem label={t('library.running.close')} note={t('library.running.unsavedLost')} danger disabled={busy} onClick={then(() => stop(game))} />
    </> : <SheetItem label={t('library.play')} disabled={busy || !game.can_launch} note={game.can_launch ? undefined : statusDetail(game)} onClick={then(() => start(game))} />}
    <SheetItem label={t('common.gameSettings')} note={t('common.appliesNextLaunch')} disabled={busy || importing} onClick={then(() => settings(game))} />
    {view && <SheetItem label={t('library.viewCartridge')} note={t('library.viewCartridgeHint')} onClick={then(() => view(game))} />}
    <SheetItem label={t('common.installInfo')} onClick={then(() => info(game))} />
    <SheetItem label={t('common.clearGameData')} note={t('library.clearDataHint')} danger disabled={busy || importing || game.running}
      onClick={then(() => launcher.setClearing(game))} />
    <SheetItem label={t('common.deleteGame')} note={t('library.savesKeptOnDelete')} danger disabled={busy || importing || game.running} onClick={then(() => launcher.requestRemoval(game))} />
  </Sheet>;
}
