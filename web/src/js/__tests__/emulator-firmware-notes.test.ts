/**
 * What the firmware in the page says in its log and nothing else tells
 * (emulator/firmware-log.ts, wired by emulator-session.ts): a config.json in
 * the old format has its fonts table under "style", which the firmware no
 * longer reads. Without the table there is no icon font: every MDI glyph is
 * LVGL's missing-glyph box while the text looks right, and no font file is
 * missing or failed. The same for a font file the firmware was given and
 * refused. Both are said under the virtual device and in the Virtual console.
 */
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { BASIC_CONFIG, emulatorModule, repoPath } from './emulator-helpers';
import { Emulator, ManualClock, type MediaCache, type MediaSource } from '../emulator';
import { FirmwareNotes, configProblem, fontRefusal, isProblem, logMessage, type FirmwareLogEntry } from '../emulator/firmware-log';
import { EmulatorSession, _forgetCanDraw } from '../emulator-session';
import { installFakeCanvas } from '../../components/__tests__/fake-canvas';
import type { ConsoleLine } from '../app-state';

const FONT_KEYS = ['icons.ttf', 'medium.ttf', 'semibold.ttf'];
const font = (key: string): Uint8Array | null => {
  const path = repoPath(`media/fonts/${key}`);
  return FONT_KEYS.includes(key) && existsSync(path) ? new Uint8Array(readFileSync(path)) : null;
};
const ALL_FONTS: MediaSource = { read: (bucket, key) => (bucket === 'fonts' ? font(key) : null) };

const FONTS = [
  { id: 1, name: 'mdi', src: 'icons.ttf' },
  { name: 'montserrat', fallback: 'mdi' },
  { name: 'montserrat_semibold', fallback: 'mdi' },
];
/** Key 3 shows an MDI glyph (volume-minus), from the icon font through the fallback of the system font. */
const BUTTONS = [
  { position: 1, label: 'M-L' },
  { position: 2, label: 'M-R' },
  { name: 'light', position: 3, label: '\u{F075E}', toplabel: 'Volume' },
  { name: 'blind', position: 4, label: 'Blind' },
];
const CONFIG = JSON.stringify({ ...BASIC_CONFIG, fonts: FONTS, buttons: BUTTONS });
/** The same in the format the firmware read before: the fonts table under "style". */
const OLD_CONFIG = JSON.stringify({ ...BASIC_CONFIG, style: { fonts: FONTS }, buttons: BUTTONS });
const STYLE_WARNING = '"style" is no longer read: use the top-level "fonts" and "styles"';
const AT = Date.UTC(2026, 10, 12, 9, 10, 0);
const wait = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));

function create(config: string, media: MediaSource): Promise<Emulator> {
  return Emulator.create({ config, wasm: emulatorModule(), identity: 'virt_btn_0a1b2c', clock: new ManualClock(AT), sampleSensors: false, media });
}

const key3 = (emu: Emulator): Uint8ClampedArray => emu.screenshot('key3').data;
const same = (a: Uint8ClampedArray, b: Uint8ClampedArray): boolean => a.length === b.length && a.every((value, i) => value === b[i]);

/** Key 3 with the icon font, and without it (the missing-glyph box). */
async function references(): Promise<{ drawn: Uint8ClampedArray; boxes: Uint8ClampedArray }> {
  const all = await create(CONFIG, ALL_FONTS);
  const without = await create(CONFIG, { read: (bucket, key) => (bucket === 'fonts' && key !== 'icons.ttf' ? font(key) : null) });
  const out = { drawn: key3(all), boxes: key3(without) };
  all.dispose();
  without.dispose();
  expect(same(out.drawn, out.boxes)).toBe(false);
  return out;
}

const lvgl = (text: string, level: FirmwareLogEntry['level'] = 'W'): FirmwareLogEntry => ({ level, tag: 'lvgl', text });

