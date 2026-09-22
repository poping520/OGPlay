import { render, type ComponentChildren } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { rpc, type Library, type LibraryItem } from './rpc';
import { averageIconColor, statuses, validatePackageSelection, versionLabel, visibleItems, type Filter, type Sort } from './library';
import { SettingsPage } from './settings-page';
import { GameSettingsPage } from './game-settings-page';
import { resolvedTheme, type Settings } from './settings';
import { ImportWizard } from './import-wizard';
import '../../packages/ui-kit/theme.css';

function GameIcon({ item, glow = false }: { item: LibraryItem; glow?: boolean }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [item.icon]);
  const paint = (image: HTMLImageElement) => {
    if (!canvas.current) return;
    const sample = document.createElement('canvas');
    sample.width = sample.height = 16;
    const context = sample.getContext('2d', { willReadFrequently: true });
    const target = canvas.current.getContext('2d');
    if (!context || !target) return;
    let color: [number, number, number] = [124, 156, 255];
    try { context.drawImage(image, 0, 0, 16, 16); color = averageIconColor(context.getImageData(0, 0, 16, 16).data); }
    catch { /* Keep the visible placeholder glow if a resource cannot be sampled. */ }
    const gradient = target.createRadialGradient(128, 128, 0, 128, 128, 124);
    gradient.addColorStop(0, `rgba(${color.join(',')},.7)`);
    gradient.addColorStop(1, `rgba(${color.join(',')},0)`);
    target.clearRect(0, 0, 256, 256); target.fillStyle = gradient; target.fillRect(0, 0, 256, 256);
  };
  return <div class={`game-icon ${glow ? 'with-glow' : ''}`}>
    {glow && <canvas ref={canvas} width={256} height={256} aria-hidden="true" />}
    {item.icon && !failed ? <img src={item.icon} alt="" onLoad={event => paint(event.currentTarget)} onError={() => setFailed(true)} /> : <span class="icon-placeholder" aria-hidden="true">O</span>}
  </div>;
}
function Status({ item }: { item: LibraryItem }) {
  const status = statuses[item.status] ?? { label: '状态未知', tone: 'warning' };
  return <span class={`badge ${status.tone}`} title={item.detail}>{status.label}</span>;
}
function Modal({ title, onDismiss, children }: { title: string; onDismiss: () => void; children: ComponentChildren }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => { const element = dialog.current!; element.showModal(); return () => element.close(); }, []);
  return <dialog ref={dialog} aria-label={title} onCancel={event => { event.preventDefault(); onDismiss(); }}>
    <h2>{title}</h2>{children}<div class="modal-actions"><button autoFocus onClick={onDismiss}>关闭</button></div>
  </dialog>;
}
function App() {
  const [library, setLibrary] = useState<Library>({ items: [], library_root: '' });
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [settings, setSettings] = useState<Settings | null>(null);
  const settingsRef = useRef<Settings | null>(null);
  const [selected, setSelected] = useState('');
  const [query, setQuery] = useState('');
  const [filter, setFilter] = useState<Filter>('all');
  const [sort, setSort] = useState<Sort>('name');
  const [mode, setMode] = useState<'grid' | 'list'>('grid');
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
  const lastSelected = useRef('');
  const dragDepth = useRef(0);
  const items = useMemo(() => visibleItems(library.items, query, filter, sort), [library.items, query, filter, sort]);
  const item = items.find(game => game.installation_id === selected);
  const report = (reason: unknown) => setErrors(queue => [...queue, reason instanceof Error ? reason.message : String(reason)]);
  const applySettings = (value: Settings) => { settingsRef.current = value; setSettings(value); };
  useEffect(() => {
    const media = window.matchMedia('(prefers-color-scheme: light)');
    const apply = () => {
      document.documentElement.dataset.theme = resolvedTheme(settings?.values.theme, media.matches);
      document.documentElement.dataset.density = String(settings?.values.density ?? 'comfortable');
    };
    apply(); media.addEventListener('change', apply);
    return () => media.removeEventListener('change', apply);
  }, [settings]);
  const closeDrawer = () => {
    setSelected('');
    const button = Array.from(document.querySelectorAll<HTMLButtonElement>('.select-game'))
      .find(element => element.dataset.instance === lastSelected.current);
    (button ?? search.current)?.focus();
  };
  useEffect(() => { if (selected && !item) setSelected(''); }, [selected, item]);
  useEffect(() => {
    const escape = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && selected && !errors.length && !importing && !settingsOpen && !gameSettingsId) closeDrawer();
    };
    window.addEventListener('keydown', escape);
    return () => window.removeEventListener('keydown', escape);
  }, [selected, errors.length, importing, settingsOpen, gameSettingsId]);
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
    void rpc<Settings>('settings.get').then(value => { if (!disposed) applySettings(value); }).catch(report);
    const exited = (event: Event) => {
      const detail = (event as CustomEvent<{ installation_id: string; exit_code: number; log_tail: string }>).detail;
      if (detail.exit_code !== 0 && settingsRef.current?.values.show_exit_log !== false) report(`${detail.installation_id} 退出码 ${detail.exit_code}\n${detail.log_tail}`);
      void refresh().catch(report);
    };
    window.addEventListener('ogplay-exit', exited);
    return () => { disposed = true; ++generation.current; window.removeEventListener('ogplay-exit', exited); };
  }, []);
  async function act(method: string, game: LibraryItem, kind?: string) {
    if (inFlight.current) return;
    inFlight.current = true; setBusy(true);
    try {
      await rpc(method, { installation_id: game.installation_id, ...(kind ? { kind } : {}) });
      await refresh();
    } catch (reason) { report(reason); }
    finally { inFlight.current = false; setBusy(false); }
  }
  const select = (game: LibraryItem) => { lastSelected.current = game.installation_id; setSelected(game.installation_id); };
  const intake = (files: File[]) => {
    const error = validatePackageSelection(files);
    if (importing) return;
    if (error) report(error); else { setPendingFile(files[0]); setImporting(true); }
  };
  const isFileDrag = (event: DragEvent) => Array.from(event.dataTransfer?.types ?? []).includes('Files');
  const resetFilters = () => { setQuery(''); setFilter('all'); search.current?.focus(); };
  const importButton = <button class="primary" disabled={importing} onClick={() => { setPendingFile(null); setImporting(true); }}>＋ 选择安装包</button>;
  if (settingsOpen) return <SettingsPage onBack={() => { setSettingsOpen(false); void refresh().catch(report); }} onSaved={applySettings} />;
  if (gameSettingsId) return <GameSettingsPage id={gameSettingsId} onBack={() => { setGameSettingsId(''); void refresh().catch(report); }} />;
  return <div class={`shell ${dragging ? 'dragging' : ''}`}
    onDragEnter={event => { if (isFileDrag(event)) { event.preventDefault(); ++dragDepth.current; setDragging(true); } }}
    onDragOver={event => { if (isFileDrag(event)) { event.preventDefault(); if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'; } }}
    onDragLeave={event => { event.preventDefault(); if (--dragDepth.current <= 0) { dragDepth.current = 0; setDragging(false); } }}
    onDrop={event => { event.preventDefault(); dragDepth.current = 0; setDragging(false); if (isFileDrag(event)) intake(Array.from(event.dataTransfer?.files ?? [])); }}>
    <nav class="navigation" aria-label="主导航">
      <div class="brand"><span>O</span><div><b>OGPlay</b><small>安卓经典游戏</small></div></div>
      <div class="nav-selected" aria-current="page"><span>▦</span> 游戏库 <span class="nav-count">{library.items.length}</span></div>
      <button class="settings-entry" disabled={busy || importing} onClick={() => setSettingsOpen(true)}>⚙ 设置</button>
      <div class="nav-bottom"><b>你的游戏，你的存档</b><small>关闭启动器后，游戏继续运行</small></div>
    </nav>
    <main>
      <header class="topbar"><h1>游戏库</h1><span class="subtle">{library.items.length} 个安装实例</span>
        <div class="search-box"><span aria-hidden="true">⌕</span><input ref={search} type="search" placeholder="搜索名称、包名或版本" aria-label="搜索游戏" value={query} onInput={event => setQuery(event.currentTarget.value)} /></div>
        <div class="view-switch" role="group" aria-label="库视图"><button aria-label="网格视图" aria-pressed={mode === 'grid'} onClick={() => setMode('grid')}>▦</button><button aria-label="列表视图" aria-pressed={mode === 'list'} onClick={() => setMode('list')}>☷</button></div>
        <button disabled={busy || loading} onClick={() => void refresh().catch(report)}>刷新</button>{importButton}
      </header>
      <div class="filterbar"><div class="filter-chips" role="group" aria-label="筛选游戏">
        {([['all', '全部'], ['launchable', '可启动'], ['running', '运行中'], ['attention', '需检查']] as const).map(([value, label]) =>
          <button key={value} aria-pressed={filter === value} onClick={() => setFilter(value)}>{label}</button>)}
        </div><span class="result-count" role="status">{items.length} / {library.items.length} 个</span>
        <label class="sort-label">排序 <select aria-label="排序" value={sort} onChange={event => setSort(event.currentTarget.value as Sort)}><option value="name">名称</option><option value="newest">最近导入</option><option value="oldest">最早导入</option></select></label>
      </div>
      <div class="workspace"><section class="library-content" aria-label="游戏条目" aria-busy={loading}>
        {loading ? <div class="empty-state"><h2>正在读取游戏库…</h2></div> : library.items.length === 0 ?
          <div class="empty-state drop-target"><span class="empty-symbol" aria-hidden="true">＋</span><h2>把经典游戏带回来</h2><p>拖入 APK，或选择安装包</p>{importButton}<small>支持单体 APK；分包格式暂不支持</small></div> : items.length === 0 ?
          <div class="empty-state"><h2>没有符合条件的游戏</h2><p>试试其他关键词，或清除当前筛选。</p><button onClick={resetFilters}>清除筛选</button></div> : mode === 'grid' ?
          <div class="game-grid">{items.map(game => <article key={game.installation_id} class={`game-card ${selected === game.installation_id ? 'selected' : ''}`}>
            <button class="select-game card-select" data-instance={game.installation_id} aria-label={`查看 ${game.display_name}`} aria-pressed={selected === game.installation_id} onClick={() => select(game)}>
              <div class="cover"><GameIcon item={game} glow /></div><div class="card-meta"><strong title={game.display_name}>{game.display_name}</strong><small title={versionLabel(game)}>{versionLabel(game)}</small></div>
            </button><div class="card-status"><Status item={game} />{game.running && game.status !== 'running' && <span class="badge running">运行中</span>}</div>
            <button class="quick-play" aria-label={`启动 ${game.display_name}`} title={game.can_launch ? '启动游戏' : game.detail} disabled={busy || !game.can_launch} onClick={() => void act('library.launch', game)}>▶</button>
          </article>)}</div> :
          <div class="table-scroll"><table><thead><tr><th>游戏 / 包名</th><th>版本</th><th>状态</th><th>Profile</th><th>数据包</th><th>导入时间</th><th>操作</th></tr></thead><tbody>{items.map(game =>
            <tr key={game.installation_id} class={selected === game.installation_id ? 'selected' : ''}>
              <td><button class="select-game list-select" data-instance={game.installation_id} aria-label={`查看 ${game.display_name}`} aria-pressed={selected === game.installation_id} onClick={() => select(game)}><GameIcon item={game} /><span><strong title={game.display_name}>{game.display_name}</strong><small class="mono">{game.package || game.installation_id}</small></span></button></td>
              <td>{versionLabel(game)}</td><td><Status item={game} />{game.running && game.status !== 'running' && <small>运行中</small>}</td><td title={game.profile.detail}>{game.profile.value}</td><td title={game.external.detail}>{game.external.value}</td><td class="mono">{game.imported_at || '未记录'}</td>
              <td><button disabled={busy || !game.can_launch} onClick={() => void act('library.launch', game)} aria-label={`启动 ${game.display_name}`}>▶ 启动</button></td>
            </tr>)}</tbody></table></div>}
      </section>
      {item && <aside class="drawer" aria-label="游戏详情"><div class="drawer-header"><span>游戏详情</span><button class="icon-button" aria-label="关闭详情" onClick={closeDrawer}>×</button></div>
        <div class="drawer-scroll"><div class="hero"><GameIcon item={item} /><div><h2>{item.display_name}</h2><p class="mono">{item.package || '包名不可用'}</p><small>{versionLabel(item)}</small></div></div>
          <div class="detail-status"><Status item={item} />{item.running && item.status !== 'running' && <span class="badge running">运行中</span>}</div>
          <button class="primary launch-button" disabled={busy || !item.can_launch} onClick={() => void act('library.launch', item)}>{item.running ? '游戏运行中' : '▶ 启动游戏'}</button><p class="condition-detail">{item.detail}</p>
          <div class="detail-actions"><button disabled={busy || importing} onClick={() => setGameSettingsId(item.installation_id)}>游戏设置</button><button disabled title="运行监控暂未开放">Dashboard · 暂未开放</button><button disabled={busy} onClick={() => void act('library.open_dir', item, 'sandbox')}>打开沙盒目录</button><button disabled={busy} onClick={() => void act('library.open_dir', item, 'log')}>打开日志目录</button></div>
          <h3>安装信息</h3><dl><dt>安装实例</dt><dd class="mono">{item.installation_id}</dd><dt>Profile</dt><dd>{item.profile.value}<small>{item.profile.detail}</small></dd><dt>数据包</dt><dd>{item.external.value}<small>{item.external.detail}</small></dd><dt>沙盒路径</dt><dd class="mono">{item.sandbox_path}</dd><dt>日志目录</dt><dd class="mono">{item.log_directory}</dd><dt>导入时间</dt><dd>{item.imported_at || '未记录'}</dd></dl>
        </div></aside>}
      </div><footer class="statusbar"><span title={library.library_root}>{library.library_root}</span><span>{library.items.filter(game => game.running).length} 个运行中</span></footer>
    </main>
    {dragging && <div class="drop-overlay" aria-live="polite"><div><span>↓</span><h2>松开以选择安装包</h2><p>单体 APK · 一次一个文件</p></div></div>}
    {importing && <ImportWizard defaultExternal={String(settings?.values.default_external_dir ?? '')} file={pendingFile} onClose={() => { setImporting(false); setPendingFile(null); }} onImported={async id => {
      setQuery(''); setFilter('all'); await refresh(); lastSelected.current = id; setSelected(id); setImporting(false); setPendingFile(null);
    }} />}
    {!importing && errors.length > 0 && <Modal title="操作提示" onDismiss={() => setErrors(queue => queue.slice(1))}><pre>{errors[0]}</pre></Modal>}
  </div>;
}
render(<App />, document.getElementById('app')!);
