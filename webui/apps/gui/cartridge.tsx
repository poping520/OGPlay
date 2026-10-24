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
      // 32 px keeps the background ring and the subject apart; at 16 px small icons blur them together.
      canvas.width = canvas.height = 32;
      const context = canvas.getContext('2d', { willReadFrequently: true });
      let value = fallback;
      try { if (context) { context.drawImage(image, 0, 0, 32, 32); value = vividIconColor(context.getImageData(0, 0, 32, 32).data, 32); } }
      catch { /* Keep the neutral shell if a resource cannot be sampled. */ }
      sampled.set(icon, value);
      if (live) setColor(value);
    };
    image.src = icon;
    return () => { live = false; };
  }, [icon]);
  return color;
}
export const iconSampled = (icon: string) => !icon || sampled.has(icon);
export const knownIconColor = (icon: string): Rgb | null => iconSampled(icon) ? sampled.get(icon) ?? fallback : null;
// Backdrop art: the icon blurred and saturated once into a tiny bitmap that CSS only scales. A CSS blur of this size
// is re-rasterised on every image change (and every frame of a crossfade) in the system WebView, stalling the hero.
const BACKDROP_W = 64, BACKDROP_H = 40;
const backdrops = new Map<string, Promise<string>>();
function blurredArt(icon: string): Promise<string> {
  let art = backdrops.get(icon);
  if (!art) backdrops.set(icon, art = new Promise<string>(resolve => {
    const image = new Image();
    image.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = BACKDROP_W; canvas.height = BACKDROP_H;
      const context = canvas.getContext('2d', { willReadFrequently: true });
      if (!context) { resolve(''); return; }
      const side = BACKDROP_W;                          // cover: the square icon fills the width
      context.drawImage(image, 0, (BACKDROP_H - side) / 2, side, side);
      const pixels = context.getImageData(0, 0, BACKDROP_W, BACKDROP_H);
      boxBlur(pixels.data, BACKDROP_W, BACKDROP_H, 3, 3);
      for (let i = 0; i < pixels.data.length; i += 4) {   // saturate 1.5
        const d = pixels.data, luma = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
        for (let c = 0; c < 3; c++) d[i + c] = luma + (d[i + c] - luma) * 1.5;
      }
      context.putImageData(pixels, 0, 0);
      resolve(canvas.toDataURL('image/png'));
    };
    image.onerror = () => resolve('');
    image.src = icon;
  }));
  return art;
}
// Separable box blur, repeated so it approximates a gaussian; edges clamp.
function boxBlur(data: Uint8ClampedArray, width: number, height: number, radius: number, passes: number) {
  const buffer = new Float32Array(width * height * 4);
  const run = (count: number, length: number, at: (line: number, index: number) => number) => {
    for (let line = 0; line < count; line++) for (let c = 0; c < 4; c++) {
      let sum = 0;
      for (let k = -radius; k <= radius; k++) sum += data[at(line, Math.min(length - 1, Math.max(0, k))) + c];
      for (let index = 0; index < length; index++) {
        buffer[at(line, index) + c] = sum / (radius * 2 + 1);
        sum += data[at(line, Math.min(length - 1, index + radius + 1)) + c] - data[at(line, Math.max(0, index - radius)) + c];
      }
      for (let index = 0; index < length; index++) data[at(line, index) + c] = buffer[at(line, index) + c];
    }
  };
  for (let pass = 0; pass < passes; pass++) {
    run(height, width, (y, x) => (y * width + x) * 4);
    run(width, height, (x, y) => (y * width + x) * 4);
  }
}
export function useBackdropArt(icon: string): string | null {
  const [art, setArt] = useState<{ icon: string; url: string } | null>(null);
  useEffect(() => {
    if (!icon) { setArt({ icon, url: '' }); return; }
    let live = true;
    void blurredArt(icon).then(url => { if (live) setArt({ icon, url }); });
    return () => { live = false; };
  }, [icon]);
  return art?.icon === icon ? art.url : null;
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
