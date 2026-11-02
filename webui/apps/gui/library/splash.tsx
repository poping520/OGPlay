import { useEffect, useRef, useState } from 'preact/hooks';
import logo from '../assets/logo.png';
import { t } from '../shared/i18n';

// Launch splash: icon and wordmark stay up at least SPLASH_MIN_MS and until the main screen is ready (library,
// settings, and with 3D the hero's first frame and the first rail stills), never longer than SPLASH_MAX_MS.
const SPLASH_MIN_MS = 900, SPLASH_MAX_MS = 8000;
export function Splash({ ready, onDone }: { ready: boolean; onDone: () => void }) {
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
  return <div class={`splash glass-sky ${leaving ? 'leaving' : ''}`} role="status" aria-label={t('OGPlay 正在启动')}>
    <img src={logo} alt="" /><b><span>OG</span>Play</b><small>{t('Android 经典游戏兼容层')}</small><i class="splash-bar" />
  </div>;
}
