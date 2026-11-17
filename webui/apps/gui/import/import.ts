import { t } from '../shared/i18n';
import { rpc } from '../shared/rpc';
export interface ImportSummary {
  display_name: string; localized_names: Record<string, string>; package: string; version_name: string; version_code: number;
  min_sdk: number | null; target_sdk: number | null; abis: string[]; icon: string;
  profile: string | null; existing_instances: number;
  /** A game archive's contents; null for a plain APK. */
  archive?: ArchiveSummary | null;
}
/** What the host found in a game archive: its APKs (the analyzed one is `apk`), the data pack it recognized with
 *  the mount it planned (and the /sdcard top level, used once extras are chosen), and the parts of no known use. */
export interface ArchiveSummary {
  apks: { path: string; size: number }[]; apk: string;
  data: (DataDirectory & { guest_dir: string; sdcard: Omit<DataDirectory, 'path'> }) | null;
  extras: { name: string; files: number; bytes: number }[];
}
export interface ImportJob {
  job: string; state: 'uploading' | 'analyzing' | 'ready' | 'importing' | 'imported' | 'cancelled' | 'failed';
  summary?: ImportSummary; installation_id?: string; message?: string;
  progress?: { copied_bytes: number; total_bytes: number };
}
export type ExternalMode = 'copy' | 'link';
const units = ['B', 'KB', 'MB', 'GB', 'TB'];
export function formatBytes(value: number): string {
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
  return `${unit ? value.toFixed(1) : value} ${units[unit]}`;
}
/** Stage text while a commit runs; byte counts appear once data copying (or unpacking an archive) reports a total. */
export function importingStage(job: ImportJob, unpacking = false): string {
  if (job.state === 'failed') return t('导入失败');
  const progress = job.progress;
  if (!progress || !progress.total_bytes) return t('正在加入游戏库，请勿关闭…');
  const percent = Math.min(100, Math.floor(progress.copied_bytes / progress.total_bytes * 100));
  const params = { percent, copied: formatBytes(progress.copied_bytes), total: formatBytes(progress.total_bytes) };
  return unpacking ? t('正在解压数据包… {percent}%（{copied} / {total}）', params) : t('正在复制数据包… {percent}%（{copied} / {total}）', params);
}
/** With extras chosen, an archive's data mounts at /sdcard: the preview then lists that level plus the extras. */
export function archivePreview(archive: ArchiveSummary, extras: string[]): DataDirectory | null {
  const chosen = archive.extras.filter(extra => extras.includes(extra.name));
  if (!chosen.length) return archive.data;
  const base = archive.data?.sdcard ?? { entries: [], entry_count: 0, files: 0, bytes: 0, truncated: false };
  return {
    path: '/sdcard', truncated: false,
    entries: [...base.entries, ...chosen.map(extra => ({ name: extra.name, directory: true }))].slice(0, 8),
    entry_count: base.entry_count + chosen.length,
    files: base.files + chosen.reduce((sum, extra) => sum + extra.files, 0),
    bytes: base.bytes + chosen.reduce((sum, extra) => sum + extra.bytes, 0),
  };
}
export type Call = <T>(method: string, params?: Record<string, unknown>) => Promise<T>;
export const pause = () => new Promise<void>(resolve => setTimeout(resolve, 150));
/** A host pick: `path` goes back to the host, `label` is what to show (a SAF pick's path is only a handle). */
export async function pickSource(kind: 'file' | 'directory', call: Call = rpc): Promise<{ path: string; label: string } | null> {
  const { dialog } = await call<{ dialog: string }>('dialog.pick', { kind });
  for (;;) {
    const result = await call<{ pending: boolean; path: string | null; label?: string }>('dialog.poll', { dialog });
    if (!result.pending) return result.path === null ? null : { path: result.path, label: result.label || result.path };
    await pause();
  }
}
export async function pickPath(kind: 'file' | 'directory', call: Call = rpc): Promise<string | null> {
  return (await pickSource(kind, call))?.path ?? null;
}
export async function upload(file: File, onJob: (job: string) => void, progress: (value: number) => void, call: Call = rpc): Promise<ImportJob> {
  if (!/\.apk$/i.test(file.name)) throw new Error(t('当前仅支持单体 APK；XAPK、APKM、APKS 分包安装尚未实现。'));
  if (!file.size || file.size > 1024 ** 3) throw new Error(t('APK 必须非空且不超过 1 GB。'));
  const result = await call<ImportJob>('library.upload.begin', { name: file.name, size: file.size });
  onJob(result.job);
  for (let offset = 0; offset < file.size; offset += 192 * 1024) {
    const bytes = new Uint8Array(await file.slice(offset, offset + 192 * 1024).arrayBuffer());
    let text = '';
    for (let i = 0; i < bytes.length; i += 8192) text += String.fromCharCode(...bytes.subarray(i, i + 8192));
    await call('library.upload.chunk', { job: result.job, offset, data: btoa(text) });
    progress(Math.min(100, Math.round((offset + bytes.length) / file.size * 100)));
  }
  return call<ImportJob>('library.upload.finish', { job: result.job });
}
export interface DataDirectory {
  path: string; entries: { name: string; directory: boolean }[];
  entry_count: number; files: number; bytes: number; truncated: boolean;
  /** Mount implied by where the directory sits (Android/data/<package>, sdcard/…); null keeps /sdcard. */
  suggested_guest_dir?: string | null;
}
/** Mirrors the host rule: /sdcard or a directory below it, no empty/dot segments or control characters. */
export function guestPathError(value: string): string | null {
  if (value !== '/sdcard' && !value.startsWith('/sdcard/')) return t('挂载位置必须是 /sdcard 或其下的目录。');
  const parts = value.slice(1).split('/');
  if (parts.some(part => !part || part === '.' || part === '..' || part.includes('\\') || /[\u0000-\u001f]/.test(part)))
    return t('路径含空段、. / .. 、反斜杠或控制字符。');
  return null;
}
/** Trailing slashes are a typing habit, not a different directory. */
export const normalizeGuestPath = (value: string) => value.trim().replace(/(?<=.)\/+$/, '');
export function describeData(data: DataDirectory): string {
  const params = { files: data.files, size: formatBytes(data.bytes) };
  return data.truncated ? t('超过 {files} 个文件 · {size}（统计已截断）', params) : t('{files} 个文件 · {size}', params);
}
export interface DataMountCandidate { guest_dir: string; explained: number; examples: string[] }
/** ADR-0146: host suggestion computed from what the last run failed to find. */
export interface DataHint {
  kind: 'none' | 'remount' | 'needs_data';
  current_guest_dir?: string; external_dir?: string | null; owned?: boolean;
  missing?: number; truncated?: boolean; candidates?: DataMountCandidate[];
  search_root?: string; examples?: string[];
}
