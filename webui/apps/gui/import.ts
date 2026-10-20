import { rpc } from './rpc';
export interface ImportSummary {
  display_name: string; package: string; version_name: string; version_code: number;
  min_sdk: number | null; target_sdk: number | null; abis: string[]; icon: string;
  profile: string | null; existing_instances: number;
}
export interface ImportJob {
  job: string; state: 'uploading' | 'analyzing' | 'ready' | 'importing' | 'imported' | 'cancelled' | 'failed';
  summary?: ImportSummary; installation_id?: string; message?: string;
  progress?: { copied_bytes: number; total_bytes: number };
}
export type ExternalMode = 'copy' | 'link';
const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
export function formatBytes(value: number): string {
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
  return `${unit ? value.toFixed(1) : value} ${units[unit]}`;
}
/** Stage text while a commit runs; byte counts appear once data copying reports a total. */
export function importingStage(job: ImportJob): string {
  const progress = job.progress;
  if (!progress || !progress.total_bytes) return '正在加入游戏库，请勿关闭…';
  const percent = Math.min(100, Math.floor(progress.copied_bytes / progress.total_bytes * 100));
  return `正在复制数据包… ${percent}%（${formatBytes(progress.copied_bytes)} / ${formatBytes(progress.total_bytes)}）`;
}
export type Call = <T>(method: string, params?: Record<string, unknown>) => Promise<T>;
export const pause = () => new Promise<void>(resolve => setTimeout(resolve, 150));
export async function pickPath(kind: 'file' | 'directory', call: Call = rpc): Promise<string | null> {
  const { dialog } = await call<{ dialog: string }>('dialog.pick', { kind });
  for (;;) {
    const result = await call<{ pending: boolean; path: string | null }>('dialog.poll', { dialog });
    if (!result.pending) return result.path;
    await pause();
  }
}
export async function upload(file: File, onJob: (job: string) => void, progress: (value: number) => void, call: Call = rpc): Promise<ImportJob> {
  if (!/\.apk$/i.test(file.name)) throw new Error('当前仅支持单体 APK；XAPK、APKM、APKS 分包安装尚未实现。');
  if (!file.size || file.size > 1024 ** 3) throw new Error('APK 必须非空且不超过 1 GiB。');
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
