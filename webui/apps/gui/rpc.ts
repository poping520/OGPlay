export interface Condition { status: string; value: string; detail: string }
export interface LibraryItem {
  installation_id: string; display_name: string; package: string; version: string;
  status: string; detail: string; running: boolean; can_launch: boolean; icon: string;
  profile: Condition; external: Condition;
  version_name: string | null; version_code: number | null; imported_at: string | null;
  sandbox_path: string; log_directory: string;
}
export interface Library { items: LibraryItem[]; library_root: string }
declare global {
  interface Window { rpc?: (request: string) => Promise<unknown>; __ogplaySmoke?: number }
}
let sequence = 0;
export function decodeResponse<T>(response: unknown, id: number): T {
  if (!response || typeof response !== 'object') throw new Error('宿主返回了无效响应。');
  const value = response as Record<string, unknown>;
  if (value.jsonrpc !== '2.0' || value.id !== id) throw new Error('宿主响应标识不匹配。');
  if (value.error) {
    const error = value.error as { message?: string; next_step?: string };
    throw new Error(`${error.message ?? '请求失败'}\n${error.next_step ?? '检查 GUI 日志后重试。'}`);
  }
  if (!('result' in value)) throw new Error('宿主响应缺少结果。');
  return value.result as T;
}
export async function rpc<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  if (!window.rpc) throw new Error('请通过 OGPlay 启动器打开此页面。');
  const id = ++sequence;
  return decodeResponse<T>(await window.rpc(JSON.stringify({ jsonrpc: '2.0', id, method, params })), id);
}
