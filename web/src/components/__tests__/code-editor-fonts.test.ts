import { describe, it, expect, vi, afterEach } from 'vitest';
import * as deviceConfig from '../../js/device-config';
import { fontLoader } from '../../js/font-loader';
import '../fb-code-editor.js';
import { deviceFontFamilies } from '../fb-code-editor.js';

afterEach(() => { vi.restoreAllMocks(); document.body.innerHTML = ''; });

const face = (index: number, name: string, src: string) => ({ index, name, id: 0, src, system: index < 2 });

describe('fb-code-editor device fonts', () => {
  it('draws icon glyphs with the loaded device fonts', async () => {
    vi.spyOn(deviceConfig, 'getFontTable').mockReturnValue({
      faces: [face(0, 'montserrat', 'medium.ttf'), face(1, 'montserrat_semibold', 'semibold.ttf'), face(2, 'mdi', 'icons.ttf')],
    } as never);
    const loaded = vi.spyOn(fontLoader, 'isLoaded').mockImplementation((name: string) => name !== 'montserrat_semibold');
    expect(deviceFontFamilies()).toBe('"fb-mdi", "fb-montserrat"');

    const el = document.createElement('fb-code-editor') as HTMLElement;
    document.body.appendChild(el);
    await new Promise(r => setTimeout(r, 0));
    expect(el.style.getPropertyValue('--fb-device-fonts')).toBe('"fb-mdi", "fb-montserrat"');

    // The icon characters of the open text are marked, other text is not.
    const { setCode } = await import('../../js/editor.js');
    setCode('{"label": "\u{F075E}", "x": 1}');
    await new Promise(r => setTimeout(r, 0));
    const marked = Array.from(el.querySelectorAll('.cm-fb-glyph')).map(n => n.textContent);
    expect(marked).toEqual(['\u{F075E}']);

    // A font went away (other device, deleted): the variable follows.
    loaded.mockReturnValue(false);
    window.dispatchEvent(new CustomEvent('preview-fonts-change'));
    expect(el.style.getPropertyValue('--fb-device-fonts')).toBe('');
  });
});
