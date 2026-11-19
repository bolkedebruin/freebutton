/**
 * Runs before every test file (vitest.config.ts setupFiles).
 *
 * The virtual device connects to the brokers of config.json at Run (live
 * publish) unless another mode was chosen. A test has no broker: the default
 * is pinned to offline here, before each test, so that no test opens a
 * WebSocket by accident. A test about the live default sets it itself
 * (setDefaultStartMode('live-publish')) and uses the local test broker.
 */
import { beforeEach } from 'vitest';
import { setDefaultStartMode } from './js/virtual-broker';

beforeEach(() => {
  setDefaultStartMode('offline');
});
