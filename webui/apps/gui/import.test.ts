import { describe, expect, it } from 'vitest';
import { upload, type Call } from './import';
describe('APK transfer', () => {
  it('rejects unsupported, empty and oversized files before contacting the host', async () => {
    const call: Call = async () => { throw new Error('unexpected RPC'); };
    for (const file of [{name: 'x.xapk', size: 10}, {name: 'x.apk', size: 0}, {name: 'x.apk', size: 1024 ** 3 + 1}])
      await expect(upload(file as File, () => {}, () => {}, call)).rejects.not.toThrow('unexpected RPC');
  });
  it('sends ordered bounded chunks and finishes only after every acknowledged write', async () => {
    const bytes = new Uint8Array(192 * 1024 + 13).map((_, i) => i % 256);
    const file = { name: '测试.APK', size: bytes.length, slice: (a: number, b: number) => new Blob([bytes.slice(a, b)]) } as File;
    const received: number[] = []; const methods: string[] = []; const progress: number[] = [];
    const call: Call = async <T>(method: string, params?: Record<string, unknown>) => {
      methods.push(method);
      if (method === 'library.upload.chunk') {
        expect(params?.offset).toBe(received.length);
        expect(String(params?.data).length).toBeLessThanOrEqual(262144);
        for (const value of atob(String(params?.data))) received.push(value.charCodeAt(0));
      }
      return { job: '7', state: 'analyzing' } as T;
    };
    let job = '';
    await upload(file, value => { job = value; }, value => progress.push(value), call);
    expect(job).toBe('7'); expect(new Uint8Array(received)).toEqual(bytes);
    expect(methods).toEqual(['library.upload.begin', 'library.upload.chunk', 'library.upload.chunk', 'library.upload.finish']);
    expect(progress.at(-1)).toBe(100);
  });
});
