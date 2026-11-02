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
