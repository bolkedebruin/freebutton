/**
 * Uploading images (SVG) and fonts from this computer: the same steps for
 * the "+" of the Images and Fonts panels and for files dropped on them.
 * After each upload the matching config.json entry is offered (it asks).
 */
import { emit } from './app-state';
import { workspace } from './workspace';

type Picked = File;

export const isSvgFile = (f: { name: string }) => /\.svg$/i.test(f.name);
export const isFontFile = (f: { name: string }) => /\.(ttf|otf)$/i.test(f.name);

/** Upload SVG files to icons/<name>.svg. */
export async function uploadImages(files: readonly Picked[]): Promise<void> {
  for (const file of files) {
    const key = await workspace.uploadSvg(file);
    // Offer the matching config.json "icons" entry (asks first).
    if (key) await workspace.addIconToConfig(key);
  }
}

/** Upload .ttf / .otf files to the device's fonts. */
export async function uploadFonts(files: readonly Picked[]): Promise<void> {
  for (const file of files) {
    const key = await workspace.uploadFont(file);
    // Offer the matching config.json "fonts" entry (asks first).
    if (key) await workspace.addFontToConfig(key);
  }
}

/**
 * Files dropped on the Images or Fonts view: each goes where its type
 * belongs (.svg: images; .ttf / .otf: fonts), whichever of the two is shown.
 * Other files are named in the console and not uploaded. Returns what was
 * taken, so the caller can show the view of it.
 */
export async function uploadDropped(files: readonly Picked[]): Promise<{ images: number; fonts: number }> {
  const images = files.filter(isSvgFile);
  const fonts = files.filter(isFontFile);
  const other = files.filter(f => !isSvgFile(f) && !isFontFile(f));
  if (other.length) {
    emit('console-line', { to: 'device',
      text: `Not uploaded (only .svg images and .ttf / .otf fonts can be dropped here): ${other.map(f => f.name).join(', ')}`,
      type: 'warn',
    });
  }
  await uploadImages(images);
  await uploadFonts(fonts);
  return { images: images.length, fonts: fonts.length };
}