describe('firmware-log: the lines of the firmware\'s log', () => {
  it('logMessage takes LVGL\'s wrapping off', () => {
    expect(logMessage(lvgl('[Warn]\t(0.000, +0)\t fonts_configure: fonts: 0 of 0 config fonts accepted, 2 faces fonts.c:699')))
      .toBe('fonts: 0 of 0 config fonts accepted, 2 faces');
    expect(logMessage(lvgl('[Warn]\t(1.500, +3)\t sync_fonts: fonts: mdi: icons.ttf not loaded: bad loca fonts.c:432')))
      .toBe('fonts: mdi: icons.ttf not loaded: bad loca');
    expect(logMessage({ level: 'W', tag: 'ConfigManager', text: `${STYLE_WARNING}\n` })).toBe(STYLE_WARNING);
  });

  it('isProblem: warnings and errors, but not what every run writes', () => {
    expect(isProblem({ level: 'W', tag: 'ConfigManager', text: STYLE_WARNING })).toBe(true);
    expect(isProblem({ level: 'I', tag: 'ConfigManager', text: 'Loaded 4 of 4 fonts' })).toBe(false);
    expect(isProblem(lvgl('[Warn]\t(0.000, +0)\t fonts_configure: fonts: 4 of 4 config fonts accepted, 4 faces fonts.c:699'))).toBe(false);
    expect(isProblem(lvgl('[Warn]\t(0.001, +1)\t lv_obj_has_flag: Deprecated: use the dedicated lv_obj_is_<flag>() getters instead, e.g. lv_obj_is_hidden(obj). lv_obj.c:861'))).toBe(false);
    expect(isProblem({ level: 'W', tag: 'mqtt', text: 'Subscription list grows past its reserved 0 entries' })).toBe(false);
    expect(isProblem(lvgl('[Warn]\t(0.0, +0)\t make: fonts: mdi 48 px: out of memory fonts.c:295'))).toBe(true);
  });

  it('fontRefusal: a file the firmware was given and did not load; a missing file is not one', () => {
    expect(fontRefusal(lvgl('[Warn]\t(0.0, +0)\t sync_fonts: fonts: mdi: icons.ttf not loaded: bad loca fonts.c:432')))
      .toEqual({ face: 'mdi', key: 'icons.ttf', reason: 'bad loca' });
    expect(fontRefusal(lvgl('[Warn]\t(0.0, +0)\t sync_fonts: fonts: mdi: icons.ttf not loaded (media error 1) fonts.c:428')))
      .toEqual({ face: 'mdi', key: 'icons.ttf', reason: 'the media store gave error 1' });
    expect(fontRefusal(lvgl('[Warn]\t(0.0, +0)\t sync_fonts: fonts: mdi: icons.ttf not loaded (media error 2) fonts.c:413'))).toBeNull();
    expect(configProblem({ level: 'W', tag: 'ConfigManager', text: STYLE_WARNING })).toBe(STYLE_WARNING);
    expect(configProblem({ level: 'W', tag: 'mqtt', text: 'something else' })).toBeNull();
  });

  it('FirmwareNotes: the hint names the refused file and the first config problem, each once', () => {
    const notes = new FirmwareNotes();
    expect(notes.hint()).toBe('');
    expect(notes.add({ level: 'W', tag: 'ConfigManager', text: STYLE_WARNING })).toBe(true);
    expect(notes.add({ level: 'W', tag: 'ConfigManager', text: STYLE_WARNING })).toBe(false);
    expect(notes.add({ level: 'W', tag: 'ConfigManager', text: '"fontsize" is no longer read: use a style with "font-size"' })).toBe(true);
    expect(notes.hint()).toBe(`The firmware ignores a part of config.json: ${STYLE_WARNING} (and 1 more, listed in the Virtual console at Run)`);
    expect(notes.add(lvgl('[Warn]\t(0.0, +0)\t sync_fonts: fonts: mdi: icons.ttf not loaded: bad loca fonts.c:432'))).toBe(true);
    expect(notes.refusedFonts()).toEqual(['icons.ttf']);
    expect(notes.hint()).toContain('The firmware cannot use icons.ttf (bad loca): its fallback font shows. The firmware ignores');
    expect(notes.fontChanged('icons.ttf')).toBe(true);
    expect(notes.fontChanged('icons.ttf')).toBe(false);
    expect(notes.refusedFonts()).toEqual([]);
  });
});

describe('the firmware\'s start is kept (Emulator.bootLog)', () => {
  it('a config in the old format: the firmware says "style" is not read, and draws the icons as boxes with every font file there', async () => {
    const ref = await references();
    const asked: string[] = [];
    const emu = await create(OLD_CONFIG, { read: (bucket, key) => { asked.push(`${bucket}/${key}`); return ALL_FONTS.read(bucket, key); } });
    expect(same(key3(emu), ref.boxes)).toBe(true);
    // No font file is missing: the firmware never asks for the icon font.
    expect(asked).not.toContain('fonts/icons.ttf');
    const notes = new FirmwareNotes(emu.bootLog);
    expect(notes.configProblems()).toContain(STYLE_WARNING);
    expect(notes.refusedFonts()).toEqual([]);
    emu.dispose();
  }, 30000);

  it('a font file that is not a font: the firmware says it did not load it', async () => {
    const ref = await references();
    const bad = new Uint8Array(4096).fill(0x41);
    const emu = await create(CONFIG, { read: (bucket, key) => (key === 'icons.ttf' ? bad : ALL_FONTS.read(bucket, key)) });
    expect(same(key3(emu), ref.boxes)).toBe(true);
    const notes = new FirmwareNotes(emu.bootLog);
    expect(notes.refusedFonts()).toEqual(['icons.ttf']);
    expect(notes.hint()).toMatch(/^The firmware cannot use icons\.ttf \(.+\): its fallback font shows$/);
    const good = await create(CONFIG, ALL_FONTS);
    expect(new FirmwareNotes(good.bootLog).hint()).toBe('');
    good.dispose();
    emu.dispose();
  }, 30000);
});

