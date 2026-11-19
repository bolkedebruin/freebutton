import { describe, it, expect } from 'vitest';
import {
  canDeleteKey, fileKind, iconNameForKey, isEditableKey, isStorableKey, svgUploadKey, validateBody,
  validateConfigShape, validateIcons, validateKey,
} from '../fs-rules';
import { configNames, configToModel, mapButton, mapDisplayItem, mapImage, validateConfigNames } from '../config-model';
import { validateCode } from '../validator';
import { contentTypeFor } from '../device-storage';

describe('SVG files in bucket app', () => {
  it('are storable and deletable but not opened in the editor', () => {
    expect(fileKind('icons/rain.svg')).toBe('svg');
    expect(isStorableKey('icons/rain.svg')).toBe(true);
    expect(isEditableKey('icons/rain.svg')).toBe(false);
    expect(canDeleteKey('icons/rain.svg')).toBe(true);
    expect(validateKey('icons/rain.svg')).toBeNull();
    expect(validateKey('icons/rain.png')).toBe('Only .js, .json, .txt and .svg files can be saved');
    expect(contentTypeFor('icons/rain.svg')).toBe('image/svg+xml');
    expect(contentTypeFor('ico-rain-48x48.bin')).toBe('application/octet-stream');
  });

  it('upload keys go to icons/ with a device-safe name', () => {
    expect(svgUploadKey('rain.svg')).toBe('icons/rain.svg');
    expect(svgUploadKey('C:\\pics\\weather rain (1).SVG')).toBe('icons/weather_rain_1.svg');
    expect(svgUploadKey('/home/u/ü.svg')).toBe('icons/icon.svg');
    expect(svgUploadKey('rain.png')).toBeNull();
    expect(iconNameForKey('icons/weather-rain.svg')).toBe('weather-rain');
  });
});

