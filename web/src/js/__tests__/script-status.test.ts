import { describe, it, expect } from 'vitest';
import { scriptStatusOf, waitForScriptStart, type ScriptStatus } from '../script-status';

const noSleep = () => Promise.resolve();
function reader(seq: Array<ScriptStatus | null | Error>) {
  let i = 0;
  return async () => {
    const v = seq[Math.min(i++, seq.length - 1)];
    if (v instanceof Error) throw v;
    return v;
  };
}

describe('scriptStatusOf', () => {
  it('reads the script fields', () => {
    expect(scriptStatusOf({ scriptStarts: 2, scriptError: '' })).toEqual({ starts: 2, error: '' });
    expect(scriptStatusOf({ scriptStarts: 1, scriptError: 'x is not defined' })).toEqual({ starts: 1, error: 'x is not defined' });
  });
  it('is null for older firmware', () => {
    expect(scriptStatusOf({ uptime: 5 })).toBeNull();
    expect(scriptStatusOf(null)).toBeNull();
  });
});

describe('waitForScriptStart', () => {
  const before = { starts: 1, error: '' };
  it('reports a new start without error as running', async () => {
    const r = await waitForScriptStart(reader([before, { starts: 2, error: '' }]), before, { sleep: noSleep });
    expect(r).toEqual({ kind: 'running' });
  });
  it('reports the exception of the new start', async () => {
    const r = await waitForScriptStart(reader([{ starts: 2, error: 'ReferenceError: foo' }]), before, { sleep: noSleep });
    expect(r).toEqual({ kind: 'failed', error: 'ReferenceError: foo' });
  });
  it('reports a script that was never started', async () => {
    const r = await waitForScriptStart(reader([{ starts: 1, error: 'checksum mismatch' }]), before, { sleep: noSleep, tries: 3 });
    expect(r).toEqual({ kind: 'failed', error: 'checksum mismatch' });
  });
  it('keeps asking while the device does not answer', async () => {
    const r = await waitForScriptStart(reader([new Error('busy'), new Error('busy'), { starts: 2, error: '' }]), before, { sleep: noSleep });
    expect(r).toEqual({ kind: 'running' });
  });
  it('is unknown when nothing changes', async () => {
    const r = await waitForScriptStart(reader([before]), before, { sleep: noSleep, tries: 3 });
    expect(r).toEqual({ kind: 'unknown' });
  });
});
