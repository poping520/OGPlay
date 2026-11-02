import { useEffect, useState } from 'preact/hooks';

// Top-bar clock, minute precision.
export function Clock() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const timer = window.setInterval(() => setNow(new Date()), 10000); return () => window.clearInterval(timer); }, []);
  return <span class="clock">{now.toTimeString().slice(0, 5)}</span>;
}
