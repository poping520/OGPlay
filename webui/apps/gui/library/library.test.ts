import { describe, expect, it } from 'vitest';
import { cycle, externalText, profileText, shellPalette, statusDetail, storageText, vividIconColor, validatePackageSelection, versionLabel, visibleItems } from './library';
import type { LibraryItem } from '../shared/rpc';
import { setLanguage } from '../shared/i18n';

const item = (id: string, values: Partial<LibraryItem> = {}): LibraryItem => ({
  installation_id: id, display_name: id, localized_names: {}, package: 'org.example.game',
  version_code: 1, version_name: '1.0', imported_at: null, sandbox_path: '/sandbox', log_directory: '/log',
  status: 'ready', damage_reason: null, running: false, can_launch: true, icon: '',
  profile: null, external: { status: 'unavailable', value: '', owned: false }, ...values,
});
describe('library presentation', () => {
  it('combines search terms and host eligibility without merging installation instances', () => {
    const items = [item('one', { display_name: '测试游戏' }), item('two', { display_name: '测试游戏', can_launch: false })];
    expect(visibleItems(items, ' 测试 ORG.EXAMPLE ', 'all', 'name')).toHaveLength(2);
    expect(visibleItems(items, '测试', 'launchable', 'name').map(x => x.installation_id)).toEqual(['one']);
    expect(visibleItems(items, 'no match', 'all', 'name')).toEqual([]);
    expect(visibleItems([item('one', { display_name: '糖果铺', localized_names: { en: 'Sweet Shop' } })], 'sweet', 'all', 'name'))
      .toHaveLength(1);
  });
  it('keeps a running entry under attention when damage controls the badge', () => {
    const damaged = item('one', { status: 'damaged', running: true, can_launch: false });
    expect(visibleItems([damaged], '', 'attention', 'name')).toEqual([damaged]);
  });
  it('sorts dates with missing facts last and leaves host order untouched', () => {
    const items = [item('unknown'), item('older', { imported_at: '2025-01-01' }), item('newer', { imported_at: '2026-01-01' })];
    expect(visibleItems(items, '', 'all', 'newest').map(x => x.installation_id)).toEqual(['newer', 'older', 'unknown']);
    expect(visibleItems(items, '', 'all', 'oldest').map(x => x.installation_id)).toEqual(['older', 'newer', 'unknown']);
    expect(items[0].installation_id).toBe('unknown');
  });
  it('preserves zero version codes and damaged metadata', () => {
    expect(versionLabel(item('zero', { version_code: 0 }))).toBe('1.0 (0)');
    expect(versionLabel(item('none', { version_name: '', version_code: 5 }))).toBe('versionCode 5');
    expect(versionLabel(item('broken', { version_code: null }))).toBe('元数据不可用');
  });
});
describe('host conditions', () => {
  it('words codes and paths in the launcher language', () => {
    const owned = item('a', { status: 'missing_external', external: { status: 'missing', value: '/d', owned: true } });
    expect(statusDetail(owned)).toBe('游戏库中的数据包缺失：/d。请重新选择目录。');
    expect(externalText(owned)).toEqual({ value: '缺失', detail: '记录的数据包目录不可用：/d' });
    expect(externalText(item('b', { external: { status: 'ready', value: '/e', owned: false } })).detail).toBe('引用原目录：/e');
    const gone = item('c', { status: 'profile_catalog_unavailable', profile: { status: 'missing', value: 'p.id', owned: false } });
    expect(statusDetail(gone)).toContain('p.id');
    expect(profileText(gone)).toEqual({ value: 'p.id', detail: '记录的 Profile 在所选目录中不存在。' });
    expect(statusDetail(item('d', { status: 'damaged', damage_reason: null }))).toContain('元数据不可用');
    expect(statusDetail(item('e'))).toBe('');
    const storage = { apk_bytes: 5 * 1024 ** 2, external_bytes: null, sandbox_bytes: 0, external_guest_dir: null };
    expect(storageText(undefined, undefined, '尚未生成')).toBe('计算中…');
    expect(storageText('failed', undefined, '尚未生成')).toBe('无法读取');
    expect(storageText(storage, storage.apk_bytes, '文件缺失')).toBe('5.0 MB');
    expect(storageText(storage, storage.sandbox_bytes, '尚未生成')).toBe('0 B');
    expect(storageText(storage, storage.external_bytes, '目录不可用')).toBe('目录不可用');
    setLanguage('en-US');
    try {
      expect(externalText(item('f', { external: { status: 'not_required', value: '', owned: false } })).value).toBe('Not set');
      expect(statusDetail(owned)).toBe('The data pack in the library is missing: /d. Choose the folder again.');
    } finally { setLanguage('zh-CN'); }
  });
});
describe('file intake', () => {
  it('accepts supported suffixes and rejects batches, directories and empty files', () => {
    for (const name of ['a.apk', 'a.XAPK', 'a.apkm', 'a.apks']) expect(validatePackageSelection([{ name, size: 1 }])).toBeNull();
    for (const files of [[], [{ name: 'a.txt', size: 1 }], [{ name: 'folder', size: 1 }], [{ name: 'a.apk', size: 0 }], [{ name: 'a.apk', size: 1 }, { name: 'b.apk', size: 1 }]])
      expect(validatePackageSelection(files)).not.toBeNull();
  });
});
describe('console navigation and cartridge tint', () => {
  it('wraps selection and category cycling in both directions', () => {
    expect(cycle(['a', 'b', 'c'], 'c', 1)).toBe('a');
    expect(cycle(['a', 'b', 'c'], 'a', -1)).toBe('c');
    expect(cycle(['a', 'b'], 'missing', 1)).toBe('b');
    expect(cycle([], 'x', 1)).toBe('x');
  });
  it('takes the dominant saturated hue instead of averaging highlights and opposite accents', () => {
    const blue = [40, 120, 230, 255], red = [230, 50, 50, 255], white = [255, 255, 255, 255], hidden = [0, 255, 0, 0];
    const pixels = new Uint8ClampedArray([...blue, ...blue, ...blue, ...red, ...white, ...white, ...white, ...white, ...hidden, ...hidden]);
    expect(vividIconColor(pixels)).toEqual([40, 120, 230]);
    expect(vividIconColor(new Uint8ClampedArray(4))).toEqual([124, 156, 255]);
  });
  // 8×8 icon: a 2 px frame around a 4×4 centre, the shape of a bordered launcher icon.
  const framed = (frame: number[], centre: number[], dot?: number[]) => new Uint8ClampedArray(Array.from({ length: 64 }, (_, i) => {
    const x = i % 8, y = Math.floor(i / 8), inner = Math.min(x, y, 7 - x, 7 - y) >= 2;
    return dot && x === 4 && y === 4 ? dot : inner ? centre : frame;
  }).flat());
  const yellow = [230, 190, 20, 255], green = [70, 170, 50, 255], grey = [90, 90, 90, 255];
  it('lets the subject decide over a frame or background hue', () => {
    expect(vividIconColor(framed(yellow, green), 8)).toEqual([70, 170, 50]);
  });
  it('keeps the frame colour when the rest has too little colour', () => {
    expect(vividIconColor(framed(yellow, yellow, green), 8)).toEqual([230, 190, 20]);
  });
  it('turns a neutral icon into a cool graphite so warm light does not read it as gold', () => {
    expect(vividIconColor(framed(grey, grey), 8)).toEqual([84, 90, 104]);
  });
  it('derives ordered light, base and dark shell tones from the icon colour', () => {
    const luminance = (value: string) => value.match(/\d+/g)!.map(Number).reduce((sum, part) => sum + part, 0);
    for (const color of [[0, 0, 0], [255, 255, 255], [124, 156, 255]] as [number, number, number][]) {
      const palette = shellPalette(color);
      expect(luminance(palette.light)).toBeGreaterThan(luminance(palette.shell));
      expect(luminance(palette.shell)).toBeGreaterThan(luminance(palette.dark));
    }
  });
});
