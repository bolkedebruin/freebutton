import { describe, it, expect, vi } from 'vitest';
import { updateDevice, updateFileProblem, type UpdateKind, type UpdateProgress } from '../device-update';

const image = (first = 0xe9) => new Blob([new Uint8Array([first, 1, 2, 3])]);
const device = (uptime: number, firmware = '3.2.0') => new Response(JSON.stringify({ uptime, firmware, commit: 'abc1234' }));

const fsImage = () => new Blob([new Uint8Array(8), new TextEncoder().encode('littlefs')]);

function run(opts: { uploadStatus?: number; states: Array<number | null>; file?: Blob; kind?: UpdateKind }) {
  const seen: UpdateProgress[] = [];
  const posts: string[] = [];
  const order: string[] = [];
  const kind = opts.kind ?? 'firmware';
  const states = [...opts.states];
  const fetchImpl = vi.fn(async (url: string, init?: RequestInit) => {
    if (init?.method === 'POST') { posts.push(url); return new Response('ok'); }
    const next = states.shift();
    if (next === null || next === undefined) throw new Error('unreachable');
    return device(next);
  }) as unknown as typeof fetch;
  const upload = vi.fn(async (_url: string, body: FormData, _h: Record<string, string>, onPercent: (p: number) => void) => {
    for (const [k, v] of body.entries()) order.push(typeof v === 'string' ? `${k}=${v}` : k);
    onPercent(50); onPercent(100);
    return { status: opts.uploadStatus ?? 200, text: opts.uploadStatus ? 'storage busy' : 'Upload complete.' };
  });
  let t = 0;
  const result = updateDevice('http://dev/', kind, opts.file ?? (kind === 'firmware' ? image() : fsImage()), p => seen.push(p), {
    fetchImpl, upload, sign: async () => ({ Authorization: 'sig' }), sleep: async () => { t += 2000; }, now: () => t,
  });
  return { result, seen, posts, order, upload };
}

describe('updateDevice', () => {
  it('uploads with progress, applies, and is done when the device is back', async () => {
    // Uptime before, then: unreachable while restarting, back with a small uptime.
    const r = run({ states: [900, null, 12] });
    const last = await r.result;
    expect(r.order).toEqual(['updateType=firmware', 'updateFile']);
    expect(r.upload.mock.calls[0][0]).toBe('http://dev/upload');
    expect(r.upload.mock.calls[0][2]).toEqual({ Authorization: 'sig' });
    expect(r.posts).toEqual(['http://dev/ota_update']);
    expect(r.seen.map(p => p.phase + (p.percent !== undefined ? ':' + p.percent : ''))).toEqual(
      ['uploading:0', 'uploading:50', 'uploading:100', 'applying', 'done']);
    expect(last.message).toBe('The device is running 3.2.0 (abc1234).');
  });

  it('notices a restart it never saw as an outage, by the uptime', async () => {
    const r = run({ states: [900, 5] });
    expect((await r.result).phase).toBe('done');
  });

  it('stops when the device refuses the upload, and applies nothing', async () => {
    const r = run({ uploadStatus: 400, states: [900] });
    const last = await r.result;
    expect(last.phase).toBe('failed');
    expect(last.message).toContain('storage busy');
    expect(last.message).toContain('unchanged');
    expect(r.posts).toEqual([]);
  });

  it('fails after two minutes without the device', async () => {
    const r = run({ states: [900] });
    const last = await r.result;
    expect(last.phase).toBe('failed');
    expect(last.message).toContain('did not come back');
  });

  it('refuses a file that is not a firmware image before any request', async () => {
    const r = run({ states: [], file: image(0x50) });
    expect((await r.result).phase).toBe('failed');
    expect(r.upload).not.toHaveBeenCalled();
    expect(await updateFileProblem('firmware', new Blob([]))).toBe('The file is empty.');
    expect(await updateFileProblem('firmware', image())).toBeNull();
  });

  it('filesystem: checks the LittleFS magic, sends updateType=filesystem, reports the restart', async () => {
    expect(await updateFileProblem('filesystem', image())).toContain('not a LittleFS');
    expect(await updateFileProblem('filesystem', fsImage())).toBeNull();
    const r = run({ kind: 'filesystem', states: [900, null, 8] });
    const last = await r.result;
    expect(r.order).toEqual(['updateType=filesystem', 'updateFile']);
    expect(last).toEqual({ phase: 'done', message: 'The device restarted with the new filesystem.' });
  });
});
