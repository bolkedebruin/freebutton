import { describe, it, expect } from 'vitest';
import { oneAtATime, queuedDeviceRequests } from '../device-queue';

const tick = () => new Promise<void>(r => setTimeout(r, 0));

describe('oneAtATime', () => {
  it('runs tasks one after the other, in the order asked', async () => {
    const log: string[] = [];
    let active = 0, most = 0;
    const task = (name: string, ms: number) => oneAtATime(async () => {
      active++; most = Math.max(most, active); log.push('start ' + name);
      await new Promise(r => setTimeout(r, ms));
      log.push('end ' + name); active--;
      return name;
    });
    const results = await Promise.all([task('a', 15), task('b', 1), task('c', 5)]);
    expect(results).toEqual(['a', 'b', 'c']);
    expect(most).toBe(1);
    expect(log).toEqual(['start a', 'end a', 'start b', 'end b', 'start c', 'end c']);
  });

  it('goes on after a task that fails, and passes the failure to its caller only', async () => {
    const failed = oneAtATime(async () => { throw new Error('boom'); });
    const next = oneAtATime(async () => 'fine');
    await expect(failed).rejects.toThrow('boom');
    await expect(next).resolves.toBe('fine');
  });

  it('counts the tasks that wait', async () => {
    let release!: () => void;
    const first = oneAtATime(() => new Promise<void>(r => { release = r; }));
    const second = oneAtATime(async () => 2);
    await tick();
    expect(queuedDeviceRequests()).toBe(1);
    release();
    await first; await second;
    expect(queuedDeviceRequests()).toBe(0);
  });
});
