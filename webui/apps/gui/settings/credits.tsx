import { useEffect, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { t } from '../shared/i18n';
import { Note } from './settings-ui';

// Open-source acknowledgements: the host lists the projects shipped with OGPlay (data/credits, written by
// tools/credits.py) and reads a project's license texts only when its row is opened.
export interface CreditProject { id: string; name: string; version: string; url: string; license: string | null }
interface CreditNotices { id: string; notices: { title: string; text: string }[] }

export function creditSummary(project: CreditProject): string {
  return [project.version, project.license ?? t('多种许可证')].filter(Boolean).join(' · ');
}

function Project({ project }: { project: CreditProject }) {
  const [notices, setNotices] = useState<CreditNotices['notices'] | null>(null);
  const [error, setError] = useState('');
  const load = (open: boolean) => {
    if (!open || notices) return;
    rpc<CreditNotices>('about.notices', { id: project.id }).then(result => setNotices(result.notices),
      reason => setError(reason instanceof Error ? reason.message : String(reason)));
  };
  return <details class="fact credit" onToggle={event => load((event.currentTarget as HTMLDetailsElement).open)}>
    <summary><span>{project.name}</span><small>{creditSummary(project)}</small></summary>
    <p class="credit-url">{project.url}</p>
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
    <p>{t('OGPlay 使用了以下开源项目，感谢它们的作者与贡献者。展开可查看许可证全文。')}</p>
    {error ? <p class="credit-error">{error}</p> : projects?.map(project => <Project key={project.id} project={project} />)}
  </Note>;
}