describe('config.json icons validation', () => {
  const ok = {
    icons: { rain: '/icons/rain.svg', 'sun-2': 'icons/sun.svg' },
    buttons: [{ position: 1, icon: 'rain', iconsize: 48 }, { position: 2, icon: '' }],
    displayitems: [{ displayitemid: 1, icon: 'sun-2', iconsize: 32, style: 'sunny' }],
    styles: { sunny: { icon: { color: '#ffd45f' } } },
    images: [
      { name: 'radar', page: 1, x: 10, y: 20, width: 120, height: 80, icon: 'rain', color: '00ff00' },
      { name: 'photo', width: 64, height: 64, src: 'photo.bin' },
    ],
  };

  it('accepts a consistent config', () => {
    expect(validateIcons(ok)).toEqual([]);
    expect(validateConfigShape(ok)).toEqual([]);
  });

  it('checks the icons table', () => {
    expect(validateIcons({ icons: [] })).toEqual(['"icons" must be an object ({"name": "/icons/name.svg"})']);
    expect(validateIcons({ icons: { 'bad name': '/a.svg', rain: '/rain.png', sun: 5, x: '/www/x.svg' } })).toEqual([
      'icons: "bad name" is not a valid icon name (1-31 letters, digits, _ or -)',
      'icons.rain must be the path of an .svg file on the device (e.g. "/icons/rain.svg")',
      'icons.sun must be the path of an .svg file on the device (e.g. "/icons/sun.svg")',
      'icons.x: www/ is read-only (device bucket "web")',
    ]);
  });

  it('checks icon references and sizes on buttons and display items; "iconcolor" is a removed key', () => {
    expect(validateIcons({
      icons: { rain: '/icons/rain.svg' },
      buttons: [{ position: 1, icon: 'snow', iconsize: 0 }, { position: 2, icon: 3, iconsize: 2.5 }],
      displayitems: [{ icon: 'rain', iconsize: 321, iconcolor: 'red' }, { icon: 'rain', iconsize: 320 }],
    })).toEqual([
      'buttons[0].icon "snow" is not in "icons"',
      'buttons[1].icon must be a string (an icon name from "icons")',
      'buttons[1].iconsize must be an integer <= 320 (<= 0: the default)',
      'displayitems[0].iconsize must be an integer <= 320 (<= 0: the default)',
    ]);
    expect(validateConfigShape({ icons: { rain: '/icons/rain.svg' }, displayitems: [{ icon: 'rain', iconcolor: '#ff0000' }] }))
      .toEqual(['"iconcolor" is no longer read: use a style with "icon": {"color"} (displayitems[0])']);
    expect(validateIcons({ buttons: [{ position: 1, icon: 'rain' }] })).toEqual(['buttons[0].icon "rain" is not in "icons"']);
  });

  it('checks images entries', () => {
    expect(validateIcons({
      icons: { rain: '/icons/rain.svg' },
      images: [
        { width: 0, height: 1.5, icon: 'rain', src: 'x.bin' },
        { name: 'a', width: 10, height: 10, x: 320, y: 12.5, page: -1, src: '../x.png', color: '#12345' },
        'nope',
      ],
    })).toEqual([
      'images[0].name is required',
      'images[0].width must be an integer 1..1024 (pixels)',
      'images[0].height must be an integer 1..1024 (pixels)',
      'images[0] has both "icon" and "src" (use one; the device would show the icon)',
      'images[1].page must be an integer >= 0',
      'images[1].x must be an integer 0..319 (pixels)',
      'images[1].y must be an integer 0..239 (pixels)',
      'images[1].src must be a key in the device\'s images bucket ([A-Za-z0-9._-], ending in .bin)',
      'images[1].color must be a colour "#rrggbb"',
      'images[2] must be an object',
    ]);
    expect(validateConfigShape({ images: {} })).toContain('"images" must be an array');
  });

  it('image names are a namespace of their own, validated and unique', () => {
    expect(validateConfigNames({
      lists: [{ name: 'radar' }],
      images: [{ name: 'radar', width: 1, height: 1 }, { name: 'radar', width: 1, height: 1 }, { name: '9x' }],
    })).toEqual([
      'images[1].name "radar" is already used by images[0] (image names must be unique)',
      'images[2].name "9x" is not a valid name (1-24 letters, digits or _, not starting with a digit)',
    ]);
  });

  it('a config.json save is refused with icon problems', () => {
    expect(validateBody('config.json', JSON.stringify({ buttons: [{ position: 1, icon: 'x' }] })))
      .toBe('config.json: buttons[0].icon "x" is not in "icons"');
  });
});

