// Cartridge kit: loads cartridge.glb (tools/build_cartridge_model.py), prints the front sticker, rear install
// record and spine per library entry, and builds the per-entry materials. Prints use host facts only.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { mainThreadDraco } from './draco';
import type { CartFace } from './api';
import glb from '../assets/cartridge.glb';
import aoPng from '../assets/cartridge-ao.png';
import logoPng from '../assets/logo.png';

export { THREE };
const LED_COLOR: Record<string, string> = { ready: '#3ddc84', running: '#4aa3ff', warning: '#ffb020', danger: '#ff5a52' };
const FONT = '"PingFang SC", "Microsoft YaHei", "Noto Sans CJK SC", system-ui, sans-serif';
const MONO = 'ui-monospace, "SF Mono", Menlo, Consolas, monospace';
type Ctx = CanvasRenderingContext2D;
type Mesh = THREE.Mesh<THREE.BufferGeometry, THREE.MeshStandardMaterial>;

const loadImage = (src: string) => new Promise<HTMLImageElement>((ok, bad) => {
  const image = new Image(); image.onload = () => ok(image); image.onerror = bad; image.src = src;
});
const canvas = (width: number, height: number) => {
  const element = document.createElement('canvas'); element.width = width; element.height = height;
  return [element, element.getContext('2d', { willReadFrequently: true })!] as const;
};
function canvasTexture(source: HTMLCanvasElement, color = true) {
  const texture = new THREE.CanvasTexture(source);
  texture.colorSpace = color ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  texture.flipY = false;                      // glTF UV convention
  texture.anisotropy = 8;
  return texture;
}
// Damaged entries print faded; done on pixels because canvas filters are not available in every system WebView.
function fade(x: Ctx, width: number, height: number, bright: number) {
  const image = x.getImageData(0, 0, width, height), data = image.data;
  for (let i = 0; i < data.length; i += 4) {
    const luma = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
    for (let channel = 0; channel < 3; channel++) data[i + channel] = (data[i + channel] * 0.15 + luma * 0.85) * bright;
  }
  x.putImageData(image, 0, 0);
}
// Shrinks a font until the text fits the given width.
function fit(x: Ctx, text: string, weight: number, size: number, family: string, width: number) {
  let px = size;
  do { x.font = `${weight} ${Math.round(px)}px ${family}`; px *= 0.94; } while (px > size * 0.45 && x.measureText(text).width > width);
}
const rng = (seed: string) => {
  let h = 2166136261;
  for (const ch of seed) h = Math.imul(h ^ ch.charCodeAt(0), 16777619);
  return () => ((h = Math.imul(h ^ (h >>> 15), 2246822507) ^ Math.imul(h ^ (h >>> 13), 3266489909)) >>> 0) / 4294967296;
};