describe('the IDE\'s virtual device (EmulatorSession) says what the firmware said', () => {
  let session: EmulatorSession;
  let lines: ConsoleLine[] = [];
  const onLine = (event: Event): void => { lines.push((event as CustomEvent<ConsoleLine>).detail); };
  const running = (): Emulator => (session as unknown as { emu: Emulator }).emu;
  const firmwareLines = (): string[] => lines.filter(line => line.to === 'virtual' && line.text.startsWith('Firmware ')).map(line => line.text);

  let removeCanvas: () => void = () => undefined;

  beforeEach(() => {
    lines = [];
    // The face is the firmware's only where the page can draw it.
    removeCanvas = installFakeCanvas();
    _forgetCanDraw();
    window.addEventListener('console-line', onLine);
    session = new EmulatorSession();
    session.wasm = emulatorModule();
    session.mediaSource = ALL_FONTS;
  });

  afterEach(() => {
    session.destroy();
    window.removeEventListener('console-line', onLine);
    removeCanvas();
    _forgetCanDraw();
  });

  it('Run with a config in the old format: boxes, with the reason under the device and in the Virtual console', async () => {
    const ref = await references();
    session.readSource = async () => ({ script: '', config: OLD_CONFIG });
    await session.run();
    expect(session.snapshot().status).toBe('running');
    expect(same(key3(running()), ref.boxes)).toBe(true);
    expect(session.faceInfo().hint).toContain(`The firmware ignores a part of config.json: ${STYLE_WARNING}`);
    expect(firmwareLines()).toEqual([`Firmware warning: ${STYLE_WARNING}`]);
    expect(lines.find(line => line.text.startsWith('Firmware '))?.type).toBe('warn');

    // Run again: said again for that run, once.
    lines = [];
    await session.run();
    expect(firmwareLines()).toEqual([`Firmware warning: ${STYLE_WARNING}`]);
  }, 30000);

  it('Run with the config as the firmware reads it: icons drawn, nothing said', async () => {
    const ref = await references();
    session.readSource = async () => ({ script: '', config: CONFIG });
    await session.run();
    expect(same(key3(running()), ref.drawn)).toBe(true);
    expect(session.faceInfo().hint).toBe('');
    expect(firmwareLines()).toEqual([]);
  }, 30000);

  it('the idle device (no Run) has the hint too', async () => {
    session.readConfig = () => OLD_CONFIG;
    session.showPreview();
    for (let i = 0; i < 100 && session.faceInfo().state !== 'engine'; i++) await wait(50);
    expect(session.faceInfo().state).toBe('engine');
    expect(session.faceInfo().hint).toContain(STYLE_WARNING);
    expect(firmwareLines()).toEqual([]);
  }, 30000);

  it('a font file the firmware refuses is named; a good one that comes later takes the hint away and is drawn', async () => {
    const ref = await references();
    const bad = new Uint8Array(4096).fill(0x41);
    session.mediaSource = { read: (bucket, key) => (key === 'icons.ttf' ? bad : ALL_FONTS.read(bucket, key)) };
    session.readSource = async () => ({ script: '', config: CONFIG });
    await session.run();
    expect(same(key3(running()), ref.boxes)).toBe(true);
    expect(session.faceInfo().hint).toMatch(/^The firmware cannot use icons\.ttf \(.+\): its fallback font shows$/);
    expect(firmwareLines()).toHaveLength(1);
    expect(firmwareLines()[0]).toMatch(/^Firmware warning: fonts: mdi: icons\.ttf not loaded: /);

    // As an upload of the font: the firmware reads its fonts again 1.5 s after the change.
    (session as unknown as { media: MediaCache }).media.set('fonts', 'icons.ttf', font('icons.ttf'));
    expect(session.faceInfo().hint).toBe('');
    await wait(2200);
    expect(same(key3(running()), ref.drawn)).toBe(true);
    expect(session.faceInfo().hint).toBe('');
  }, 30000);
});
