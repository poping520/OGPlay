import { useEffect, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { describeData, type DataDirectory } from './import';

/** Host listing of a data directory; null while loading, error text on failure. */
export function useDataDirectory(path: string): { data: DataDirectory | null; error: string } {
  const [data, setData] = useState<DataDirectory | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    setData(null); setError('');
    if (!path) return;
    let current = true;
    rpc<DataDirectory>('library.external.inspect', { path })
      .then(value => { if (current) setData(value); })
      .catch(reason => { if (current) setError(reason instanceof Error ? reason.message : String(reason)); });
    return () => { current = false; };
  }, [path]);
  return { data, error };
}

/** The data directory's real top-level entries drawn under the guest path they appear at. */
export function MountPreview({ caption, guest, invalid, data, error }: {
  caption: string; guest: string; invalid?: boolean; data: DataDirectory | null; error: string;
}) {
  const shown = data?.entries ?? [];
  const hidden = data ? data.entry_count - shown.length : 0;
  return <figure class="iw-mount" aria-label="挂载预览">
    <figcaption><span>{caption}</span>{data && <small>{describeData(data)}</small>}</figcaption>
    <code class={invalid ? 'iw-root bad' : 'iw-root'}>{invalid ? '挂载位置无效，修正后显示' : `${guest}/`}</code>
    {error ? <p class="iw-note bad">{error}</p> : !data ? <p class="iw-note">正在读取目录…</p> :
      <ul class="iw-tree">
        {shown.map((entry, index) => <li><i>{index === shown.length - 1 && hidden <= 0 ? '└─' : '├─'}</i><code>{entry.name}{entry.directory ? '/' : ''}</code></li>)}
        {hidden > 0 && <li class="more"><i>└─</i>另 {hidden} 项</li>}
        {!data.entry_count && <li class="more"><i>└─</i>空目录</li>}
      </ul>}
  </figure>;
}
