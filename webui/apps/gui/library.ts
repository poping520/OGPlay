import type { LibraryItem } from './rpc';

export type Filter = 'all' | 'launchable' | 'running' | 'attention';
export type Sort = 'name' | 'newest' | 'oldest';
export const filters: readonly (readonly [Filter, string])[] = [['all', '全部'], ['launchable', '可启动'], ['running', '运行中'], ['attention', '需检查']];
export const statuses: Record<string, { label: string; tone: string }> = {
  damaged: { label: '条目损坏', tone: 'danger' },
  profile_catalog_unavailable: { label: 'Profile 不可用', tone: 'danger' },
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
// Wraps around so L/R and arrow keys can cycle without dead ends.
export function cycle<T>(values: readonly T[], current: T, delta: number): T {
  const index = values.indexOf(current);
  return values.length ? values[((index < 0 ? 0 : index) + delta + values.length * Math.abs(delta)) % values.length] : current;
}
// Picks the dominant hue (weighted by saturation) so white highlights, outlines and small accents do not muddy the tint.
export function vividIconColor(pixels: Uint8ClampedArray): [number, number, number] {
  const bins = Array.from({ length: 13 }, () => [0, 0, 0, 0]);
  for (let i = 0; i + 3 < pixels.length; i += 4) {
    const [red, green, blue] = [pixels[i], pixels[i + 1], pixels[i + 2]];
    const max = Math.max(red, green, blue), chroma = max - Math.min(red, green, blue);
    const hue = !chroma ? 0 : max === red ? ((green - blue) / chroma + 6) % 6 : max === green ? (blue - red) / chroma + 2 : (red - green) / chroma + 4;
    const bin = bins[chroma < 24 ? 12 : Math.floor(hue * 2) % 12];
    const weight = pixels[i + 3] / 255 * (0.04 + (chroma / 255) ** 2);
    bin[0] += red * weight; bin[1] += green * weight; bin[2] += blue * weight; bin[3] += weight;
  }
  const best = bins.reduce((left, right) => right[3] > left[3] ? right : left);
  return best[3] ? [0, 1, 2].map(channel => Math.round(best[channel] / best[3])) as [number, number, number] : [124, 156, 255];
}
// Cartridge shell tints from the icon colour: very dark or very pale icons are pulled toward a printable plastic tone.
export function shellPalette([red, green, blue]: [number, number, number]): { shell: string; light: string; dark: string } {
  const mix = (value: number, target: number, amount: number) => Math.round(value + (target - value) * amount);
  const luma = 0.299 * red + 0.587 * green + 0.114 * blue;
  const lift = luma < 80 ? (80 - luma) / 80 * 0.45 : luma > 210 ? -(luma - 210) / 45 * 0.25 : 0;
  const base = [red, green, blue].map(value => lift >= 0 ? mix(value, 150, lift) : mix(value, 40, -lift));
  const rgb = (values: number[]) => `rgb(${values.join(', ')})`;
  return { shell: rgb(base), light: rgb(base.map(value => mix(value, 255, 0.32))), dark: rgb(base.map(value => mix(value, 0, 0.42))) };
}
