import { useEffect, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { t } from '../shared/i18n';
import { Note } from './settings-ui';

// Open-source acknowledgements: the host lists the projects shipped with OGPlay (data/credits, written by
// tools/credits.py) and reads a project's license texts only when its row is opened. Projects OGPlay uses itself
// are the top level; the ones it only gets through them fold inside their parent.
export interface CreditProject { id: string; name: string; version: string; url: string; original_url?: string; license: string | null; parent?: string }
export interface CreditNode { project: CreditProject; children: CreditNode[] }
interface CreditNotices { id: string; notices: { title: string; text: string }[] }

export function creditTree(projects: CreditProject[]): CreditNode[] {
  const nodes = new Map(projects.map(project => [project.id, { project, children: [] as CreditNode[] }]));
  const roots: CreditNode[] = [];
  for (const node of nodes.values()) (nodes.get(node.project.parent ?? '')?.children ?? roots).push(node);
  return roots;
}

function dependencyCount(node: CreditNode): number {
  return node.children.reduce((total, child) => total + 1 + dependencyCount(child), 0);
}

export function creditSummary(node: CreditNode): string {
  const count = dependencyCount(node);
  return [node.project.version, node.project.license ?? t('多种许可证'),
    count > 1 ? t('{n} 个依赖', { n: count }) : count ? t('1 个依赖') : ''].filter(Boolean).join(' · ');
}

function Project({ node }: { node: CreditNode }) {
  const { project, children } = node;
  const [notices, setNotices] = useState<CreditNotices['notices'] | null>(null);
  const [error, setError] = useState('');
  const load = (open: boolean) => {
    if (!open || notices) return;
    rpc<CreditNotices>('about.notices', { id: project.id }).then(result => setNotices(result.notices),
      reason => setError(reason instanceof Error ? reason.message : String(reason)));
  };
  return <details class="fact credit" onToggle={event => load((event.currentTarget as HTMLDetailsElement).open)}>
    <summary><span>{project.name}</span><small>{creditSummary(node)}</small></summary>
    {project.original_url ? <>
      <p class="credit-url">{t('当前使用仓库')}：{project.url}</p>
      <p class="credit-url">{t('原始仓库')}：{project.original_url}</p>
    </> : <p class="credit-url">{project.url}</p>}
    {children.length > 0 && <><h5>{t('依赖项')}</h5>{children.map(child => <Project key={child.project.id} node={child} />)}<h5>{t('许可证')}</h5></>}
    {error ? <pre>{error}</pre> : !notices ? <pre>{t('正在读取…')}</pre> : notices.map(notice =>
      <div key={notice.title}>{notices.length > 1 && <h5>{notice.title}</h5>}<pre>{notice.text}</pre></div>)}
  </details>;
}

export function Credits() {
  const [projects, setProjects] = useState<CreditProject[] | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    rpc<{ projects: CreditProject[] }>('about.credits').then(result => setProjects(result.projects),
      reason => setError(reason instanceof Error ? reason.message : String(reason)));
  }, []);
  return <Note icon="info" title={t('开源致谢')}>
    <p>{t('OGPlay 使用了以下开源项目，感谢它们的作者与贡献者。展开可查看许可证全文及其依赖的项目。')}</p>
    {error ? <p class="credit-error">{error}</p> : projects && creditTree(projects).map(node => <Project key={node.project.id} node={node} />)}
  </Note>;
}
