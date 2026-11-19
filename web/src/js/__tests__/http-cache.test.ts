import { describe, it, expect } from 'vitest';
import { cacheModeFor, versionFor } from '../http-cache';

describe('versionFor', () => {
  it('versions media objects and files the editor does not change', () => {
    expect(versionFor('images', 'ico-sun-64x64.bin', '"26c5f41952ef9cd8514574610dcae9b3"')).toBe('26c5f41952ef9cd8514574610dcae9b3');
    expect(versionFor('fonts', 'medium.ttf', '"b3ba"')).toBe('b3ba');
    expect(versionFor('app', 'icons/batt-0.svg', '"d08-6af9cbd8"')).toBe('d08-6af9cbd8');
    expect(versionFor('app', 'screenshot-main.rgb565', '"25800-1"')).toBe('25800-1');
  });
  it('reads edited files and unknown objects fresh', () => {
    expect(versionFor('app', 'config.json', '"d08-6af9cbd8"')).toBeUndefined();
    expect(versionFor('app', 'scripts/main.js', '"1-2"')).toBeUndefined();
    expect(versionFor('app', 'notes.TXT', '"1-2"')).toBeUndefined();
    expect(versionFor('images', 'a.bin', undefined)).toBeUndefined();
    expect(versionFor('images', 'a.bin', '""')).toBeUndefined();
  });
});

describe('cacheModeFor', () => {
  it('uses the cache only for a versioned GET', () => {
    expect(cacheModeFor('GET', true)).toBe('force-cache');
    expect(cacheModeFor('GET', false)).toBe('no-store');
    expect(cacheModeFor('PUT', true)).toBe('no-store');
    expect(cacheModeFor('HEAD', true)).toBe('no-store');
  });
});