describe('config model with icons', () => {
  it('maps icon fields with their defaults', () => {
    expect(mapButton({ position: 1, icon: 'rain' })).toMatchObject({ icon: 'rain', iconSize: 48 });
    expect(mapButton({ position: 1, icon: 'rain', iconsize: 24 })).toMatchObject({ icon: 'rain', iconSize: 24 });
    expect(mapButton({ position: 1, icon: 'rain', iconsize: -3 })).toMatchObject({ iconSize: 48 });
    expect(mapButton({ position: 1, icon: 'bad name' })?.icon).toBeUndefined();
    expect(mapDisplayItem({ icon: 'sun' })?.item).toMatchObject({ icon: 'sun', iconSize: 32 });
    // The tint: "icon.color" of the item's styles, else of its kind; none: the icon's own colours in the preview.
    const styles = configToModel({ styles: { button: { icon: { color: '#00ff00' } }, sunny: { icon: { color: '#FFD45F' } }, red: { color: '#ff0000' } } }).styles;
    expect(mapDisplayItem({ icon: 'sun', style: 'sunny' }, styles)?.item.iconColor).toBe('#ffd45f');
    expect(mapDisplayItem({ icon: 'sun', style: 'red' }, styles)?.item.iconColor).toBeUndefined();
    expect(mapDisplayItem({ icon: 'sun', iconcolor: '#ff0000' })?.item.iconColor).toBeUndefined();
    expect(mapButton({ position: 1, icon: 'rain' }, styles)?.iconColor).toBe('#00ff00');
    expect(mapButton({ position: 1, icon: 'rain', style: 'sunny' }, styles)?.iconColor).toBe('#ffd45f');
  });

  it('maps images onto their pages', () => {
    expect(mapImage({ name: 'radar', width: 120, height: 80, icon: 'rain', color: '#00FF00' })).toEqual({
      name: 'radar', page: 1, x: 0, y: 0, width: 120, height: 80, icon: 'rain', color: '#00ff00',
    });
    expect(mapImage({ name: 'radar', x: 200.4, y: 160, width: 120, height: 80 })).toMatchObject({ x: 200, y: 160 });
    expect(validateIcons({ images: [{ name: 'r', x: 319, y: 239, width: 1, height: 1 }] })).toEqual([]);
    // No size: kept (a src raster has its own size), without width/height.
    expect(mapImage({ name: 'radar', width: 0, height: 80, src: 'r.bin' })).toEqual({ name: 'radar', page: 1, x: 0, y: 0, src: 'r.bin' });
    expect(validateIcons({ images: [{ name: 'r', src: 'r.bin' }] })).toEqual([]);
    expect(validateIcons({ icons: { rain: '/r.svg' }, images: [{ name: 'r', icon: 'rain' }] })).toEqual([
      'images[0].width must be an integer 1..1024 (pixels)',
      'images[0].height must be an integer 1..1024 (pixels)',
    ]);
    expect(mapImage({ width: 10, height: 10 })).toBeNull();
    const model = configToModel({
      images: [{ name: 'a', page: 2, width: 8, height: 8 }, { name: 'b', page: 0, width: 8, height: 8, src: 'b.bin' }],
    });
    expect(model.pages.map(p => [p.number, p.images?.map(i => i.name)])).toEqual([[1, ['b']], [2, ['a', 'b']]]);
    expect(configToModel({}).pages[0].images).toBeUndefined();
  });

  it('exposes image and icon names for scripts', () => {
    const names = configNames({ icons: { rain: '/r.svg' }, images: [{ name: 'radar' }] });
    expect(names.images).toEqual(['radar']);
    expect(names.icons).toEqual(['rain']);
  });
});

describe('script validation of image and icon names', () => {
  const names = { keys: [], displayItems: ['temp'], lists: [], images: ['radar'], icons: ['rain', 'sun'] };

  it('checks image names for fb.main.image methods', () => {
    expect(validateCode('fb.main.image.setIcon("radar", "rain");\nfb.main.image.setSrc("radar", "x.bin");', names).errors).toEqual([]);
    expect(validateCode('fb.main.image.setSrc("radr", "x.bin");', names).errors.map(e => e.message))
      .toEqual(['unknown image "radr" in config.json']);
  });

  it('checks icon names given to setIcon; "" hides', () => {
    const msgs = (code: string) => validateCode(code, names).errors.map(e => e.message);
    expect(msgs('fb.main.display.setIcon("temp", "sun");\nfb.main.left.display.setIcon("");')).toEqual([]);
    expect(msgs('fb.main.display.setIcon("temp", "snow");')).toEqual(['unknown icon "snow" in config.json']);
    expect(msgs('fb.main.image.setIcon("radar", "snow");')).toEqual(['unknown icon "snow" in config.json']);
    expect(msgs('fb.bars[0].left.display.setIcon("snow");')).toEqual(['unknown icon "snow" in config.json']);
    expect(msgs('fb.main.display.setIcon("tmp", "sun");')).toEqual(['unknown display item "tmp" in config.json']);
  });

  it('does not check icon names without an icons table', () => {
    const noIcons = { keys: [], displayItems: ['temp'], lists: [] };
    expect(validateCode('fb.main.display.setIcon("temp", "snow");', noIcons).errors).toEqual([]);
  });
});
