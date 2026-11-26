import { useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { t } from '../shared/i18n';

// The About page's call to star the project on GitHub. The host opens the fixed repository page in the system
// browser (about.open_repository); the page never passes a URL. Twinkling sparkles and a breathing star draw the
// eye; a press spins the star once. All motion stops under prefers-reduced-motion.
const STAR = 'M12 2.6l2.83 5.94 6.52.83-4.8 4.5 1.24 6.46L12 17.17l-5.79 3.16 1.24-6.46-4.8-4.5 6.52-.83z';
const GITHUB = 'M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 0-.27.01-1.13.01-2.2 0-.75-.25-1.23-.54-1.48 1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15.08-.2.36-1.02-.08-2.12 0 0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27-.68 0-1.36.09-2 .27-1.53-1.03-2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 3.64 3.95-.23.2-.44.55-.51 1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9.82 1.13.16.45.68 1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-4.42 3.58-8 8-8Z';
const SPARK = 'M5 0c.3 2.7 2.3 4.7 5 5-2.7.3-4.7 2.3-5 5-.3-2.7-2.3-4.7-5-5 2.7-.3 4.7-2.3 5-5z';

export function StarCard({ onError }: { onError: (message: string) => void }) {
  const [spin, setSpin] = useState(0);
  const open = () => {
    setSpin(value => value + 1);
    void rpc('about.open_repository').catch(reason => onError(reason instanceof Error ? reason.message : String(reason)));
  };
  return <button type="button" class="star-card" onClick={open}>
    <span class="star-orb" aria-hidden="true">
      <svg key={spin} class={`star-glyph${spin ? ' spun' : ''}`} viewBox="0 0 24 24"><path d={STAR} /></svg>
      {[0, 1, 2].map(index => <svg key={index} class={`star-spark s${index}`} viewBox="0 0 10 10"><path d={SPARK} /></svg>)}
    </span>
    <span class="star-copy">
      <b>{t('settings.star.title')}</b>
      <small>{t('settings.star.body')}</small>
    </span>
    <span class="star-cta">
      <svg viewBox="0 0 16 16" aria-hidden="true"><path d={GITHUB} /></svg>
      <span>Star</span>
      <em>poping520/OGPlay</em>
    </span>
  </button>;
}
