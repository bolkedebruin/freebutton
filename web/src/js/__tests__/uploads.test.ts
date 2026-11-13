import { describe, it, expect, vi, afterEach } from 'vitest';
import { uploadDropped } from '../uploads';
import { workspace } from '../workspace';

afterEach(() => vi.restoreAllMocks());

describe('uploadDropped', () => {
  it('sends each file where its type belongs and names the rest', async () => {
    const svg = vi.spyOn(workspace, 'uploadSvg').mockResolvedValue('icons/sun.svg');
    const icon = vi.spyOn(workspace, 'addIconToConfig').mockResolvedValue(true as never);
    const font = vi.spyOn(workspace, 'uploadFont').mockResolvedValue(null);
    const fontCfg = vi.spyOn(workspace, 'addFontToConfig').mockResolvedValue(true as never);
    const lines: string[] = [];
    const onLine = (e: Event) => lines.push((e as CustomEvent).detail.text);
    window.addEventListener('console-line', onLine);

    const files = [new File(['<svg/>'], 'Sun.SVG'), new File(['x'], 'inter.ttf'), new File(['x'], 'photo.png')];
    const got = await uploadDropped(files);
    window.removeEventListener('console-line', onLine);

    expect(got).toEqual({ images: 1, fonts: 1 });
    expect(svg.mock.calls.map(c => (c[0] as File).name)).toEqual(['Sun.SVG']);
    expect(icon).toHaveBeenCalledWith('icons/sun.svg');
    expect(font.mock.calls.map(c => (c[0] as File).name)).toEqual(['inter.ttf']);
    expect(fontCfg).not.toHaveBeenCalled();   // the upload was refused
    expect(lines.join('\n')).toContain('photo.png');
  });
});
