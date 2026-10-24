import type { LibraryItem } from './rpc';
import { t } from './i18n';

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
  if (files.length !== 1) return t('请一次选择一个 APK、XAPK、APKM 或 APKS 文件。');
  if (!/\.(apk|xapk|apkm|apks)$/i.test(files[0].name)) return t('文件格式不支持，请选择 APK、XAPK、APKM 或 APKS。');
  if (files[0].size === 0) return t('文件为空，请选择完整的安装包。');
  return null;
}
// Wraps around so L/R and arrow keys can cycle without dead ends.
export function cycle<T>(values: readonly T[], current: T, delta: number): T {
  const index = values.indexOf(current);
  return values.length ? values[((index < 0 ? 0 : index) + delta + values.length * Math.abs(delta)) % values.length] : current;
}
// Cartridge colour from the icon's subject. Pixels fall into twelve 30° hue bins plus a neutral one, weighted by chroma
// and by closeness to the centre. When one hue covers most of the outer ring it is the background or frame (a yellow
// border, a sky) and is left out, so the character or object in front decides; it only counts again when the rest
// carries too little colour (a single-colour icon). Neutral wins only then too, and becomes a cool graphite so warm
// studio light does not read it as gold.
const NEUTRAL = 12, RING = 2, RING_SHARE = 0.35, SUBJECT_SHARE = 0.12;
export function vividIconColor(pixels: Uint8ClampedArray, width = Math.round(Math.sqrt(pixels.length / 4))): [number, number, number] {
  const bins = Array.from({ length: 13 }, () => [0, 0, 0, 0]), ring = new Array<number>(13).fill(0);
  const height = Math.ceil(pixels.length / 4 / width);
  let ringTotal = 0;
  for (let i = 0; i + 3 < pixels.length; i += 4) {
    const [red, green, blue] = [pixels[i], pixels[i + 1], pixels[i + 2]], alpha = pixels[i + 3] / 255;
    const max = Math.max(red, green, blue), chroma = max - Math.min(red, green, blue);
    const hue = !chroma ? 0 : max === red ? ((green - blue) / chroma + 6) % 6 : max === green ? (blue - red) / chroma + 2 : (red - green) / chroma + 4;
    const index = chroma < 24 ? NEUTRAL : Math.floor(hue * 2) % 12;
    const x = (i / 4) % width, y = Math.floor(i / 4 / width);
    if (Math.min(x, y, width - 1 - x, height - 1 - y) < RING) { ring[index] += alpha; ringTotal += alpha; }
    const dx = (x + 0.5) / width - 0.5, dy = (y + 0.5) / height - 0.5;
    const weight = alpha * Math.exp(-(dx * dx + dy * dy) / 0.18) * (0.04 + chroma / 255);
    const bin = bins[index];
    bin[0] += red * weight; bin[1] += green * weight; bin[2] += blue * weight; bin[3] += weight;
  }
  const top = ring.indexOf(Math.max(...ring));
  const background = ringTotal && ring[top] / ringTotal > RING_SHARE ? top : -1;
  const colour = (keep: (index: number) => boolean) => bins.slice(0, NEUTRAL).reduce((sum, bin, index) => sum + (keep(index) ? bin[3] : 0), 0);
  const total = colour(() => true);
  const subject = background >= 0 && total > 0 && colour(index => index !== background) >= SUBJECT_SHARE * total;
  const candidates = bins.map((_, index) => index).filter(index => subject ? index !== background && index !== NEUTRAL : true);
  const best = candidates.reduce((left, right) => bins[right][3] > bins[left][3] ? right : left);
  const [red, green, blue, weight] = bins[best];
  if (!weight) return [124, 156, 255];
  const rgb = [red, green, blue].map(value => Math.round(value / weight)) as [number, number, number];
  if (best !== NEUTRAL) return rgb;
  const grey = Math.round((rgb[0] + rgb[1] + rgb[2]) / 3);
  return [Math.max(0, grey - 6), grey, Math.min(255, grey + 14)];
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