export type Kit = Awaited<ReturnType<typeof loadKit>>;
export async function loadKit() {
  // ---------------------------------------------------------------- model
  // Vite inlines the GLB as a data URL; it is decoded by hand because fetch is outside the CSP.
  const bytes = Uint8Array.from(atob(glb.slice(glb.indexOf(',') + 1)), ch => ch.charCodeAt(0));
  const gltf = await new GLTFLoader().setDRACOLoader(mainThreadDraco).parseAsync(bytes.buffer, '');
  const model = gltf.scene;
  const parts: Record<string, Mesh[]> = {};
  model.traverse(object => { const mesh = object as Mesh; if (mesh.isMesh) (parts[mesh.material.name] ||= []).push(mesh); });
  const sizeOf = (name: string) => new THREE.Box3().setFromObject(parts[name][0]).getSize(new THREE.Vector3());
  const modelSize = new THREE.Box3().setFromObject(model).getSize(new THREE.Vector3());
  // meshes sit untransformed under the model root, so geometry bounds are model coordinates
  const boxOf = (name: string) => { const g = parts[name][0].geometry; g.computeBoundingBox(); return g.boundingBox!.clone(); };
  const titleAspect = sizeOf('Title').x / sizeOf('Title').y;
  const backAspect = sizeOf('BackLabel').x / sizeOf('BackLabel').y;
  const spineAspect = sizeOf('Spine').x / sizeOf('Spine').z;
  // where the holo seal sits on the rear sticker, in sticker canvas units (0..1, read from behind)
  const holoSpot = (() => {
    const b = boxOf('BackLabel'), h = boxOf('Holo'), c = h.getCenter(new THREE.Vector3());
    return { u: (b.max.x - c.x) / (b.max.x - b.min.x), v: (b.max.y - c.y) / (b.max.y - b.min.y), r: (h.max.x - h.min.x) / 2 / (b.max.x - b.min.x) };
  })();
  model.position.y = -modelSize.y / 2;     // spin around the cartridge centre
  const [logo, aoImage] = await Promise.all([loadImage(logoPng).catch(() => null), loadImage(aoPng)]);

  // printed sticker edge: a hairline of bare paper, then a soft inner shade, both following the sticker's rounded
  // outline (corner radii exported by the generator). `open` leaves the side bare where two stickers butt together.
  function paperEdge(x: Ctx, W: number, H: number, part: string, open?: 'top' | 'bottom') {
    const r = ((parts[part][0].userData.ogp_corners as number[] | undefined) ?? [0, 0, 0, 0]).map(f => f * W);
    const pad = 40, y0 = open === 'top' ? -pad : 0, h = H + (open ? pad : 0);
    const edge = (style: string, width: number) => { x.strokeStyle = style; x.lineWidth = width; x.beginPath(); x.roundRect(0, y0, W, h, r); x.stroke(); };
    edge('rgba(0,0,0,.2)', 22);
    edge('#efe9dc', 8);
  }
  // a damaged entry: scratched print and (optionally) a corner peeled back to the white paper
  function scuff(x: Ctx, W: number, H: number, seed: string, peel = true) {
    const r = rng(seed);
    x.save(); x.lineCap = 'round';
    for (let i = 0; i < 26; i++) {
      const x0 = r() * W, y0 = r() * H, a = -0.5 + r() * 0.5, len = 60 + r() * 260;
      x.strokeStyle = `rgba(255,255,255,${0.12 + r() * 0.22})`; x.lineWidth = 1 + r() * 2.5;
      x.beginPath(); x.moveTo(x0, y0); x.lineTo(x0 + Math.cos(a) * len, y0 + Math.sin(a) * len); x.stroke();
    }
    if (peel) {
      const cw = Math.min(W * 0.2, H * 1.2), ch = Math.min(H * 0.17, W * 0.2) * (H < W / 2 ? 3 : 1), pts = [[W, H - ch]];
      for (let t = 0.12; t < 1; t += 0.12) pts.push([W - cw * t + (r() - 0.5) * 14, H - ch * (1 - t) + (r() - 0.5) * 14]);
      pts.push([W - cw, H]);
      x.shadowColor = 'rgba(0,0,0,.45)'; x.shadowBlur = 18; x.shadowOffsetX = -6; x.shadowOffsetY = -6;
      x.fillStyle = '#ece6d8'; x.beginPath(); x.moveTo(W, H); pts.forEach(([px, py]) => x.lineTo(px, py)); x.closePath(); x.fill();
    }
    x.restore();
  }

  // ---------------------------------------------------------------- per-entry prints
  const tint = (face: CartFace, amount: number) => `#${new THREE.Color(...face.color.map(v => v / 255) as [number, number, number])
    .lerp(new THREE.Color(amount > 0 ? '#ffffff' : '#000000'), Math.abs(amount)).getHexString()}`;
  // Icon box art: the icon on a gradient of its own colour, or the first letter when the host has no icon.
  function drawIcon(x: Ctx, face: CartFace, icon: HTMLImageElement | null, left: number, top: number, size: number, round: number) {
    x.save(); x.beginPath(); x.roundRect(left, top, size, size, round); x.clip();
    if (icon) x.drawImage(icon, left, top, size, size);
    else {
      x.fillStyle = tint(face, -0.25); x.fillRect(left, top, size, size);
      x.fillStyle = '#ffffff'; x.font = `900 ${Math.round(size * 0.56)}px ${FONT}`; x.textAlign = 'center'; x.textBaseline = 'middle';
      x.fillText([...face.name][0] ?? 'O', left + size / 2, top + size * 0.54);
    }
    x.restore();
  }
  function drawArt(face: CartFace, icon: HTMLImageElement | null, dim: boolean) {
    const W = 1024, H = 1024, [cv, x] = canvas(W, H), cx = W / 2, cy = H / 2;
    const bg = x.createLinearGradient(0, 0, W, H); bg.addColorStop(0, tint(face, 0.28)); bg.addColorStop(1, tint(face, -0.35));
    x.fillStyle = bg; x.fillRect(0, 0, W, H);
    x.save(); x.translate(cx, cy); x.fillStyle = 'rgba(255,255,255,.07)';
    for (let i = 0; i < 18; i++) { x.rotate(Math.PI / 9); x.beginPath(); x.moveTo(0, 0); x.lineTo(W, -W * 0.09); x.lineTo(W, W * 0.09); x.fill(); }
    x.restore();
    const glow = x.createRadialGradient(cx, cy, 0, cx, cy, W * 0.45); glow.addColorStop(0, 'rgba(255,255,255,.32)'); glow.addColorStop(1, 'rgba(255,255,255,0)');
    x.fillStyle = glow; x.fillRect(0, 0, W, H);
    const vig = x.createRadialGradient(cx, cy, W * 0.35, cx, cy, W * 0.8); vig.addColorStop(0, 'rgba(0,0,0,0)'); vig.addColorStop(1, 'rgba(0,0,0,.3)');
    x.fillStyle = vig; x.fillRect(0, 0, W, H);
    const s = W * 0.7, round = s * 0.18;
    x.save(); x.shadowColor = 'rgba(0,0,0,.4)'; x.shadowBlur = 34; x.shadowOffsetY = 16;
    x.fillStyle = 'rgba(0,0,0,.25)'; x.beginPath(); x.roundRect(cx - s / 2, cy - s / 2, s, s, round); x.fill(); x.restore();
    drawIcon(x, face, icon, cx - s / 2, cy - s / 2, s, round);
    if (dim) { fade(x, W, H, 0.75); scuff(x, W, H, face.id, false); }
    paperEdge(x, W, H, 'Art', 'bottom');
    const texture = canvasTexture(cv);
    // foil mask: the holographic film shows fully on the background and only faintly over the icon itself
    const [mc, mx] = canvas(512, 512);
    mx.fillStyle = '#fff'; mx.fillRect(0, 0, 512, 512);
    mx.fillStyle = '#000'; mx.beginPath(); mx.roundRect(256 - s / 4, 256 - s / 4, s / 2, s / 2, round / 2); mx.fill();
    texture.userData.foilMask = canvasTexture(mc, false);
    return texture;
  }
  function drawTitle(face: CartFace, dim: boolean) {
    const W = 1024, H = Math.round(W / titleAspect), [cv, x] = canvas(W, H);
    const bg = x.createLinearGradient(0, 0, 0, H); bg.addColorStop(0, '#141a2c'); bg.addColorStop(1, '#090c16');
    x.fillStyle = bg; x.fillRect(0, 0, W, H);
    x.fillStyle = '#ffc21a'; x.fillRect(0, 0, W, 7);                   // divider where the strip meets the icon
    const ls = H * 0.56, room = W - 46 - ls - 70;
    x.textBaseline = 'alphabetic'; x.fillStyle = '#f2f4fa';
    fit(x, face.name, 900, H * 0.4, FONT, room); x.fillText(face.name, 46, H * 0.57);
    const sub = `${face.version}  ·  ${face.package}`;
    x.fillStyle = '#8f97ad'; fit(x, sub, 800, H * 0.15, FONT, room); x.fillText(sub, 48, H * 0.83);
    if (logo) x.drawImage(logo, W - 46 - ls, (H - ls) / 2 + 6, ls, ls);
    if (dim) { fade(x, W, H, 0.75); scuff(x, W, H, face.id + ':title'); }
    paperEdge(x, W, H, 'Title', 'top');
    return canvasTexture(cv);
  }
  function barcode(x: Ctx, text: string, bx: number, by: number, bw: number, bh: number) {
    let h = 2166136261;
    const bits: number[] = [];
    for (let i = 0; i < 64; i++) { h ^= text.charCodeAt(i % text.length) + i; h = Math.imul(h, 16777619) >>> 0; bits.push(1 + (h % 3)); }
    const unit = bw / bits.reduce((a, b) => a + b + 1, 0);
    let px = bx;
    bits.forEach((w, i) => { if (i % 2 === 0) x.fillRect(px, by, w * unit, bh); px += (w + 1) * unit; });
    return h.toString(16).toUpperCase().padStart(8, '0').slice(0, 6);
  }
  // rear sticker: the install record
  function drawBack(face: CartFace, icon: HTMLImageElement | null, dim: boolean) {
    const W = 1024, H = Math.round(W / backAspect), [cv, x] = canvas(W, H);
    x.fillStyle = '#f3f0e8'; x.fillRect(0, 0, W, H);
    const hh = H * 0.2;
    x.fillStyle = '#141a2c'; x.fillRect(0, 0, W, hh);
    x.font = `900 ${Math.round(hh * 0.46)}px ${FONT}`; x.textBaseline = 'middle';
    x.fillStyle = '#ffc21a'; x.fillText('OG', 56, hh / 2);
    x.fillStyle = '#f2f4fa'; x.fillText('PLAY', 56 + x.measureText('OG').width, hh / 2);
    x.font = `800 ${Math.round(hh * 0.2)}px ${FONT}`; x.fillStyle = '#8f97ad'; x.textAlign = 'right';
    x.fillText('C A R T R I D G E', W - 56, hh / 2); x.textAlign = 'left';
    x.fillStyle = '#ffc21a'; x.fillRect(0, hh, W, 6);
    const as = H * 0.26, room = W - 56 - as - 90;
    drawIcon(x, face, icon, W - 56 - as, hh + 44, as, 26);
    x.textBaseline = 'alphabetic'; x.fillStyle = '#141a2c';
    fit(x, face.name, 900, H * 0.085, FONT, room); x.fillText(face.name, 56, hh + 44 + H * 0.08);
    x.fillStyle = '#5d6680'; fit(x, face.package, 800, H * 0.04, FONT, room); x.fillText(face.package, 58, hh + 44 + H * 0.135);
    x.fillStyle = '#2a3142';
    const rows = [face.id, `${face.captions.version} ${face.version}`, face.imported ? `${face.captions.imported} ${face.imported}` : ''].filter(Boolean);
    rows.forEach((row, i) => { fit(x, row, 500, H * 0.036, i ? FONT : MONO, room); x.fillText(row, 58, hh + 44 + H * (0.215 + i * 0.06)); });
    x.fillStyle = '#141a2c';
    const code = barcode(x, face.id || face.name, 56, H * 0.74, W * 0.5, H * 0.13);
    x.font = `500 ${Math.round(H * 0.032)}px ${MONO}`; x.fillStyle = '#5d6680';
    x.fillText(`OGP-${code}`, 56, H * 0.93);
    x.textAlign = 'right'; x.font = `800 ${Math.round(H * 0.034)}px ${FONT}`;
    x.fillText(face.captions.tagline, W - 56, H * 0.86);
    x.fillText('OGPlay Compatibility Layer', W - 56, H * 0.93);
    // security seal spot: the holo sticker sits here; on a damaged entry it is gone and leaves VOID behind
    const hx = holoSpot.u * W, hy = holoSpot.v * H, hr = holoSpot.r * W;
    if (dim) {
      x.save(); x.beginPath(); x.arc(hx, hy, hr, 0, Math.PI * 2); x.clip();
      x.fillStyle = 'rgba(160,166,180,.25)'; x.fillRect(hx - hr, hy - hr, hr * 2, hr * 2);
      x.fillStyle = 'rgba(120,128,146,.75)'; x.font = `900 ${Math.round(hr * 0.34)}px ${FONT}`; x.textAlign = 'center';
      for (let i = -2; i <= 2; i++) x.fillText('VOID  VOID', hx + (i % 2) * hr * 0.3, hy + i * hr * 0.42);
      x.restore();
      fade(x, W, H, 0.8); scuff(x, W, H, face.id + ':back');
    }
    paperEdge(x, W, H, 'BackLabel');
    return canvasTexture(cv);
  }
  // spine strip on the top edge
  function drawSpine(face: CartFace, icon: HTMLImageElement | null, dim: boolean) {
    const W = 2048, H = Math.round(W / spineAspect), [cv, x] = canvas(W, H);
    x.fillStyle = '#141a2c'; x.fillRect(0, 0, W, H);
    const tab = H * 1.6;
    const bg = x.createLinearGradient(0, 0, tab, H); bg.addColorStop(0, tint(face, 0.28)); bg.addColorStop(1, tint(face, -0.35));
    x.fillStyle = bg; x.fillRect(0, 0, tab, H);
    const as = H * 0.78;
    drawIcon(x, face, icon, (tab - as) / 2, (H - as) / 2, as, as * 0.18);
    x.fillStyle = '#ffc21a'; x.fillRect(tab, 0, 10, H);
    x.textBaseline = 'middle';
    x.font = `900 ${Math.round(H * 0.34)}px ${FONT}`;
    const brand = W - 40 - x.measureText('OGPLAY').width - 60;
    x.fillStyle = '#f2f4fa'; fit(x, face.name, 900, H * 0.5, FONT, (brand - tab - 60) * 0.62);
    x.fillText(face.name, tab + 60, H * 0.52);
    const nameW = x.measureText(face.name).width;
    x.fillStyle = '#8f97ad'; fit(x, face.package, 800, H * 0.24, FONT, Math.max(0, brand - tab - 96 - nameW));
    x.fillText(face.package, tab + 60 + nameW + 36, H * 0.55);
    x.textAlign = 'right'; x.font = `900 ${Math.round(H * 0.34)}px ${FONT}`;
    x.fillStyle = '#f2f4fa'; x.fillText('PLAY', W - 40, H * 0.53);
    x.fillStyle = '#ffc21a'; x.fillText('OG', W - 40 - x.measureText('PLAY').width, H * 0.53);
    if (dim) fade(x, W, H, 0.8);
    paperEdge(x, W, H, 'Spine');
    return canvasTexture(cv);
  }

  // ---------------------------------------------------------------- fixed prints: PCB, component marks, LED, holo
  // The board print is laid out from the footprints the generator exports (extras.ogp_board), so traces,
  // silkscreen and holes line up with the 3D components that sit on it.
  type Footprint = { ref: string; kind: string; box: [number, number, number, number]; pins: number };
  function drawPCB() {
    const info = JSON.parse((parts.PCB[0].userData.ogp_board as string | undefined) || '{"parts":[],"holes":[]}') as
      { parts: Footprint[]; holes: [number, number][]; fingers?: { n: number; x0: number; dx: number; y1: number } };
    const b = boxOf('PCB'), W = 1024, H = Math.round(W * (b.max.y - b.min.y) / (b.max.x - b.min.x)), [cv, x] = canvas(W, H);
    const k = W / (b.max.x - b.min.x), px = (mx: number) => (mx - b.min.x) * k, py = (my: number) => (b.max.y - my) * k;
    const part = (ref: string) => info.parts.find(p => p.ref === ref);
    // solder mask with a faint copper pour underneath
    const g = x.createLinearGradient(0, 0, 0, H); g.addColorStop(0, '#1d6044'); g.addColorStop(1, '#0f3d2b');
    x.fillStyle = g; x.fillRect(0, 0, W, H);
    x.strokeStyle = 'rgba(120,220,160,.05)'; x.lineWidth = 3;
    for (let d = -H; d < W + H; d += 18) { x.beginPath(); x.moveTo(d, 0); x.lineTo(d + H, H); x.stroke(); }
    // pin positions along one side of an IC footprint, at the end of its pins
    const pins = (p: Footprint, side: 't' | 'b' | 'l' | 'r') => {
      const [x0, y0, x1, y1] = p.box, out = 0.04, n = p.pins;
      return Array.from({ length: n }, (_, i): [number, number] => {
        const t = (i + 0.5) / n;
        if (side === 't') return [x0 + (x1 - x0) * t, y1 + out];
        if (side === 'b') return [x0 + (x1 - x0) * t, y0 - out];
        if (side === 'r') return [x1 + out, y0 + (y1 - y0) * t];
        return [x0 - out, y0 + (y1 - y0) * t];
      });
    };
    const vias: [number, number][] = [];
    const trace = (pts: [number, number][]) => {
      x.beginPath(); pts.forEach(([mx, my], i) => i ? x.lineTo(px(mx), py(my)) : x.moveTo(px(mx), py(my))); x.stroke();
    };
    x.strokeStyle = '#2f8a5e'; x.lineWidth = 6; x.lineJoin = x.lineCap = 'round';
    const u1 = part('U1'), u2 = part('U2'), u3 = part('U3'), f = info.fingers;
    if (u1 && f) {                                              // edge connector -> ROM
      const pb = pins(u1, 'b');
      for (let i = 0; i < f.n; i++) {
        const fx = f.x0 + i * f.dx, [tx, ty] = pb[Math.min(i + 1, pb.length - 1)], bend = f.y1 + 0.12 + (i % 4) * 0.035;
        trace([[fx, f.y1], [fx, bend], [tx, bend + 0.16], [tx, ty]]); vias.push([fx, bend]);
      }
    }
    if (u1 && u2) {                                             // ROM -> controller
      const a = pins(u1, 't'), c = pins(u2, 'b');
      c.forEach(([cx, cy], i) => { const [ax, ay] = a[i + 3]; trace([[ax, ay], [ax, ay + 0.08 + (i % 3) * 0.03], [cx, cy - 0.1], [cx, cy]]); });
    }
    if (u2 && u3) {                                             // controller -> save SRAM
      const r = pins(u2, 'r'), c = pins(u3, 'b');
      c.forEach(([cx, cy], i) => { const [ax, ay] = r[Math.min(i + 1, r.length - 1)]; trace([[ax, ay], [ax + 0.06 + i * 0.012, ay], [cx, cy - 0.12 - i * 0.01], [cx, cy]]); });
    }
    if (u2) {                                                   // controller -> battery, LED and the top row of parts
      const l = pins(u2, 'l'), t = pins(u2, 't');
      l.slice(2, 8).forEach(([ax, ay], i) => { const ex = -0.5 - i * 0.03; trace([[ax, ay], [ex, ay], [ex, 2.02]]); vias.push([ex, 2.02]); });
      t.forEach(([ax, ay], i) => { if (i % 2) { const ty = 2.0 + (i % 3) * 0.05; trace([[ax, ay], [ax, ty]]); vias.push([ax, ty]); } });
    }
    x.lineWidth = 10; x.strokeStyle = '#2f8a5e';                // power rails along the edges
    trace([[b.min.x + 0.06, 0.45], [b.min.x + 0.06, b.max.y - 0.12], [b.max.x - 0.06, b.max.y - 0.12], [b.max.x - 0.06, 0.45]]);
    for (const [vx, vy] of vias) {
      x.fillStyle = '#c9a85a'; x.beginPath(); x.arc(px(vx), py(vy), 7, 0, Math.PI * 2); x.fill();
      x.fillStyle = '#0e2a1f'; x.beginPath(); x.arc(px(vx), py(vy), 3, 0, Math.PI * 2); x.fill();
    }
    for (const [hx, hy] of info.holes) {                        // screw holes: plated ring
      x.fillStyle = '#c9a85a'; x.beginPath(); x.arc(px(hx), py(hy), 0.082 * k, 0, Math.PI * 2); x.fill();
      x.fillStyle = '#07090c'; x.beginPath(); x.arc(px(hx), py(hy), 0.055 * k, 0, Math.PI * 2); x.fill();
    }
    // silkscreen: footprint outlines, reference labels, pin-1 marks, polarity
    x.strokeStyle = x.fillStyle = 'rgba(240,240,232,.85)'; x.lineWidth = 3;
    x.font = `800 22px ${FONT}`; x.textBaseline = 'bottom';
    for (const p of info.parts) {
      const [x0, y0, x1, y1] = p.box, pad = p.kind === 'ic' ? 0.06 : 0.02;
      if (p.kind === 'cell' || p.kind === 'cap') {
        const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, r = (x1 - x0) / 2 + pad;
        x.beginPath(); x.arc(px(cx), py(cy), r * k, 0, Math.PI * 2); x.stroke();
        x.fillText('+', px(cx + r * 0.7), py(cy + r * 0.7));
      } else x.strokeRect(px(x0 - pad), py(y1 + pad), (x1 - x0 + 2 * pad) * k, (y1 - y0 + 2 * pad) * k);
      if (p.kind === 'ic') { x.beginPath(); x.arc(px(x0 - pad) + 12, py(y1 + pad) + 12, 5, 0, Math.PI * 2); x.fill(); }
      if (p.kind !== 'smd') x.fillText(p.ref, px(x0 - pad), py(y1 + pad) - 4);
    }
    // board title and date between the top screw rings, clear of their plating
    const ring = info.holes.length ? Math.max(...info.holes.map(h => Math.abs(h[0]))) - 0.082 - 0.05 : b.max.x - 0.12;
    x.font = `800 24px ${FONT}`; x.textBaseline = 'middle';
    x.fillText('OGPLAY  OGP-01  REV.B', px(-ring) + 30, py(b.max.y - 0.2));
    x.textAlign = 'right'; x.fillText('2026', px(ring), py(b.max.y - 0.2));
    return canvasTexture(cv);
  }
  // Component markings: one atlas laid out by the generator (extras.ogp_marks). Part numbers are fictional.
  function drawMarks() {
    const info = JSON.parse((parts.Marking?.[0]?.userData.ogp_marks as string | undefined) || '{"height":1,"cells":[]}') as
      { height: number; cells: { ref: string; top: number; h: number }[] };
    const W = 1024, [cv, x] = canvas(W, Math.ceil(W * info.height));
    const etch = 'rgba(196,202,214,.82)', ink = 'rgba(34,38,46,.85)';
    const text = (str: string, cx: number, cy: number, size: number, weight = 700, color = etch, font = MONO, spacing = 0) => {
      x.font = `${weight} ${size}px ${font}`; x.fillStyle = color; x.textAlign = 'center'; x.textBaseline = 'middle';
      x.letterSpacing = `${spacing}px`; x.fillText(str, cx, cy); x.letterSpacing = '0px';
    };
    const dimple = (cx: number, cy: number, r: number) => {     // moulded pin-1 dimple
      const g = x.createRadialGradient(cx - r * 0.3, cy - r * 0.3, 0, cx, cy, r);
      g.addColorStop(0, 'rgba(0,0,0,.55)'); g.addColorStop(1, 'rgba(255,255,255,.08)');
      x.fillStyle = g; x.beginPath(); x.arc(cx, cy, r, 0, Math.PI * 2); x.fill();
    };
    const mark = (cx: number, cy: number, s: number, color = etch) => {   // small OG mark in a rounded square
      x.strokeStyle = color; x.lineWidth = s * 0.08; x.beginPath(); x.roundRect(cx - s / 2, cy - s / 2, s, s, s * 0.22); x.stroke();
      text('OG', cx, cy + s * 0.04, s * 0.5, 900, color, FONT);
    };
    const draw: Record<string, (w: number, h: number) => void> = {
      U1: (w, h) => { mark(h * 0.36, h * 0.5, h * 0.42); dimple(w - h * 0.16, h * 0.78, h * 0.07);
        text('OG27C160-12PC', w * 0.56, h * 0.27, h * 0.2, 700, etch, MONO, 2);
        text('OGPLAY MASK ROM · 16M', w * 0.56, h * 0.52, h * 0.13, 600);
        text('1126  A3F9  KR', w * 0.56, h * 0.74, h * 0.13, 500); },
      U2: (w, h) => { dimple(w * 0.13, h * 0.13, w * 0.045); mark(w * 0.5, h * 0.3, w * 0.26);
        text('OGP-MC1', w * 0.5, h * 0.58, w * 0.12, 700, etch, MONO, 2);
        text('CART MAPPER', w * 0.5, h * 0.72, w * 0.075, 600);
        text('1131 · TQFP44', w * 0.5, h * 0.84, w * 0.07, 500); },
      U3: (w, h) => { dimple(h * 0.18, h * 0.78, h * 0.07);
        text('OG62256LP-70', w * 0.5, h * 0.3, h * 0.2, 700, etch, MONO, 2);
        text('32K × 8  SRAM', w * 0.5, h * 0.54, h * 0.14, 600);
        text('1131  B7', w * 0.5, h * 0.76, h * 0.13, 500); },
      Y1: (w, h) => { text('16.000', w * 0.5, h * 0.38, h * 0.34, 700, ink, MONO, 4); text('OGX 11', w * 0.5, h * 0.74, h * 0.2, 600, ink); },
      BT1: (w, h) => { text('CR2032', w * 0.5, h * 0.42, h * 0.46, 800, ink, FONT, 3); text('+', w * 0.93, h * 0.3, h * 0.34, 800, ink, FONT); },
      BT1b: (w, h) => text('3V  LITHIUM', w * 0.5, h * 0.5, h * 0.36, 700, ink, FONT, 2),
      vent: (w, h) => { x.strokeStyle = 'rgba(60,64,72,.75)'; x.lineWidth = w * 0.07; x.lineCap = 'round';
        x.beginPath(); x.moveTo(w * 0.5, h * 0.12); x.lineTo(w * 0.5, h * 0.88); x.moveTo(w * 0.12, h * 0.5); x.lineTo(w * 0.88, h * 0.5);
        x.moveTo(w * 0.5 - w * 0.33, h * 0.5 - h * 0.33); x.lineTo(w * 0.5 + w * 0.33, h * 0.5 + h * 0.33); x.stroke(); },
    };
    for (const c of info.cells) {
      x.save(); x.translate(0, c.top * W);
      (draw[c.ref] ?? (/^C\d/.test(c.ref) ? draw.vent : () => {}))(W, c.h * W);
      x.restore();
    }
    const texture = canvasTexture(cv);
    texture.anisotropy = 16;
    return texture;
  }
  for (const m of parts.PCB) m.material = new THREE.MeshStandardMaterial({ map: drawPCB(), roughness: 0.5 });
  for (const m of parts.Gold) Object.assign(m.material, { metalness: 1, roughness: 0.22 });
  for (const m of parts.Screw) Object.assign(m.material, { color: new THREE.Color('#d4d9e2'), metalness: 0.9, roughness: 0.32 });
  for (const m of parts.Metal) Object.assign(m.material, { color: new THREE.Color('#dde2ea'), metalness: 0.85, roughness: 0.34 });
  for (const m of parts.Chip) Object.assign(m.material, { color: new THREE.Color('#23262e'), roughness: 0.55 });
  const markMaterial = new THREE.MeshStandardMaterial({ map: drawMarks(), transparent: true, depthWrite: false, roughness: 0.6,
    polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
  for (const m of parts.Marking ?? []) m.material = markMaterial;
  // metal trim (seam band, front wordmark inlay, status light bezel): satin brushed aluminium, so the softboxes
  // spread into a sheen instead of mirroring the dark room back
  const trimMaterial = new THREE.MeshPhysicalMaterial({ color: '#e4e8ee', metalness: 1, roughness: 0.3, clearcoat: 0.5, clearcoatRoughness: 0.08 });
  for (const m of parts.Trim ?? []) m.material = trimMaterial;

  // light pipe: bright core fading to the pipe walls
  const ledTex = (() => {
    const [cv, x] = canvas(256, 96);
    const g = x.createRadialGradient(128, 48, 4, 128, 48, 130);
    g.addColorStop(0, '#ffffff'); g.addColorStop(0.35, '#d0d0d0'); g.addColorStop(1, '#505050');
    x.fillStyle = g; x.fillRect(0, 0, 256, 96);
    x.fillStyle = 'rgba(255,255,255,.8)'; x.beginPath(); x.roundRect(40, 22, 176, 10, 5); x.fill();
    return canvasTexture(cv, false);
  })();

  // Holographic foil over the front icon sticker: a rainbow that follows the reflection direction plus fine glitter
  // that winks on and off with the angle.
  const sparkleTex = (() => {
    const [cv, x] = canvas(256, 256), r = rng('ogplay-foil');
    x.fillStyle = '#000'; x.fillRect(0, 0, 256, 256);
    for (let i = 0; i < 1400; i++) { const v = 90 + r() * 165; x.fillStyle = `rgb(${v},${v},${v})`; x.fillRect(Math.floor(r() * 256), Math.floor(r() * 256), 1 + Number(r() < 0.2), 1 + Number(r() < 0.2)); }
    const t = new THREE.CanvasTexture(cv);
    t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.NoColorSpace;
    return t;
  })();
  function foilMaterial(opts: THREE.MeshPhysicalMaterialParameters, mask: THREE.Texture, strength: number) {
    const m = new THREE.MeshPhysicalMaterial(opts);
    m.onBeforeCompile = sh => {
      Object.assign(sh.uniforms, { uFoilOn: { value: strength }, uFoilMask: { value: mask }, uSparkle: { value: sparkleTex } });
      sh.fragmentShader = sh.fragmentShader
        .replace('#include <common>', '#include <common>\nuniform float uFoilOn;\nuniform sampler2D uFoilMask, uSparkle;')
        .replace('#include <opaque_fragment>', `{
  vec3 V = normalize(vViewPosition), R = reflect(-V, normal);
  float phase = R.x * 1.9 + R.y * 1.3 + vMapUv.x * 1.5 + vMapUv.y * 2.1;
  vec3 rainbow = .5 + .5 * cos(6.28318 * (phase + vec3(0., .33, .67)));
  // printed over the foil: dark ink mostly blocks it, light ink lets it through
  float ink = smoothstep(.02, .45, dot(diffuseColor.rgb, vec3(.2126, .7152, .0722)));
  float mask = mix(.18, 1., texture2D(uFoilMask, vMapUv).r) * mix(.3, 1., ink);
  float grazing = .45 + .55 * pow(1. - clamp(dot(normal, V), 0., 1.), 1.2);
  float spk = texture2D(uSparkle, vMapUv * 3.).r;
  float glint = spk * smoothstep(.86, 1., fract(spk * 9. + phase * 2.5));
  outgoingLight += uFoilOn * mask * (rainbow * rainbow * .2 * grazing + rainbow * 1.3 * glint);
}
#include <opaque_fragment>`);
    };
    m.customProgramCacheKey = () => 'ogpFoil';
    return m;
  }

  // holographic security seal: a fine guilloche pattern drives the iridescent film thickness
  const holoMaterial = (() => {
    const [cv, x] = canvas(512, 512);
    x.fillStyle = '#808080'; x.fillRect(0, 0, 512, 512);
    x.translate(256, 256);
    for (let i = 0; i < 48; i++) { x.rotate(Math.PI / 24); x.fillStyle = i % 2 ? '#d0d0d0' : '#404040'; x.beginPath(); x.moveTo(0, 0); x.lineTo(260, -18); x.lineTo(260, 18); x.fill(); }
    for (let r = 40; r < 256; r += 22) { x.strokeStyle = r % 44 ? '#ffffff' : '#202020'; x.lineWidth = 6; x.beginPath(); x.arc(0, 0, r, 0, Math.PI * 2); x.stroke(); }
    x.fillStyle = '#202020'; x.beginPath(); x.ellipse(0, 4, 168, 92, 0, 0, Math.PI * 2); x.fill();   // plain field so the letters stand out
    x.fillStyle = '#ffffff'; x.font = `900 156px ${FONT}`; x.textAlign = 'center'; x.textBaseline = 'middle'; x.fillText('OGP', 0, 8);
    // printed base: a pastel rainbow sweep under the same pattern, so the foil reads holographic even face-on
    const [bc, bx] = canvas(512, 512), sweep = bx.createConicGradient(0, 256, 256);
    ['#ff9ad5', '#ffe48a', '#9dffb0', '#8fe3ff', '#b49bff', '#ff9ad5'].forEach((c, i, a) => sweep.addColorStop(i / (a.length - 1), c));
    bx.fillStyle = sweep; bx.fillRect(0, 0, 512, 512);
    bx.globalAlpha = 0.35; bx.drawImage(cv, 0, 0);
    bx.globalAlpha = 0.85; bx.fillStyle = '#ffffff'; bx.font = `900 156px ${FONT}`; bx.textAlign = 'center'; bx.textBaseline = 'middle';
    bx.fillText('OGP', 256, 264);
    return new THREE.MeshPhysicalMaterial({ map: canvasTexture(bc), metalness: 0.4, roughness: 0.22,
      iridescence: 1, iridescenceIOR: 1.7, iridescenceThicknessRange: [250, 1100], iridescenceThicknessMap: canvasTexture(cv, false),
      clearcoat: 1, clearcoatRoughness: 0.05 });
  })();
  for (const m of parts.Holo) m.material = holoMaterial;

  // ---------------------------------------------------------------- shell
  // baked AO: three only applies aoMap to indirect light, so the shell shader also darkens the diffuse colour with
  // it, softened, so grooves read under the key lights too
  const aoTex = new THREE.Texture(aoImage);
  aoTex.flipY = false; aoTex.colorSpace = THREE.NoColorSpace; aoTex.needsUpdate = true;
  // Moulded shell: polished gloss, shallow recess floors, a darker satin rear half and matte side grips. Positions
  // come from the mesh's own space: x across, y up from the plug, +z towards the front label.
  const shellInfo = parts.Shell[0].userData;
  const grainTex = (() => {
    const N = 256, r = rng('ogplay-grain'), a = new Float32Array(N * N).map(() => r());
    for (let pass = 0; pass < 2; pass++) {                  // soften with a wrapping box blur so the tile repeats seamlessly
      const b = new Float32Array(N * N);
      for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
        let s = 0;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) s += a[((y + dy + N) % N) * N + (x + dx + N) % N];
        b[y * N + x] = s / 9;
      }
      a.set(b);
    }
    let lo = Infinity, hi = -Infinity;
    for (const v of a) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    const px = new Uint8Array(N * N * 4);
    a.forEach((v, i) => { px.fill(Math.round((v - lo) / (hi - lo) * 255), i * 4, i * 4 + 4); });
    const t = new THREE.DataTexture(px, N, N);
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.magFilter = THREE.LinearFilter; t.minFilter = THREE.LinearMipmapLinearFilter; t.generateMipmaps = true;
    t.needsUpdate = true;
    return t;
  })();
  const shellUniforms = {
    uGrain: { value: grainTex },
    uGrainScale: { value: 3 },                            // texture repeats per model unit
    uGrainAmp: { value: 0.002 },                          // grain height in model units
    uSeamZ: { value: (shellInfo.ogp_seam_z as number | undefined) ?? 0 },
    uFaceZ: { value: (shellInfo.ogp_face_z as number | undefined) ?? 0.23 },
    uPlugY: { value: (shellInfo.ogp_plug_y as number | undefined) ?? 0.3 },
    uRear: { value: 0.9 },                                // rear shell albedo relative to the front
    uRearRough: { value: 0.36 },                          // rear shell: at least this rough...
    uRearCoat: { value: 0.3 },                            // ...and with this share of the front's clear coat
    uGrip: { value: new THREE.Vector4(...((shellInfo.ogp_grip as number[] | undefined) ?? [9, 9, 0, 9])) },   // side grips: y0, y1, half depth, |x| inside
    uGloss: { value: 0.14 },                              // roughness of the recess floors
  };
  function shellMaterial(color: THREE.Color, wear: number) {
    // polished gloss surface; a damaged entry turns chalky and gets grain back
    const m = new THREE.MeshPhysicalMaterial({ color, aoMap: aoTex, aoMapIntensity: 0.6, roughness: 0.16, clearcoat: 1, clearcoatRoughness: 0.04 });
    const uniforms = { uWear: { value: wear }, uGrainOn: { value: wear } };
    m.onBeforeCompile = sh => {
      Object.assign(sh.uniforms, shellUniforms, uniforms);
      sh.vertexShader = sh.vertexShader
        .replace('#include <common>', '#include <common>\nvarying vec3 vObj;\nvarying vec3 vObjN;')
        .replace('#include <begin_vertex>', '#include <begin_vertex>\nvObj = position;\nvObjN = normal;');
      sh.fragmentShader = sh.fragmentShader
        .replace('#include <common>', `#include <common>
varying vec3 vObj;
varying vec3 vObjN;
uniform sampler2D uGrain;
uniform float uGrainScale, uGrainAmp, uSeamZ, uFaceZ, uPlugY, uRear, uGloss, uWear, uGrainOn, uRearRough, uRearCoat;
uniform vec4 uGrip;
// bump from a height gradient, using unnormalised screen derivatives so the grain keeps its size in model units
vec3 ogpPerturb(vec3 p, vec3 n, vec2 dh, float faceDir) {
  vec3 sx = dFdx(p), sy = dFdy(p);
  vec3 r1 = cross(sy, n), r2 = cross(n, sx);
  float det = dot(sx, r1) * faceDir;
  return normalize(abs(det) * n - sign(det) * (dh.x * r1 + dh.y * r2));
}`)
        .replace('#include <color_fragment>', `#include <color_fragment>
#ifdef USE_AOMAP
  diffuseColor.rgb *= mix(1., texture2D(aoMap, vAoMapUv).r, .8);
#endif
  vec3 objN = normalize(vObjN);
  float recess = step(abs(vObj.z), uFaceZ - .005) * smoothstep(.8, .95, abs(objN.z)) * step(uPlugY + .02, vObj.y);
  float rear = 1. - smoothstep(uSeamZ - .004, uSeamZ + .004, vObj.z);
  float grip = step(uGrip.w, abs(vObj.x)) * step(uGrip.x, vObj.y) * step(vObj.y, uGrip.y) * step(abs(vObj.z), uGrip.z);
  diffuseColor.rgb *= mix(1., uRear, rear);
  // scuffs on a damaged entry: low-frequency patches, chalky and rough
  float scuffs = uWear * smoothstep(.6, .76, texture2D(uGrain, vObj.xy * .22 + vObj.z * .3).r);
  diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * .8 + .06, scuffs);`)
        .replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\nroughnessFactor = mix(roughnessFactor, uGloss, recess);\nroughnessFactor = mix(roughnessFactor, max(roughnessFactor, uRearRough), rear);\nroughnessFactor = mix(roughnessFactor, max(roughnessFactor, .72), grip);\nroughnessFactor = mix(roughnessFactor, .9, scuffs);')
        .replace('#include <lights_physical_fragment>', '#include <lights_physical_fragment>\n#ifdef USE_CLEARCOAT\n  material.clearcoat *= mix(1., uRearCoat, rear) * (1. - .85 * grip);\n#endif')
        .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
  {
    vec3 w = pow(abs(objN), vec3(4.)); w /= w.x + w.y + w.z;
    vec3 q = vObj * uGrainScale;
    float h = texture2D(uGrain, q.yz).r * w.x + texture2D(uGrain, q.xz).r * w.y + texture2D(uGrain, q.xy).r * w.z;
    vec2 dh = vec2(dFdx(h), dFdy(h)) * uGrainAmp * max(uGrainOn, grip) * (1. - recess);
    normal = ogpPerturb(-vViewPosition, normal, dh, faceDirection);
  }`);
    };
    m.customProgramCacheKey = () => 'ogpShell';
    return m;
  }
  // icon colour as printable plastic: keep the hue, cap the saturation and keep the lightness mid-range
  function shellColor(face: CartFace) {
    const c = new THREE.Color().setRGB(...face.color.map(v => v / 255) as [number, number, number], THREE.SRGBColorSpace), hsl = { h: 0, s: 0, l: 0 };
    c.getHSL(hsl);
    c.setHSL(hsl.h, Math.min(hsl.s, 0.82), THREE.MathUtils.clamp(hsl.l, 0.34, 0.7));
    return c;
  }

  // ---------------------------------------------------------------- looks: one set of materials per entry state
  const dressed = ['Shell', 'Art', 'Title', 'BackLabel', 'Spine', 'LED'] as const;
  type Look = Record<typeof dressed[number], THREE.MeshStandardMaterial> & { tone: string; holo: boolean; gold: boolean };
  async function makeLook(face: CartFace): Promise<Look> {
    const dim = face.status === 'damaged', c = shellColor(face);
    if (dim) { const hsl = { h: 0, s: 0, l: 0 }; c.getHSL(hsl); c.setHSL(hsl.h, hsl.s * 0.2, hsl.l * 0.7); }
    const icon = face.icon ? await loadImage(face.icon).catch(() => null) : null;
    const front = drawArt(face, icon, dim), led = new THREE.Color(LED_COLOR[face.tone] ?? LED_COLOR.warning);
    return {
      tone: face.tone,
      holo: !dim,                                       // the seal is gone from a damaged entry (VOID is printed instead)
      gold: face.status !== 'missing_external',         // no data pack: the connector is bare
      Shell: shellMaterial(c, dim ? 1 : 0),
      Art: foilMaterial({ map: front, roughness: dim ? 0.6 : 0.34, clearcoat: dim ? 0.3 : 1, clearcoatRoughness: dim ? 0.4 : 0.08 },
        front.userData.foilMask as THREE.Texture, dim ? 0 : 1),   // a damaged entry's film has worn off
      Title: new THREE.MeshPhysicalMaterial({ map: drawTitle(face, dim), roughness: dim ? 0.6 : 0.4, clearcoat: dim ? 0.3 : 0.8, clearcoatRoughness: dim ? 0.4 : 0.12 }),
      BackLabel: new THREE.MeshStandardMaterial({ map: drawBack(face, icon, dim), roughness: 0.62 }),
      Spine: new THREE.MeshPhysicalMaterial({ map: drawSpine(face, icon, dim), roughness: 0.4, clearcoat: 0.8, clearcoatRoughness: 0.12 }),
      LED: new THREE.MeshPhysicalMaterial({ color: led.clone().multiplyScalar(0.5), emissive: led, emissiveMap: ledTex, emissiveIntensity: 2.2,
        roughness: 0.08, clearcoat: 1, clearcoatRoughness: 0.05 }),
    };
  }
  function dispose(look: Look) {
    for (const name of dressed) {
      const m = look[name];
      for (const texture of [m.map, m.map?.userData.foilMask as THREE.Texture | undefined]) texture?.dispose();
      m.dispose();
    }
  }
  // Prints are ~10 MB of textures per entry, so only the last few looks stay alive (the shown one, its rail
  // neighbours and recent ones); the rail keeps PNG stills.
  const looks = new Map<string, Promise<Look>>(), keep = 6;
  function lookFor(face: CartFace, pinned: () => string | null) {
    let look = looks.get(face.key);
    if (look) looks.delete(face.key);
    else look = makeLook(face);
    looks.set(face.key, look);
    for (const [key, old] of looks) {
      if (looks.size <= keep) break;
      if (key === pinned()) continue;
      looks.delete(key); void old.then(dispose);
    }
    return look;
  }
  const texturesOf = (look: Look) => dressed.flatMap(name => {
    const m = look[name];
    return [m.map, m.emissiveMap, m.map?.userData.foilMask as THREE.Texture | undefined];
  }).filter((texture): texture is THREE.Texture => !!texture);
  function wear(look: Look) {
    for (const name of dressed) for (const m of parts[name]) m.material = look[name];
    for (const m of parts.Holo) m.visible = look.holo;
    for (const m of parts.Gold) m.visible = look.gold;
  }

  // ---------------------------------------------------------------- renderer, lights
  // Reflection environment: a dim room with a large overhead-front softbox and strip lights, so the flat faces carry
  // one long soft highlight and the edges catch thin rims.
  function studioEnv() {
    const env = new THREE.Scene();
    env.add(new THREE.Mesh(new THREE.BoxGeometry(24, 16, 24), new THREE.MeshBasicMaterial({ color: '#2c3346', side: THREE.BackSide })));
    const panel = (w: number, h: number, pos: [number, number, number], k: number, tint = '#ffffff') => {
      const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ color: new THREE.Color(tint).multiplyScalar(k), side: THREE.DoubleSide }));
      m.position.set(...pos); m.lookAt(0, 0, 0); env.add(m);
    };
    panel(7, 4.5, [3.5, 5, 7], 5, '#fff4e6');     // key softbox, above front-right
    panel(12, 1.2, [0, 7.5, -1], 3);              // overhead strip
    panel(1.2, 9, [-8, 1, 3], 2.4, '#cfe0ff');    // cool side strip
    panel(1.2, 9, [7, 1, -6], 2.2, '#ffe2b0');    // warm rim strip
    panel(6, 4, [-3, 4, -7.5], 3.2);              // back softbox, so the rear face and metal parts have something to mirror
    panel(10, 3, [0, -7, 4], 0.6);                // faint floor bounce
    panel(16, 6, [0, 1.5, 11], 1.5, '#eef2ff');   // broad fill cards front and back: metal facing the viewer mirrors them
    panel(16, 6, [0, 1.5, -11], 1.3, '#eef2ff');
    return env;
  }
  function makeRenderer(options: THREE.WebGLRendererParameters = {}) {
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, ...options });
    renderer.toneMapping = THREE.NeutralToneMapping;
    renderer.toneMappingExposure = 1.05;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    const pmrem = new THREE.PMREMGenerator(renderer);
    const env = pmrem.fromScene(studioEnv(), 0.02).texture;
    pmrem.dispose();
    return { renderer, env };
  }
  // stage lighting: warm key, blue + yellow rims, cool hemisphere fill
  function stageLights() {
    const key = new THREE.DirectionalLight(0xfff1dc, 2.5); key.position.set(3, 6, 6);
    const rimBlue = new THREE.DirectionalLight(0x4aa3ff, 4); rimBlue.position.set(-5, 3, -4);
    const rimWarm = new THREE.DirectionalLight(0xffc21a, 2); rimWarm.position.set(5, 1, -3);
    const fill = new THREE.HemisphereLight(0xbcd4ff, 0x1a1020, 0.6);
    return [key, rimBlue, rimWarm, fill];
  }
  return { model, modelSize, lookFor, wear, texturesOf, makeRenderer, stageLights };
}
