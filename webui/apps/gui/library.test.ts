import { describe, expect, it } from 'vitest';
import { averageIconColor, validatePackageSelection, versionLabel, visibleItems } from './library';
import type { LibraryItem } from './rpc';

const item = (id: string, values: Partial<LibraryItem> = {}): LibraryItem => ({
  installation_id: id, display_name: id, package: 'org.example.game', version: '1.0',
  version_code: 1, version_name: '1.0', imported_at: null, sandbox_path: '/sandbox', log_directory: '/log',
  status: 'missing_profile', detail: '', running: false, can_launch: true, icon: '',
  profile: { status: 'missing', value: '', detail: '' }, external: { status: 'unavailable', value: '', detail: '' }, ...values,
});
describe('library presentation', () => {
  it('combines search terms and host eligibility without merging installation instances', () => {
    const items = [item('one', { display_name: '测试游戏' }), item('two', { display_name: '测试游戏', can_launch: false })];
    expect(visibleItems(items, ' 测试 ORG.EXAMPLE ', 'all', 'name')).toHaveLength(2);
    expect(visibleItems(items, '测试', 'launchable', 'name').map(x => x.installation_id)).toEqual(['one']);
    expect(visibleItems(items, 'no match', 'all', 'name')).toEqual([]);
  });
  it('uses running fact even when higher priority damage controls the badge', () => {
    const damaged = item('one', { status: 'damaged', running: true, can_launch: false });
    expect(visibleItems([damaged], '', 'running', 'name')).toEqual([damaged]);
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
    expect(versionLabel(item('broken', { version_code: null, version: '元数据不可用' }))).toBe('元数据不可用');
  });
});
describe('file intake and icon colors', () => {
  it('accepts supported suffixes and rejects batches, directories and empty files', () => {
    for (const name of ['a.apk', 'a.XAPK', 'a.apkm', 'a.apks']) expect(validatePackageSelection([{ name, size: 1 }])).toBeNull();
    for (const files of [[], [{ name: 'a.txt', size: 1 }], [{ name: 'folder', size: 1 }], [{ name: 'a.apk', size: 0 }], [{ name: 'a.apk', size: 1 }, { name: 'b.apk', size: 1 }]])
      expect(validatePackageSelection(files)).not.toBeNull();
  });
  it('weights alpha and uses a deterministic transparent fallback', () => {
    expect(averageIconColor(new Uint8ClampedArray([255, 0, 0, 255, 0, 0, 255, 0]))).toEqual([255, 0, 0]);
    expect(averageIconColor(new Uint8ClampedArray(4))).toEqual([124, 156, 255]);
    expect(averageIconColor(new Uint8ClampedArray([255, 0, 0, 255, 0, 0, 255, 255]))).toEqual([128, 0, 128]);
  });
});
