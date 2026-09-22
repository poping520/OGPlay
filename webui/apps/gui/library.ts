import type { LibraryItem } from './rpc';

export type Filter = 'all' | 'launchable' | 'running' | 'attention';
export type Sort = 'name' | 'newest' | 'oldest';
export const statuses: Record<string, { label: string; tone: string }> = {
  damaged: { label: '条目损坏', tone: 'danger' },
  profile_catalog_unavailable: { label: 'Profile 不可用', tone: 'danger' },
  missing_profile: { label: '无 Profile', tone: 'warning' },
  missing_external: { label: '缺数据包', tone: 'warning' },
  running: { label: '运行中', tone: 'running' },
  ready: { label: '就绪', tone: 'ready' },
};
export function visibleItems(items: LibraryItem[], query: string, filter: Filter, sort: Sort): LibraryItem[] {
  const terms = query.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
  const time = (item: LibraryItem) => {
    const parsed = Date.parse(item.imported_at ?? '');
    return Number.isFinite(parsed) ? parsed : null;
  };
  return items.filter(item => {
    const text = [item.display_name, item.package, item.installation_id, item.version].join(' ').toLocaleLowerCase();
    return terms.every(term => text.includes(term)) &&
      (filter === 'all' || (filter === 'launchable' && item.can_launch) ||
       (filter === 'running' && item.running) ||
       (filter === 'attention' && !['ready', 'running'].includes(item.status)));
  }).sort((left, right) => {
    if (sort !== 'name') {
      const a = time(left), b = time(right);
      if (a === null && b !== null) return 1;
      if (a !== null && b === null) return -1;
      if (a !== null && b !== null && a !== b) return sort === 'newest' ? b - a : a - b;
    }
    return left.display_name.localeCompare(right.display_name, 'zh-CN', { numeric: true }) ||
      left.installation_id.localeCompare(right.installation_id);
  });
}
export function versionLabel(item: LibraryItem): string {
  return item.version_code === null ? item.version :
    item.version_name ? `${item.version_name} (${item.version_code})` : `versionCode ${item.version_code}`;
}
export function validatePackageSelection(files: Pick<File, 'name' | 'size'>[]): string | null {
  if (files.length !== 1) return '请一次选择一个 APK、XAPK、APKM 或 APKS 文件。';
  if (!/\.(apk|xapk|apkm|apks)$/i.test(files[0].name)) return '文件格式不支持，请选择 APK、XAPK、APKM 或 APKS。';
  if (files[0].size === 0) return '文件为空，请选择完整的安装包。';
  return null;
}
// Ignore transparent pixels so icon padding does not darken the glow.
export function averageIconColor(pixels: Uint8ClampedArray): [number, number, number] {
  let red = 0, green = 0, blue = 0, weight = 0;
  for (let i = 0; i + 3 < pixels.length; i += 4) {
    const alpha = pixels[i + 3] / 255;
    red += pixels[i] * alpha; green += pixels[i + 1] * alpha; blue += pixels[i + 2] * alpha; weight += alpha;
  }
  return weight ? [Math.round(red / weight), Math.round(green / weight), Math.round(blue / weight)] : [124, 156, 255];
}
