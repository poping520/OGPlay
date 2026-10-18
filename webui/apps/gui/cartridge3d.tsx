import { useEffect, useRef, useState } from 'preact/hooks';
import type { Cart3D, CartFace } from './cart3d/api';
import { Cartridge, iconSampled, useIconColor } from './cartridge';
import { versionLabel } from './library';
import type { LibraryItem } from './rpc';

// Resolved while gui.js runs, so cart3d.js is found next to gui.js rather than next to the page.
const bundle = new URL('cart3d.js', (document.currentScript as HTMLScriptElement | null)?.src ?? location.href).href;
let loading: Promise<Cart3D> | null = null;
const loadCart3D = () => loading ??= new Promise<void>((resolve, reject) => {
  const script = document.createElement('script');
  script.src = bundle;
  script.onload = () => resolve();
  script.onerror = () => reject(new Error('cart3d.js 缺失或无法加载。'));
  document.head.append(script);
}).then(() => window.OGPlayCart3D ?? Promise.reject(new Error('cart3d.js 没有注册 3D 卡带。')));

// The 3D bundle loads only when the setting asks for it. While the setting or the bundle is still on its way the
// cartridge slots stay empty ('waiting') instead of flashing the CSS cartridge; only "css" or a failure (missing
// file, no WebGL, reported once) shows the CSS cartridge.
export type CartMode = 'pending' | '3d' | 'css';
export function useCart3D(mode: CartMode, report: (reason: unknown) => void): Cart3D | 'waiting' | null {
  const [api, setApi] = useState<Cart3D | 'failed' | null>(null);
  useEffect(() => {
    setApi(null);
    if (mode !== '3d') return;
    let live = true;
    loadCart3D().then(value => { if (live) setApi(value); }, reason => {
      if (!live) return;
      setApi('failed');
      report(`3D 卡带不可用，已改用 CSS 卡带。\n${reason instanceof Error ? reason.message : String(reason)}`);
    });
    return () => { live = false; };
  }, [mode]);
  return mode === 'css' || api === 'failed' ? null : api ?? 'waiting';
}

// Icons are large data URLs; a per-icon serial keeps face keys short.
const iconIds = new Map<string, number>();
export function cartFace(item: LibraryItem, tone: string, color: [number, number, number]): CartFace {
  if (!iconIds.has(item.icon)) iconIds.set(item.icon, iconIds.size);
  const face = {
    id: item.installation_id, name: item.display_name, package: item.package || item.installation_id, version: versionLabel(item),
    imported: item.imported_at?.slice(0, 10) ?? '', profile: item.profile.value, color, tone, status: item.status,
  };
  return { ...face, icon: item.icon, key: JSON.stringify([face, iconIds.get(item.icon)]) };
}

// Waits for the icon colour so the first look is not printed in the neutral fallback tint.
function useFace(item: LibraryItem, tone: string) {
  const color = useIconColor(item.icon);
  return iconSampled(item.icon) ? cartFace(item, tone, color) : null;
}
// `shown` reports the first drawn cartridge, so the splash can stay up until the hero is really there.
export function Hero3D({ api, item, tone, shown }: { api: Cart3D | 'waiting'; item: LibraryItem; tone: string; shown: () => void }) {
  const stage = useRef<HTMLDivElement>(null);
  const face = useFace(item, tone);
  useEffect(() => api === 'waiting' ? undefined : api.mount(stage.current!, shown), [api]);
  useEffect(() => { if (face && api !== 'waiting') api.show(face); }, [api, face?.key]);
  return <div class="stage3d" ref={stage} aria-hidden="true" />;
}
// Stills already rendered this session show at once when a tile remounts (filter or search changes).
const shownStills = new Map<string, string>();
// Rail tile: a rendered still once available (the previous one stays while a changed state re-renders); empty
// while 3D is on its way, CSS only when 3D is off or failed. `ready` fires once the tile shows its cartridge.
export function TileCart({ api, item, tone, ready }: { api: Cart3D | 'waiting' | null; item: LibraryItem; tone: string; ready?: () => void }) {
  const face = useFace(item, tone);
  const [still, setStill] = useState(() => face ? shownStills.get(face.key) ?? '' : '');
  useEffect(() => {
    if (!api || api === 'waiting' || !face) return;
    let live = true;
    api.thumb(face).then(url => { shownStills.set(face.key, url); if (live) setStill(url); }, () => { if (live) setStill('failed'); });
    return () => { live = false; };
  }, [api, face?.key]);
  const done = !api || Boolean(still);
  useEffect(() => { if (done) ready?.(); }, [done]);
  if (!api || still === 'failed') return <Cartridge item={item} tone={tone} />;
  return still ? <img class="tile-3d" src={still} alt="" /> : <span class="tile-3d" />;
}
