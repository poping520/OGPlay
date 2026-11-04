import { describe, expect, it } from 'vitest';
import { decodeResponse } from './rpc';
describe('GUI RPC response boundary', () => {
  it('preserves host facts', () => {
    expect(decodeResponse({ jsonrpc: '2.0', id: 8, result: { can_launch: false } }, 8)).toEqual({ can_launch: false });
  });
  it('rejects mismatched envelopes and missing results', () => {
    for (const value of [null, 'text', { jsonrpc: '2.0', id: 9, result: {} }, { jsonrpc: '2.0', id: 8 }])
      expect(() => decodeResponse(value, 8)).toThrow();
  });
  it('presents actionable host failures', () => {
    expect(() => decodeResponse({ jsonrpc: '2.0', id: 8, error: { code: -32004, message: 'missing', next_step: 'refresh' } }, 8)).toThrow('missing\nrefresh');
  });
});

it('translates host templates and next steps with parameters', async () => {
  const { setLanguage } = await import('./i18n');
  setLanguage('en-US');
  try {
    expect(() => decodeResponse({ jsonrpc: '2.0', id: 8, error: {
      code: -32001, message: '游戏当前不可启动（{status}）', params: { status: 'running' }, next_step: '刷新游戏库后重试。',
    } }, 8)).toThrow('The game cannot be started (running)\nRefresh the library, then try again.');
  } finally { setLanguage('zh-CN'); }
});
