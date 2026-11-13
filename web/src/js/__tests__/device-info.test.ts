/**
 * Tests for the device-info HTTP poller: URL mapping, fetch, and the
 * connection-driven polling that republishes a `device-info` event.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  httpBaseFromWs,
  fetchDeviceInfo,
  initDeviceInfoPolling,
  setDeviceInfoVisible,
  _resetForTest,
} from '../device-info';
import type { DeviceInfoData } from '../app-state';

const SAMPLE: DeviceInfoData = {
  deviceName: 'btn_fab028', ipv4: '192.168.1.42', ipv6: 'fe80::1', ssid: 'HomeNetwork',
  rssi: -55, mac: 'AA:BB:CC:DD:EE:FF', firmware: '3.1.0', uptime: 720,
  heapFree: 184320, heapTotal: 327680, heapMinFree: 153600, lvglFree: 40960, lvglTotal: 61440,
};

describe('httpBaseFromWs', () => {
  it('maps ws:// to http://', () => {
    expect(httpBaseFromWs('ws://freebutton.local')).toBe('http://freebutton.local');
  });
  it('maps wss:// to https://', () => {
    expect(httpBaseFromWs('wss://freebutton.local')).toBe('https://freebutton.local');
  });
  it('leaves an empty (same-origin) base empty', () => {
    expect(httpBaseFromWs('')).toBe('');
  });
});

describe('fetchDeviceInfo', () => {
  afterEach(() => vi.unstubAllGlobals());

  it('fetches and parses /api/device from the given base', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    const data = await fetchDeviceInfo('http://dev.local');
    expect(fetchMock).toHaveBeenCalledWith('http://dev.local/api/device');
    expect(data.ipv4).toBe('192.168.1.42');
  });

  it('defaults to a same-origin relative path', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    await fetchDeviceInfo();
    expect(fetchMock).toHaveBeenCalledWith('/api/device');
  });

  it('throws on a non-ok response', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: false, status: 500, statusText: 'err' }));
    await expect(fetchDeviceInfo()).rejects.toThrow(/device info/);
  });

  it('throws on a malformed (contract-drifted) payload', async () => {
    // rssi renamed / missing -> must be rejected, not rendered as NaN.
    const bad = { ...SAMPLE } as Record<string, unknown>;
    delete bad.rssi;
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => bad }));
    await expect(fetchDeviceInfo()).rejects.toThrow(/[Mm]alformed/);
  });
});

describe('initDeviceInfoPolling', () => {
  const listeners: EventListener[] = [];

  /** Collect `device-info` payloads, remembering the listener so it can be removed. */
  function collectEvents(): (DeviceInfoData | null)[] {
    const events: (DeviceInfoData | null)[] = [];
    const fn = ((e: CustomEvent) => events.push(e.detail.data)) as EventListener;
    window.addEventListener('device-info', fn);
    listeners.push(fn);
    return events;
  }

  beforeEach(() => _resetForTest());
  afterEach(() => {
    _resetForTest();
    listeners.splice(0).forEach(fn => window.removeEventListener('device-info', fn));
    vi.unstubAllGlobals();
  });

  function nextTick() { return new Promise(r => setTimeout(r, 0)); }

  function connect(url = 'ws://freebutton.local') {
    window.dispatchEvent(new CustomEvent('connection-change', { detail: { connected: true, url } }));
  }

  it('emits device-info once visible and null on disconnect', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    const events = collectEvents();

    initDeviceInfoPolling(10_000); // long interval; we only assert the immediate poll
    connect();
    setDeviceInfoVisible(true);
    await nextTick();
    expect(fetchMock).toHaveBeenCalledWith('http://freebutton.local/api/device');
    expect(events[events.length - 1]).toEqual(SAMPLE);

    window.dispatchEvent(new CustomEvent('connection-change', { detail: { connected: false, url: '' } }));
    await nextTick();
    expect(events[events.length - 1]).toBeNull();
  });

  it('asks once on connect, then does not poll while not visible', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    initDeviceInfoPolling(5); // short cadence: the interval would fire many times
    connect();
    await new Promise(r => setTimeout(r, 40));
    // One request per connection (the virtual device shows the name and layout).
    expect(fetchMock).toHaveBeenCalledTimes(1);

    // ...and the interval stops again when the consumer goes away.
    setDeviceInfoVisible(true);
    await new Promise(r => setTimeout(r, 40));
    expect(fetchMock.mock.calls.length).toBeGreaterThan(1);

    setDeviceInfoVisible(false);
    const settled = fetchMock.mock.calls.length;
    await new Promise(r => setTimeout(r, 40));
    expect(fetchMock.mock.calls.length).toBe(settled);
  });

  it('polls immediately when a device connects while already visible', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    initDeviceInfoPolling(10_000);
    setDeviceInfoVisible(true);
    await nextTick();
    expect(fetchMock).not.toHaveBeenCalled(); // visible, but nothing to poll yet

    connect();
    await nextTick();
    expect(fetchMock).toHaveBeenCalledWith('http://freebutton.local/api/device');
  });

  it('emits null when the device fetch fails', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('network')));

    const events = collectEvents();

    initDeviceInfoPolling(10_000);
    connect();
    setDeviceInfoVisible(true);
    await nextTick();

    expect(events[events.length - 1]).toBeNull();
  });

  it('re-initialising after a stop does not leave a second listener behind', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => SAMPLE });
    vi.stubGlobal('fetch', fetchMock);

    initDeviceInfoPolling(10_000);
    _resetForTest(); // drops the connection-change listener
    initDeviceInfoPolling(10_000);

    connect();
    setDeviceInfoVisible(true);
    await nextTick();
    // Two listeners would each start an interval and fetch twice.
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });
});
