import { splitSources } from './keys';

// Wire shapes of control.get / control.apply (session::WriteControlLayerJson).
export type Anchor = 'top_left' | 'top' | 'top_right' | 'left' | 'center' | 'right' | 'bottom_left' | 'bottom' | 'bottom_right';
export interface Zone { name: string; anchor: Anchor; x: number; y: number; r?: number; w?: number; h?: number }
export interface Binding {
  source: string; action: 'button' | 'stick' | 'key' | 'mode' | 'none'; mode?: string; zone?: string;
  press?: 'tap' | 'hold' | 'repeat'; repeat_frames?: number;
  radius?: number; settle_frames?: number; ramp_frames?: number;
  key?: string | number; target?: string; switch?: 'toggle' | 'hold';
}
export interface Layer { zone: Zone[]; binding: Binding[] }
export interface Surface { width: number; height: number }
export interface Session {
  surface: Surface; frozen: boolean; mode: string; config_path: string | null; persistent: boolean;
  user: Layer; effective: Layer;
}

// Guest-pixel box: center and half extents.
export interface Box { cx: number; cy: number; hw: number; hh: number; circle: boolean }

const fractions: Record<Anchor, [number, number]> = {
  top_left: [0, 0], top: [0.5, 0], top_right: [1, 0], left: [0, 0.5], center: [0.5, 0.5],
  right: [1, 0.5], bottom_left: [0, 1], bottom: [0.5, 1], bottom_right: [1, 1],
};
const anchors: Anchor[][] = [['top_left', 'top', 'top_right'], ['left', 'center', 'right'], ['bottom_left', 'bottom', 'bottom_right']];

// Same rule as input::ControlMapper::SetSurface: the zone's own anchor point sits at the surface
// anchor point plus (x, y).
export function resolveZone(zone: Zone, surface: Surface): Box {
  const circle = (zone.r ?? 0) > 0;
  const width = circle ? zone.r! * 2 : zone.w ?? 0, height = circle ? zone.r! * 2 : zone.h ?? 0;
  const [fx, fy] = fractions[zone.anchor];
  const left = fx * surface.width + zone.x - fx * width, top = fy * surface.height + zone.y - fy * height;
  return { cx: left + width / 2, cy: top + height / 2, hw: width / 2, hh: height / 2, circle };
}

// The nearest edges decide the anchor so the zone keeps its place when the surface size changes.
export function chooseAnchor(box: Box, surface: Surface): Anchor {
  // Share of the free space before the box: the closer edge wins unless the gaps are similar.
  const side = (center: number, half: number, extent: number) => {
    const before = center - half, free = extent - half * 2;
    if (free <= 0) return 1;
    return before / free < 1 / 3 ? 0 : before / free > 2 / 3 ? 2 : 1;
  };
  return anchors[side(box.cy, box.hh, surface.height)][side(box.cx, box.hw, surface.width)];
}

export function placeZone(name: string, box: Box, surface: Surface): Zone {
  const fitted = clampBox(box, surface);
  const anchor = chooseAnchor(fitted, surface);
  const [fx, fy] = fractions[anchor];
  const width = fitted.hw * 2, height = fitted.hh * 2;
  const x = fitted.cx - fitted.hw + fx * width - fx * surface.width;
  const y = fitted.cy - fitted.hh + fy * height - fy * surface.height;
  const zone: Zone = { name, anchor, x, y };
  if (fitted.circle) zone.r = fitted.hw; else { zone.w = width; zone.h = height; }
  return zone;
}

const kMinimumHalf = 6;
// Whole guest pixels keep the saved numbers exact; the box always stays on the surface.
export function clampBox(box: Box, surface: Surface): Box {
  const limit = (value: number, extent: number) => Math.max(kMinimumHalf, Math.min(Math.round(value), Math.floor(extent / 2)));
  let hw = limit(box.hw, surface.width), hh = limit(box.hh, surface.height);
  if (box.circle) hw = hh = Math.min(hw, hh);
  const cx = Math.min(Math.max(Math.round(box.cx), hw), surface.width - hw);
  const cy = Math.min(Math.max(Math.round(box.cy), hh), surface.height - hh);
  return { cx, cy, hw, hh, circle: box.circle };
}

