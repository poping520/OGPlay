import { useEffect, useState } from 'preact/hooks';
import { shellPalette, versionLabel, vividIconColor } from './library';
import type { LibraryItem } from './rpc';
import logo from './assets/logo.png';

type Rgb = [number, number, number];
const fallback: Rgb = [124, 156, 255];
const sampled = new Map<string, Rgb>();
// Stacked silhouettes give the hero shell real thickness with rounded corners; tiles stay shallow.
const HERO_LAYERS = 14, TILE_LAYERS = 6;

// Icons are host-provided data URLs, so sampling never taints the canvas; results are cached per icon.
export function useIconColor(icon: string): Rgb {
  const [color, setColor] = useState<Rgb>(() => sampled.get(icon) ?? fallback);
  useEffect(() => {
    const known = sampled.get(icon);
    if (known || !icon) { setColor(known ?? fallback); return; }
    let live = true;
    const image = new Image();
    image.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 16;
      const context = canvas.getContext('2d', { willReadFrequently: true });
      let value = fallback;
      try { if (context) { context.drawImage(image, 0, 0, 16, 16); value = vividIconColor(context.getImageData(0, 0, 16, 16).data); } }
      catch { /* Keep the neutral shell if a resource cannot be sampled. */ }
      sampled.set(icon, value);
      if (live) setColor(value);
    };
    image.src = icon;
    return () => { live = false; };
  }, [icon]);
  return color;
}
export function shellStyle(color: Rgb): Record<string, string> {
  const palette = shellPalette(color);
  return { '--shell': palette.shell, '--shell-l': palette.light, '--shell-d': palette.dark };
}
function Sticker({ item }: { item: LibraryItem }) {
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [item.icon]);
  return <span class="cart-sticker">
    <span class="cart-art">
      {item.icon && !failed ? <img src={item.icon} alt="" onError={() => setFailed(true)} /> :
        <span class="cart-placeholder">{[...item.display_name][0] ?? 'O'}</span>}
      <span class="cart-foil" />
    </span>
    <span class="cart-title">
      <span class="cart-title-text"><b>{item.display_name}</b><small>{versionLabel(item)} · {item.package || item.installation_id}</small></span>
      <img src={logo} alt="" />
    </span>
    <span class="cart-gloss" />
  </span>;
}
// CSS cartridge modelled on the concept's 3D shell: tinted two-piece body with real depth, status LED pipe,
// debossed brand, recessed box-art sticker with title strip, keyed bottom plug whose gold contacts mark a
// complete data set. Wear follows host facts only: damaged entries are scuffed, missing data packs lose the gold.
export function Cartridge({ item, tone, hero = false }: { item: LibraryItem; tone: string; hero?: boolean }) {
  const color = useIconColor(item.icon);
  const wear = item.status === 'damaged' ? 'worn' : item.status === 'missing_external' ? 'no-data' : '';
  const layers = hero ? HERO_LAYERS : TILE_LAYERS;
  return <span class={`cartridge ${hero ? 'hero-cart' : 'tile-cart'} ${wear}`} style={{ ...shellStyle(color), '--layers': String(layers) }} aria-hidden="true">
    <span class="cart-depth">{Array.from({ length: layers }, (_, index) =>
      <i key={index} class={index === Math.floor(layers / 2) ? 'seam' : ''} style={{ '--i': String(index + 1) }} />)}</span>
    {hero && <><span class="cart-side left" /><span class="cart-side right" /><span class="cart-top" /></>}
    <span class="cart-front">
      <span class="cart-shell" />
      <span class="cart-plug"><span class="cart-pins" /></span>
      <span class={`cart-led ${tone}`} />
      <span class="cart-brand">OGPLAY</span>
      <span class="cart-window"><Sticker item={item} /></span>
      <span class="cart-key" />
      {wear === 'worn' && <span class="cart-scuff" />}
    </span>
  </span>;
}
