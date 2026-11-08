import { splitSources } from './keys';

// Wire shapes of control.get / control.apply (session::WriteControlLayerJson).
export type Anchor = 'top_left' | 'top' | 'top_right' | 'left' | 'center' | 'right' | 'bottom_left' | 'bottom' | 'bottom_right';
export interface Zone { name: string; anchor: Anchor; x: number; y: number; r?: number; w?: number; h?: number }
export interface Binding {
  source: string; action: 'button' | 'stick' | 'key' | 'mode' | 'look' | 'lock' | 'none'; mode?: string; zone?: string;
  press?: 'tap' | 'hold' | 'repeat'; repeat_frames?: number;
  radius?: number; settle_frames?: number; ramp_frames?: number;
  sensitivity?: number; sensitivity_y?: number; invert_y?: boolean; max_offset_x?: number; max_offset_y?: number;
  regrip?: 'single' | 'handoff'; release_pointer?: boolean;
  key?: string | number; target?: string; switch?: 'toggle' | 'hold';
}
export interface Layer { zone: Zone[]; binding: Binding[] }
export interface Surface { width: number; height: number }
export interface Session {
  language: 'zh-CN' | 'en-US';
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

export type ControlKind = 'button' | 'stick' | 'look';

export function defaultBox(kind: ControlKind, surface: Surface, index = 0): Box {
  const unit = Math.min(surface.width, surface.height);
  // The crosshair is a point: a small circle right of center, clear of the left movement area.
  if (kind === 'look') {
    const r = Math.max(kMinimumHalf, Math.round(unit * 0.03));
    return clampBox({ cx: surface.width * 0.62, cy: surface.height * 0.45, hw: r, hh: r, circle: true }, surface);
  }
  // A roomy stick: its travel (0.6 of the radius) reaches far enough to push most games' sticks fully.
  if (kind === 'stick') return clampBox({ cx: unit * 0.32, cy: surface.height - unit * 0.32, hw: unit * 0.22, hh: unit * 0.22, circle: true }, surface);
  const r = unit * 0.08, step = r * 2.6;
  return clampBox({ cx: surface.width - r * 1.6 - (index % 4) * step, cy: surface.height - r * 1.6 - Math.floor(index / 4) * step, hw: r, hh: r, circle: true }, surface);
}

export function addControl(draft: Draft, kind: ControlKind, surface: Surface): { draft: Draft; zone: string } {
  const name = uniqueName(kind === 'look' ? 'aim' : kind, draft.zones);
  const index = draft.zones.length;
  const zone = placeZone(name, kind === 'button' ? { ...defaultBox('button', surface, 0), cx: surface.width / 2, cy: surface.height / 2 - index * 4 } : defaultBox(kind, surface), surface);
  return { draft: { ...draft, zones: [...draft.zones, zone] }, zone: name };
}

// Direction presets in the order a new stick tries them; WASD is what keyboard players expect first.
export const stickPresets = ['keys:w,a,s,d', 'keys:up,left,down,right'];
// The first preset whose keys no binding uses yet, so a new stick steers at once instead of asking for four keys.
export function freeStickPreset(draft: Draft): string | undefined {
  return stickPresets.find(source => !draft.bindings.some(binding => binding.action !== 'none' && collide(binding, { source, action: 'stick' })));
}
export function newStick(zone: string, source: string): Binding {
  return { source, action: 'stick', zone, radius: 0.6, settle_frames: 2, ramp_frames: 4 };
}

// The crosshair: mouse motion drags a finger pressed at this zone's center, at most one per mode.
// Defaults match input::Binding; max offsets of 0 let the finger travel the whole surface.
export const lookSource = 'mouse:motion';
export function newLook(zone: string): Binding {
  return { source: lookSource, action: 'look', zone, sensitivity: 1, sensitivity_y: 1, invert_y: false, settle_frames: 0, max_offset_x: 0, max_offset_y: 0, regrip: 'single' };
}
export function hasLook(draft: Draft): boolean {
  return draft.bindings.some(binding => binding.action === 'look' && (binding.mode ?? 'default') === 'default');
}

// The key that locks and frees the mouse for the crosshair, in every mode: ` like common
// emulators, else the first free fallback.
export const lockCandidates = ['key:grave', 'key:f3', 'key:f4'];
export function lockBinding(draft: Draft): Binding | undefined {
  return draft.bindings.find(binding => binding.action === 'lock');
}
export function setLock(draft: Draft, source: string): Draft {
  return { ...draft, bindings: [...draft.bindings.filter(binding => binding.action !== 'lock'), { source, action: 'lock', mode: '*' }] };
}
// Adds a lock key with a new crosshair unless one exists: the first candidate no binding uses.
export function withLock(draft: Draft): Draft {
  if (lockBinding(draft)) return draft;
  const free = lockCandidates.find(source =>
    !draft.bindings.some(binding => binding.action !== 'none' && collide(binding, { source, action: 'lock', mode: '*' })));
  return free ? setLock(draft, free) : draft;
}

// The Back key binding (Escape by default) and whether it also frees the mouse.
export function backBinding(draft: Draft): Binding | undefined {
  return draft.bindings.find(binding => binding.action === 'key' && binding.source === 'key:escape');
}

// Where the crosshair finger may travel, as input::ControlMapper computes it: the surface less a
// margin of 3 % of its shorter side, cut to the max offsets around the center.
export function lookRange(box: Box, binding: Binding, surface: Surface) {
  const margin = Math.min(surface.width, surface.height) * 0.03;
  const axis = (center: number, extent: number, offset: number) => {
    let low = margin, high = extent - margin;
    if (offset > 0) { low = Math.max(low, center - offset); high = Math.min(high, center + offset); }
    return [Math.min(low, center), Math.max(high, center)];
  };
  const [x0, x1] = axis(box.cx, surface.width, binding.max_offset_x ?? 0);
  const [y0, y1] = axis(box.cy, surface.height, binding.max_offset_y ?? 0);
  return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 };
}

// Mobile shooters put the movement stick on the left; a crosshair near the middle or left of the
// screen risks every regrip press landing in it.
export function crosshairTooLeft(box: Box, surface: Surface): boolean {
  return box.cx < surface.width * 0.55;
}

export function removeControl(draft: Draft, name: string): Draft {
  const next = { ...draft, zones: draft.zones.filter(zone => zone.name !== name), bindings: draft.bindings.filter(binding => binding.zone !== name) };
  // The lock key only serves a crosshair.
  return next.bindings.some(binding => binding.action === 'look') ? next : { ...next, bindings: next.bindings.filter(binding => binding.action !== 'lock') };
}

// Replaces the zone's default-mode binding of the given action, or adds one.
export function setZoneBinding(draft: Draft, zone: string, binding: Binding): Draft {
  const index = draft.bindings.findIndex(item => item.zone === zone && (item.mode ?? 'default') === (binding.mode ?? 'default'));
  const bindings = [...draft.bindings];
  if (index >= 0) bindings[index] = binding; else bindings.push(binding);
  return { ...draft, bindings };
}
