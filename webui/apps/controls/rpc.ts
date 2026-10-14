export class RpcError extends Error { constructor(readonly code: number, message: string) { super(message); } }
export class Unauthorized extends Error { constructor() { super('令牌无效或已过期'); } }

const kStorageKey = 'ogplay.controls.token';
const kMaximumResponse = 32 * 1024 * 1024;

// The launcher opens /controls/#token=<hex>. The fragment never reaches the server; it is moved
// into this tab's session storage and removed from the address bar and history.
export function takeToken(location: { hash: string; pathname: string; search: string }, storage: Pick<Storage, 'getItem' | 'setItem'> | undefined,
                          replace: (url: string) => void): string | undefined {
  const match = /(?:^#|&)token=([0-9A-Za-z._~-]{16,})/.exec(location.hash);
  if (match) {
    try { storage?.setItem(kStorageKey, match[1]); } catch { /* storage may be unavailable */ }
    replace(location.pathname + location.search);
    return match[1];
  }
  try { return storage?.getItem(kStorageKey) ?? undefined; } catch { return undefined; }
}
export function rememberToken(token: string, storage: Pick<Storage, 'setItem'> | undefined) {
  try { storage?.setItem(kStorageKey, token); } catch { /* storage may be unavailable */ }
}

export type Rpc = <T>(method: string, params?: Record<string, unknown>) => Promise<T>;
let sequence = 0;
export function createRpc(token: string, fetcher: typeof fetch = fetch): Rpc {
  return async <T>(method: string, params: Record<string, unknown> = {}): Promise<T> => {
    const id = ++sequence;
    const response = await fetcher('/controls/rpc', {
      method: 'POST', credentials: 'omit',
      headers: { 'Content-Type': 'application/json', Accept: 'application/json', Authorization: `Bearer ${token}` },
      body: JSON.stringify({ jsonrpc: '2.0', id, method, params }),
    });
    if (response.status === 401) throw new Unauthorized();
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const text = await response.text();
    if (text.length > kMaximumResponse) throw new Error('响应超过大小限制');
    const value = JSON.parse(text) as { id?: number; result?: T; error?: { code: number; message: string } };
    if (value.id !== id) throw new Error('响应序号不匹配');
    if (value.error) throw new RpcError(value.error.code, value.error.message);
    if (value.result === undefined) throw new Error('响应缺少结果');
    return value.result;
  };
}

// Closing the editor must not leave the game frozen; keepalive survives page unload.
export function resumeOnUnload(token: string) {
  void fetch('/controls/rpc', {
    method: 'POST', keepalive: true, credentials: 'omit',
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
    body: JSON.stringify({ jsonrpc: '2.0', id: 0, method: 'control.resume', params: {} }),
  }).catch(() => undefined);
}
