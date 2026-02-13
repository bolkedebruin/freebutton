import { describe, it, expect, beforeEach } from 'vitest';
import { getConfig, setConfig, loadDefaultConfig, updateDisplayItem, updateButton } from '../device-config';

beforeEach(() => {
  loadDefaultConfig();
});

describe('device-config', () => {
  it('getConfig returns default config with 2 pages', () => {
    const cfg = getConfig();
    expect(cfg).toHaveLength(2);
    expect(cfg[0].number).toBe(1);
    expect(cfg[1].number).toBe(2);
  });

  it('page 1 has 2 display items', () => {
    const page1 = getConfig()[0];
    expect(page1.displayItems).toHaveLength(2);
    expect(page1.displayItems[0].label).toBe('Amsterdam');
    expect(page1.displayItems[0].value).toBe('time');
    expect(page1.displayItems[1].label).toBe('Button+ Likes');
    expect(page1.displayItems[1].value).toBe('67005');
  });

  it('page 1 has 4 buttons', () => {
    const page1 = getConfig()[0];
    expect(page1.buttons).toHaveLength(4);
    expect(page1.buttons[0].label).toBe('Tolomeo');
    expect(page1.buttons[1].label).toBe('Green Lamp');
    expect(page1.buttons[1].topLabel).toBe('Virtual');
    expect(page1.buttons[2].label).toBe('Like!');
    expect(page1.buttons[2].topLabel).toBe('Button+');
    expect(page1.buttons[3].label).toBe('Kantoor');
  });

  it('page 2 is empty', () => {
    const page2 = getConfig()[1];
    expect(page2.displayItems).toHaveLength(0);
    expect(page2.buttons).toHaveLength(0);
  });

  it('setConfig replaces config and emits device-config event', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    const newConfig = [{ number: 1, displayItems: [], buttons: [] }];
    setConfig(newConfig);

    expect(getConfig()).toEqual(newConfig);
    expect(received).toEqual({ pages: newConfig });

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('loadDefaultConfig restores defaults and emits event', () => {
    setConfig([]);
    expect(getConfig()).toHaveLength(0);

    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    loadDefaultConfig();
    expect(getConfig()).toHaveLength(2);
    expect(received?.pages).toHaveLength(2);

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('updateDisplayItem updates a specific item and emits event', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    updateDisplayItem(1, 0, { label: 'Berlin' });
    expect(getConfig()[0].displayItems[0].label).toBe('Berlin');
    expect(received).not.toBeNull();

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('updateDisplayItem ignores invalid page/index', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    updateDisplayItem(99, 0, { label: 'Nowhere' });
    expect(received).toBeNull();

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('updateButton updates a specific button and emits event', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    updateButton(1, 5, { label: 'New Label' });
    expect(getConfig()[0].buttons[0].label).toBe('New Label');
    expect(received).not.toBeNull();

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('updateButton ignores invalid position', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('device-config', handler as EventListener);

    updateButton(1, 99, { label: 'Ghost' });
    expect(received).toBeNull();

    window.removeEventListener('device-config', handler as EventListener);
  });

  it('LED states match defaults: 3 on, 1 off', () => {
    const buttons = getConfig()[0].buttons;
    const onCount = buttons.filter(b => b.ledOn).length;
    const offCount = buttons.filter(b => !b.ledOn).length;
    expect(onCount).toBe(3);
    expect(offCount).toBe(1);
  });
});