export function toGuest(clientX: number, clientY: number, rect: { left: number; top: number; width: number; height: number }, surface: Surface) {
  return { x: (clientX - rect.left) / rect.width * surface.width, y: (clientY - rect.top) / rect.height * surface.height };
}

// Same rule as session::BuildControlConfig: two bindings collide when they share a source in the
// same mode or either lives in every mode "*".
function collide(left: Binding, right: Binding): boolean {
  const lm = left.mode ?? 'default', rm = right.mode ?? 'default';
  if (lm !== rm && lm !== '*' && rm !== '*') return false;
  const sources = splitSources(left.source);
  return splitSources(right.source).some(source => sources.includes(source));
}
// Indexes of bindings that collide with another binding.
export function conflicts(bindings: Binding[]): Set<number> {
  const result = new Set<number>();
  bindings.forEach((left, i) => bindings.forEach((right, j) => {
    if (i < j && left.action !== 'none' && right.action !== 'none' && collide(left, right)) { result.add(i); result.add(j); }
  }));
  return result;
}

// The editor works on the whole effective layout; Draft is what the user sees.
export interface Draft { zones: Zone[]; bindings: Binding[] }

export function loadDraft(session: Session): Draft {
  return { zones: structuredClone(session.effective.zone),
           bindings: structuredClone(session.effective.binding.filter(binding => binding.action !== 'none')) };
}

// Track loaded bindings so deleting a built-in binding writes an explicit unbind.
export function baseBindings(session: Session): Binding[] {
  return [...session.effective.binding, ...session.user.binding];
}

// The saved layer re-lists the full draft after unbinding every lower-layer source the draft no
// longer covers, so removed built-in keys stay removed.
export function buildLayer(draft: Draft, base: Binding[]): Layer {
  const unbinds: Binding[] = [];
  const seen = new Set<string>();
  for (const binding of base) {
    for (const source of splitSources(binding.source)) {
      const single: Binding = { source, action: 'none', ...(binding.mode && binding.mode !== 'default' ? { mode: binding.mode } : {}) };
      const key = `${single.mode ?? 'default'}|${source}`;
      if (seen.has(key) || draft.bindings.some(item => collide(item, single))) continue;
      seen.add(key); unbinds.push(single);
    }
  }
  return { zone: draft.zones, binding: [...unbinds, ...draft.bindings] };
}

export function uniqueName(prefix: string, zones: Zone[]): string {
  for (let i = 1; ; ++i) if (!zones.some(zone => zone.name === `${prefix}_${i}`)) return `${prefix}_${i}`;
}

export function defaultBox(kind: 'button' | 'stick', surface: Surface, index = 0): Box {
  const unit = Math.min(surface.width, surface.height);
  if (kind === 'stick') return clampBox({ cx: unit * 0.28, cy: surface.height - unit * 0.28, hw: unit * 0.16, hh: unit * 0.16, circle: true }, surface);
  const r = unit * 0.08, step = r * 2.6;
  return clampBox({ cx: surface.width - r * 1.6 - (index % 4) * step, cy: surface.height - r * 1.6 - Math.floor(index / 4) * step, hw: r, hh: r, circle: true }, surface);
}

export function addControl(draft: Draft, kind: 'button' | 'stick', surface: Surface): { draft: Draft; zone: string } {
  const name = uniqueName(kind, draft.zones);
  const index = draft.zones.length;
  const zone = placeZone(name, kind === 'stick' ? defaultBox('stick', surface) : { ...defaultBox('button', surface, 0), cx: surface.width / 2, cy: surface.height / 2 - index * 4 }, surface);
  return { draft: { ...draft, zones: [...draft.zones, zone] }, zone: name };
}

export function removeControl(draft: Draft, name: string): Draft {
  return { ...draft, zones: draft.zones.filter(zone => zone.name !== name), bindings: draft.bindings.filter(binding => binding.zone !== name) };
}

// Replaces the zone's default-mode binding of the given action, or adds one.
export function setZoneBinding(draft: Draft, zone: string, binding: Binding): Draft {
  const index = draft.bindings.findIndex(item => item.zone === zone && (item.mode ?? 'default') === (binding.mode ?? 'default'));
  const bindings = [...draft.bindings];
  if (index >= 0) bindings[index] = binding; else bindings.push(binding);
  return { ...draft, bindings };
}
