import { describe, expect, it, vi } from 'vitest';
import { captureKey, captureMouse, joinStickSources, keyName, sourceLabel, splitSources } from './keys';
import {
  addControl, applyTemplate, baseBindings, buildLayer, chooseAnchor, clampBox, conflicts, loadDraft, placeZone,
  removeControl, resolveZone, toGuest, type Binding, type Session, type Template,
} from './model';
import { createRpc, takeToken, RpcError, Unauthorized } from './rpc';

const surface = { width: 1024, height: 600 };
const template: Template = {
  id: 'stick_action', summary: 'stick', zones: ['move', 'primary'],
  binding: [{ source: 'keys:w,a,s,d', action: 'stick', zone: 'move', radius: 0.7, settle_frames: 2, ramp_frames: 4 },
            { source: 'key:j', action: 'button', zone: 'primary', press: 'hold' },
            { source: 'key:k', action: 'button', zone: 'primary', press: 'tap', mode: 'menu' }],
};
const escape: Binding = { source: 'key:escape', action: 'key', mode: '*', key: 'back' };
function session(): Session {
  const zones = [{ name: 'move', anchor: 'bottom_left' as const, x: 30, y: -115, r: 85 },
                 { name: 'primary', anchor: 'bottom_right' as const, x: -40, y: -40, r: 50 }];
  return { surface, frozen: true, mode: 'default', config_path: '/tmp/input.toml', persistent: true, templates: [template],
           user: { template: 'stick_action', zone: zones, binding: [] },
           effective: { zone: zones, binding: [escape, ...template.binding] } };
}

describe('key editor geometry', () => {
  it('maps client pixels to guest pixels through the displayed rectangle', () => {
    expect(toGuest(110, 60, { left: 10, top: 10, width: 512, height: 300 }, surface)).toEqual({ x: 200, y: 100 });
  });
  it('resolves zones with the same anchor rule as ControlMapper', () => {
    expect(resolveZone({ name: 'm', anchor: 'bottom_left', x: 30, y: -115, r: 85 }, surface))
      .toEqual({ cx: 115, cy: 400, hw: 85, hh: 85, circle: true });
    expect(resolveZone({ name: 'p', anchor: 'top_right', x: -10, y: 10, w: 60, h: 40 }, surface))
      .toEqual({ cx: 984, cy: 30, hw: 30, hh: 20, circle: false });
    expect(resolveZone({ name: 'c', anchor: 'center', x: 0, y: 0, r: 20 }, surface).cx).toBe(512);
  });
  it('chooses the anchor from the nearest edges and round-trips the box', () => {
    const anchorOf = (cx: number, cy: number) => chooseAnchor({ cx, cy, hw: 10, hh: 10, circle: true }, surface);
    expect(anchorOf(100, 500)).toBe('bottom_left');
    expect(anchorOf(900, 50)).toBe('top_right');
    expect(anchorOf(512, 300)).toBe('center');
    expect(anchorOf(512, 580)).toBe('bottom');
    expect(anchorOf(1000, 300)).toBe('right');
    for (const box of [{ cx: 115, cy: 400, hw: 85, hh: 85, circle: true }, { cx: 600, cy: 300, hw: 41, hh: 17, circle: false },
                       { cx: 950, cy: 520, hw: 60, hh: 60, circle: true }]) {
      const zone = placeZone('z', box, surface);
      expect(resolveZone(zone, surface)).toEqual(box);
    }
    expect(placeZone('z', { cx: 115, cy: 400, hw: 85, hh: 85, circle: true }, surface))
      .toEqual({ name: 'z', anchor: 'bottom_left', x: 30, y: -115, r: 85 });
  });
  it('keeps boxes whole-pixel, at least the minimum size and on the surface', () => {
    expect(clampBox({ cx: -50, cy: 700.4, hw: 30.6, hh: 2, circle: false }, surface)).toEqual({ cx: 31, cy: 594, hw: 31, hh: 6, circle: false });
    expect(clampBox({ cx: 500, cy: 300, hw: 40, hh: 900, circle: true }, surface)).toEqual({ cx: 500, cy: 300, hw: 40, hh: 40, circle: true });
  });
});

describe('key editor key mapping', () => {
  it('maps KeyboardEvent.code to physical key names', () => {
    expect(keyName('KeyW')).toBe('w');
    expect(keyName('Digit0')).toBe('0');
    expect(keyName('F12')).toBe('f12');
    expect(keyName('Space')).toBe('space');
    expect(keyName('Quote')).toBe('apostrophe');
    expect(keyName('ShiftRight')).toBe('right_shift');
    expect(keyName('ArrowUp')).toBe('up');
    expect(keyName('NumpadEnter')).toBeUndefined();
    expect(keyName('F13')).toBeUndefined();
  });
  it('refuses keys reserved for the host', () => {
    const key = (code: string, modifiers: Partial<{ ctrlKey: boolean; metaKey: boolean }> = {}) =>
      captureKey({ code, ctrlKey: false, metaKey: false, ...modifiers });
    expect(key('KeyJ')).toEqual({ source: 'key:j' });
    expect(key('ControlLeft', { ctrlKey: true })).toEqual({ source: 'key:left_ctrl' });
    expect(key('F1')).toHaveProperty('error');
    expect(key('F2')).toHaveProperty('error');
    expect(key('KeyS', { ctrlKey: true })).toHaveProperty('error');
    expect(key('KeyS', { metaKey: true })).toHaveProperty('error');
    expect(key('MetaLeft', { metaKey: true })).toHaveProperty('error');
    expect(key('IntlRo')).toHaveProperty('error');
  });
  it('maps mouse buttons and stick groups', () => {
    expect(captureMouse(2)).toEqual({ source: 'mouse:right' });
    expect(captureMouse(7)).toHaveProperty('error');
    expect(splitSources('keys:up,left,down,right')).toEqual(['key:up', 'key:left', 'key:down', 'key:right']);
    expect(joinStickSources(['key:w', 'key:a', 'key:s', 'key:d'])).toBe('keys:w,a,s,d');
    expect(sourceLabel('key:space')).toBe('Space');
    expect(sourceLabel('key:j')).toBe('J');
    expect(sourceLabel('mouse:right')).toBe('鼠标右键');
  });
});

