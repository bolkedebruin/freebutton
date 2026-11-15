import { describe, it, expect } from 'vitest';
import { cacheModeFor } from '../http-cache';

describe('cacheModeFor', () => {
  it('fetches user files, listings and the API fresh', () => {
    expect(cacheModeFor('GET', '/app/config.json')).toBe('no-store');
    expect(cacheModeFor('GET', '/app/scripts/main.js')).toBe('no-store');
    expect(cacheModeFor('GET', '/images')).toBe('no-store');
    expect(cacheModeFor('GET', '/images/')).toBe('no-store');
    expect(cacheModeFor('GET', '/fonts?list-type=2')).toBe('no-store');
    expect(cacheModeFor('GET', '/api/device')).toBe('no-store');
  });
  it('keeps the browser cache for media objects', () => {
    expect(cacheModeFor('GET', '/images/weather/sun-64.bin')).toBe('default');
    expect(cacheModeFor('get', '/fonts/medium.ttf')).toBe('default');
  });
  it('never caches writes or HEAD', () => {
    expect(cacheModeFor('PUT', '/images/a.bin')).toBe('no-store');
    expect(cacheModeFor('HEAD', '/fonts/medium.ttf')).toBe('no-store');
    expect(cacheModeFor('DELETE', '/images/a.bin')).toBe('no-store');
  });
});
