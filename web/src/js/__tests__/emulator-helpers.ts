/**
 * Shared by the emulator-*.test.ts files: the wasm from web/public/assets,
 * an Emulator on a ManualClock, and a recorder of its events.
 */
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import {
  Emulator, ManualClock, type EmulatorOptions, type PublishEvent, type ScriptError, type StateChange,
} from '../emulator';

export const repoPath = (relative: string): string =>
  fileURLToPath(new URL('../../../../' + relative, import.meta.url));

export const WASM_PATH = repoPath('web/public/assets/fb-emulator.wasm');

let module: WebAssembly.Module | null = null;
/** The compiled wasm, once per test file. */
export function emulatorModule(): WebAssembly.Module {
  module ??= new WebAssembly.Module(readFileSync(WASM_PATH));
  return module;
}

export function readRepoFile(relative: string): string | null {
  const path = repoPath(relative);
  return existsSync(path) ? readFileSync(path, 'utf8') : null;
}

/** Main display on connector 0 (keys 1, 2), one bar on connector 1 (keys 3, 4), one broker. */
export const BASIC_CONFIG = {
  core: { name: 'Test Button' },
  info: { connectors: [{ id: 0, type: 2 }, { id: 1, type: 1 }] },
  // No Home Assistant discovery: the tests that are about it turn it on.
  brokers: [{ brokerid: 'local', url: 'mqtt://broker.test/', port: 1883, hassdiscovery: false }],
  styles: { size32: { 'font-size': 32 } },
  buttons: [
    { position: 1, label: 'M-L', toplabel: 'Main' },
    { position: 2, label: 'M-R' },
    { name: 'light', position: 3, label: 'Light', toplabel: 'Room' },
    { name: 'blind', position: 4, label: 'Blind' },
  ],
  displayitems: [
    { name: 'temp', displayitemid: 7, x: 10, y: 20, width: 40, page: 1, style: 'size32', align: 1, label: 'Temperature', unit: '°C' },
    { name: 'power', x: 50, y: 20, width: 40, page: 2, label: 'Power', unit: 'W', round: 1, boxtype: 2 },
  ],
  lists: [{ name: 'scenes', items: ['Morning', 'Evening', 'Night'], selected: 1 }],
  images: [{ name: 'weather', x: 0, y: 0, width: 48, height: 48, icon: 'sun' }],
  icons: { sun: '/icons/sun.svg', moon: '/icons/moon.svg' },
};

export interface Recorder {
  console: string[];
  errors: ScriptError[];
  publishes: PublishEvent[];
  subscribes: string[];
  unsubscribes: string[];
  changes: StateChange[];
  keys: string[];
  exits: string[];
  logs: string[];
}

export interface TestEmulator {
  emu: Emulator;
  clock: ManualClock;
  rec: Recorder;
}

export async function makeEmulator(options: Partial<EmulatorOptions> = {}): Promise<TestEmulator> {
  const clock = options.clock instanceof ManualClock ? options.clock : new ManualClock();
  const emu = await Emulator.create({
    config: BASIC_CONFIG,
    wasm: emulatorModule(),
    identity: 'virt_btn_0a1b2c',
    // The firmware's 1 s sensor sample publishes every sensor's state; tests
    // about sensors pass sampleSensors: true.
    sampleSensors: false,
    ...options,
    clock,
  });
  const rec: Recorder = {
    console: [], errors: [], publishes: [], subscribes: [], unsubscribes: [], changes: [], keys: [], exits: [], logs: [],
  };
  emu.on('console', line => rec.console.push(line));
  emu.on('error', error => rec.errors.push(error));
  emu.on('publish', event => rec.publishes.push(event));
  emu.on('subscribe', event => rec.subscribes.push(`${event.broker}:${event.topic}`));
  emu.on('unsubscribe', event => rec.unsubscribes.push(`${event.broker}:${event.topic}`));
  emu.on('state', change => rec.changes.push(change));
  emu.on('key', event => rec.keys.push(`${event.position}:${event.event}${event.defaultPrevented ? ':prevented' : ''}`));
  emu.on('exit', event => rec.exits.push(event.reason));
  emu.on('log', entry => rec.logs.push(`${entry.level} ${entry.tag}: ${entry.text}`));
  return { emu, clock, rec };
}

/**
 * The portal ID in the Victron topics of the device scripts
 * ("victron/N/<portal ID>/..."). It belongs to one installation, so the tests
 * read it from the device files (docs/DEVICES.md) and do not carry it.
 */
export const VICTRON_PORTAL =
  /victron\/N\/([0-9a-f]{12})\//.exec(readRepoFile('devices/btn_a00001/btn_a00001.js') ?? '')?.[1] ?? '0123456789ab';
