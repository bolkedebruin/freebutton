/**
 * The analog clock of a config keeps moving by itself: one changed frame per
 * second, idle and with a script running. (The HTML face of stage 2 drew the
 * hands once and stood still until something else changed.)
 */
import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { deviceId, makeEmulator, repoPath } from './emulator-helpers';
import { repoMedia } from './emulator-pixels';
import { ManualClock } from '../emulator/clock';

const ID = deviceId('btn_a00001');
const DIR = repoPath(`devices/${ID}`);
const present = existsSync(`${DIR}/config.json`);   // the device folders are a repository of their own

const sum = (d: Uint8ClampedArray): number => {
  let h = 0;
  for (let i = 0; i < d.length; i += 4) h = (h * 31 + d[i]! + d[i + 1]! * 7 + d[i + 2]! * 13) | 0;
  return h;
};

describe.skipIf(!present)('the clock of btn_a00001 moves', () => {
  for (const mode of ['idle', 'running'] as const) {
    it(`${mode}: a new frame every second`, async () => {
      const config = JSON.parse(readFileSync(`${DIR}/config.json`, 'utf8'));
      const clock = new ManualClock(Date.UTC(2026, 10, 16, 10, 0, 0));
      const { emu } = await makeEmulator({ config, clock, media: repoMedia({}, { icons: true }) });
      if (mode === 'running') emu.run(readFileSync(`${DIR}/${ID}.js`, 'utf8'));
      clock.advance(200);
      let last = sum(emu.readPixels('main'));
      let changes = 0;
      for (let s = 0; s < 10; s++) {
        clock.advance(1000);
        const now = sum(emu.readPixels('main'));
        if (now !== last) changes++;
        last = now;
      }
      expect(changes).toBe(10);
      emu.dispose();
    });
  }
});