describe('key editor drafts', () => {
  it('flags sources shared in one mode or with the every-mode set', () => {
    const bindings: Binding[] = [
      { source: 'keys:w,a,s,d', action: 'stick', zone: 'move' },
      { source: 'key:a', action: 'button', zone: 'x' },
      { source: 'key:k', action: 'button', zone: 'y', mode: 'menu' },
      { source: 'key:k', action: 'button', zone: 'z' },
      { source: 'key:escape', action: 'key', mode: '*', key: 'back' },
      { source: 'key:escape', action: 'button', zone: 'w', mode: 'menu' },
    ];
    expect([...conflicts(bindings)].sort()).toEqual([0, 1, 4, 5]);
  });
  it('re-lists the draft and unbinds lower-layer keys the draft dropped', () => {
    const current = session();
    let draft = loadDraft(current);
    draft = { ...draft, bindings: draft.bindings.map(binding => binding.source === 'key:j' ? { ...binding, source: 'key:space' } : binding) };
    draft = removeControl(draft, 'move');
    const layer = buildLayer(draft, baseBindings(current));
    expect(layer.template).toBe('stick_action');
    expect(layer.zone.map(zone => zone.name)).toEqual(['primary']);
    const unbinds = layer.binding.filter(binding => binding.action === 'none');
    expect(unbinds).toEqual([{ source: 'key:w', action: 'none' }, { source: 'key:a', action: 'none' }, { source: 'key:s', action: 'none' },
                             { source: 'key:d', action: 'none' }, { source: 'key:j', action: 'none' }]);
    expect(layer.binding.slice(unbinds.length)).toEqual(draft.bindings);
    expect(conflicts(layer.binding).size).toBe(0);
  });
  it('places new controls and template zones on the surface', () => {
    const current = session();
    const empty = { zones: [], bindings: [escape] };
    const added = addControl(empty, 'button', surface);
    expect(added.zone).toBe('button_1');
    expect(addControl(added.draft, 'button', surface).zone).toBe('button_2');
    const templated = applyTemplate({ ...empty, zones: [current.user.zone[1]] }, template, surface);
    expect(templated.template).toBe('stick_action');
    expect(templated.zones.map(zone => zone.name)).toEqual(['move', 'primary']);
    expect(templated.zones[1]).toEqual(current.user.zone[1]);
    expect(resolveZone(templated.zones[0], surface).cx).toBeLessThan(surface.width / 3);
    expect(templated.bindings[0]).toEqual(escape);
    for (const zone of templated.zones) {
      const box = resolveZone(zone, surface);
      expect(box.cx - box.hw).toBeGreaterThanOrEqual(0);
      expect(box.cy + box.hh).toBeLessThanOrEqual(surface.height);
    }
  });
});

describe('key editor transport', () => {
  it('moves the launch token out of the address bar', () => {
    const stored = new Map<string, string>();
    const storage = { getItem: (key: string) => stored.get(key) ?? null, setItem: (key: string, value: string) => void stored.set(key, value) };
    const replace = vi.fn();
    const location = { hash: '#token=0123456789abcdef0123', pathname: '/controls/', search: '' };
    expect(takeToken(location, storage, replace)).toBe('0123456789abcdef0123');
    expect(replace).toHaveBeenCalledWith('/controls/');
    expect(takeToken({ ...location, hash: '' }, storage, replace)).toBe('0123456789abcdef0123');
    expect(takeToken({ ...location, hash: '#token=short' }, undefined, replace)).toBeUndefined();
  });
  it('sends the bearer token and decodes results and errors', async () => {
    const replies: Array<{ status: number; body?: (id: number) => unknown }> = [
      { status: 200, body: id => ({ jsonrpc: '2.0', id, result: { frozen: true } }) },
      { status: 200, body: id => ({ jsonrpc: '2.0', id, error: { code: -32010, message: 'zone exceeds' } }) },
      { status: 401 },
    ];
    const fetcher = vi.fn(async (_url: string, init: RequestInit) => {
      const reply = replies.shift()!;
      const id = JSON.parse(String(init.body)).id as number;
      return new Response(reply.body ? JSON.stringify(reply.body(id)) : '', { status: reply.status });
    });
    const rpc = createRpc('0123456789abcdef', fetcher as unknown as typeof fetch);
    await expect(rpc('control.freeze')).resolves.toEqual({ frozen: true });
    expect((fetcher.mock.calls[0][1].headers as Record<string, string>).Authorization).toBe('Bearer 0123456789abcdef');
    await expect(rpc('control.apply', { layer: {} })).rejects.toBeInstanceOf(RpcError);
    await expect(rpc('control.get')).rejects.toBeInstanceOf(Unauthorized);
  });
});
