import { t } from './i18n';
// Host facts only: the Profile value is the id or the catalog error, the data pack value the
// host path; the launcher writes the words.
export interface Condition { status: string; value: string; owned: boolean }
export interface LibraryItem {
  installation_id: string; display_name: string; package: string;
  localized_names: Record<string, string>;
  status: string; damage_reason: string | null; running: boolean; can_launch: boolean; icon: string;
  /** One of OGPlay's bundled demo games (hidden by the show_demo_games setting, removal turns it off). */
  builtin_demo: boolean;
  profile: Condition | null; external: Condition;
  version_name: string | null; version_code: number | null; imported_at: string | null;
  sandbox_path: string; log_directory: string;
}
export interface Library { items: LibraryItem[]; library_root: string }
/** `library.storage`: bytes on disk (null when not there yet) and where the data pack mounts in the guest. */
export interface LibraryStorage { apk_bytes: number | null; external_bytes: number | null; sandbox_bytes: number | null; external_guest_dir: string | null }
declare global {
  interface Window { rpc?: (request: string) => Promise<unknown>; __ogplaySmoke?: number }
}
let sequence = 0;
export function decodeResponse<T>(response: unknown, id: number): T {
  if (!response || typeof response !== 'object') throw new Error(t('宿主返回了无效响应。'));
  const value = response as Record<string, unknown>;
  if (value.jsonrpc !== '2.0' || value.id !== id) throw new Error(t('宿主响应标识不匹配。'));
  if (value.error) {
    const error = value.error as { message?: string; next_step?: string };
    throw new Error(`${error.message ?? t('请求失败')}\n${error.next_step ?? t('检查 GUI 日志后重试。')}`);
  }
  if (!('result' in value)) throw new Error(t('宿主响应缺少结果。'));
  return value.result as T;
}
export async function rpc<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  if (!window.rpc) throw new Error(t('请通过 OGPlay 启动器打开此页面。'));
  const id = ++sequence;
  return decodeResponse<T>(await window.rpc(JSON.stringify({ jsonrpc: '2.0', id, method, params })), id);
}
