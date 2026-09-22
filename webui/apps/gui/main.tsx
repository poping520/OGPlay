import { render } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { rpc, type Library, type LibraryItem } from './rpc';
import '../../packages/ui-kit/theme.css';

const statusNames: Record<string, string> = { damaged: '条目损坏', profile_catalog_unavailable: 'Profile 不可用', missing_profile: '无 Profile', missing_external: '缺少数据包', running: '运行中', ready: '就绪' };
function App() {
  const [library, setLibrary] = useState<Library>({ items: [], library_root: '' });
  const [selected, setSelected] = useState('');
  const [errors, setErrors] = useState<string[]>([]);
  const error = errors[0] ?? '';
  const setError = (message: string) => setErrors(queue => message ? [...queue, message] : queue.slice(1));
  const [busy, setBusy] = useState(false);
  const item = library.items.find(item => item.installation_id === selected);
  async function refresh() {
    const result = await rpc<Library>('library.list');
    setLibrary(result);
    setSelected(old => result.items.some(item => item.installation_id === old) ? old : result.items[0]?.installation_id ?? '');
  }
  useEffect(() => {
    let disposed = false;
    const initial = async () => {
      for (let i = 0; i < Math.max(1, window.__ogplaySmoke ?? 0) && !disposed; ++i) {
        await refresh();
        await new Promise<void>(resolve => requestAnimationFrame(() => resolve()));
      }
    };
    void initial().catch(reason => setError(String(reason)));
    const exited = (event: Event) => {
      const detail = (event as CustomEvent<{ installation_id: string; exit_code: number; log_tail: string }>).detail;
      if (detail.exit_code !== 0) setError(`${detail.installation_id} 退出码 ${detail.exit_code}\n${detail.log_tail}`);
      void refresh().catch(reason => setError(String(reason)));
    };
    window.addEventListener('ogplay-exit', exited);
    return () => { disposed = true; window.removeEventListener('ogplay-exit', exited); };
  }, []);
  async function act(method: string, item: LibraryItem, kind?: string) {
    setBusy(true);
    try {
      await rpc(method, { installation_id: item.installation_id, ...(kind ? { kind } : {}) });
      await refresh();
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(false); }
  }
  return <div class="shell">
    <nav><div class="brand"><span>O</span><div>OGPlay<small>安卓经典游戏</small></div></div><div class="nav-selected">游戏库</div><footer>GUI v2 · 宿主骨架</footer></nav>
    <main><header><div><h1>游戏库</h1><p>{library.items.length} 个安装实例</p></div><button disabled={busy} onClick={() => void refresh().catch(reason => setError(String(reason)))}>刷新</button></header>
      <div class="workspace"><section class="games">{library.items.length === 0 ? <div class="empty"><h2>游戏库为空</h2><p>导入向导将在 GUI-V2-03 接入。</p></div> : library.items.map(game =>
        <button class={`game ${selected === game.installation_id ? 'selected' : ''}`} key={game.installation_id} onClick={() => setSelected(game.installation_id)}>
          <div class="cover">{game.icon ? <img src={game.icon} alt="" /> : <span>O</span>}</div>
          <strong title={game.display_name}>{game.display_name}</strong><small>{game.version}</small><span class="badge">{statusNames[game.status] ?? game.status}</span>
        </button>)}</section>
      {item && <aside><h2>{item.display_name}</h2><p class="mono">{item.package}</p><p>{item.version}</p><button class="primary" disabled={busy || !item.can_launch} onClick={() => void act('library.launch', item)}>▶ 启动</button><p>{item.detail}</p>
        <dl><dt>安装实例</dt><dd>{item.installation_id}</dd><dt>Profile</dt><dd>{item.profile.value}<small>{item.profile.detail}</small></dd><dt>数据包</dt><dd>{item.external.value}<small>{item.external.detail}</small></dd></dl>
        <div class="actions"><button disabled={busy} onClick={() => void act('library.open_dir', item, 'sandbox')}>打开沙盒目录</button><button disabled={busy} onClick={() => void act('library.open_dir', item, 'log')}>打开日志目录</button></div>
      </aside>}</div><div class="root-path" title={library.library_root}>{library.library_root}</div>
    </main>{error && <div class="overlay"><section role="alertdialog" aria-modal="true" aria-label="操作提示"><h2>操作提示</h2><pre>{error}</pre><button autoFocus onClick={() => setError('')}>关闭</button></section></div>}
  </div>;
}
render(<App />, document.getElementById('app')!);
