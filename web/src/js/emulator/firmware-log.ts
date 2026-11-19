/**
 * What the firmware in the page says in its log (Emulator 'log' events and
 * Emulator.bootLog: the lines a device writes to its UART) that the user has
 * to know, because the displays do not look as config.json or the font files
 * promise and nothing else tells why:
 *
 *   config    a part of config.json the firmware does not use: a key of the
 *             old format ('"style" is no longer read: use the top-level
 *             "fonts" and "styles"': without the fonts table there is no icon
 *             font, so every icon is LVGL's missing-glyph box), a refused
 *             "fonts" entry, a style it dropped.
 *   font      a font file the firmware was given and could not use (not a
 *             TrueType font, a damaged table, no memory for it). A file that
 *             is missing is not one of these: virtual-media.ts tells that.
 *
 * Every other warning or error goes to the Virtual console as it is; the
 * lines the firmware writes at warning level in every run are left out
 * (NOISE).
 */
import type { LogLevel } from './host';

export interface FirmwareLogEntry {
  level: LogLevel;
  tag: string;
  text: string;
}

/** Written in every run, or about the emulator itself: nothing the user can act on. */
const NOISE: readonly RegExp[] = [
  /^fonts: \d+ of \d+ config fonts accepted/,
  /^fonts reloaded:/,
  /^Deprecated: /,
  /^Subscription list grows past its reserved/,
  // A file that is not there (yet): virtual-media.ts tells of a font, and an icon raster is rendered in the page.
  /^images\/\S+: not found$/,
  /^fonts: \S+: \S+ not loaded \(media error 2\)$/,
];

/** media_err_t MEDIA_ERR_NOT_FOUND (src/media/media_store.h): the file is not there. */
const MEDIA_ERR_NOT_FOUND = 2;

/**
 * The message of a log line. LVGL wraps its own:
 * "[Warn]\t(0.000, +0)\t fonts_configure: fonts: 0 of 0 config fonts accepted fonts.c:699"
 * is "fonts: 0 of 0 config fonts accepted".
 */
export function logMessage(entry: FirmwareLogEntry): string {
  const text = entry.text.trim();
  if (entry.tag !== 'lvgl') return text;
  const wrapped = /^\[\w+\]\s*\([^)]*\)\s*\w+:\s*(.*?)(?:\s+[\w.-]+\.(?:c|cpp|h):\d+)?$/s.exec(text);
  return wrapped ? wrapped[1]!.trim() : text;
}

/** A warning or an error of the firmware that is not written in every run. */
export function isProblem(entry: FirmwareLogEntry): boolean {
  if (entry.level !== 'W' && entry.level !== 'E') return false;
  const message = logMessage(entry);
  return message !== '' && !NOISE.some(pattern => pattern.test(message));
}

/** The font file the firmware was given and did not load, with its reason; null: the line says nothing of the kind. */
export function fontRefusal(entry: FirmwareLogEntry): { face: string; key: string; reason: string } | null {
  if (entry.tag !== 'lvgl') return null;
  const message = logMessage(entry);
  const refused = /^fonts: (\S+): (\S+) not loaded: (.+)$/.exec(message);
  if (refused) return { face: refused[1]!, key: refused[2]!, reason: refused[3]! };
  const media = /^fonts: (\S+): (\S+) not loaded \(media error (\d+)\)$/.exec(message);
  if (!media || Number(media[3]) === MEDIA_ERR_NOT_FOUND) return null;
  return { face: media[1]!, key: media[2]!, reason: `the media store gave error ${media[3]}` };
}

/** A part of config.json the firmware said it does not use (its own words); null: the line says nothing of the kind. */
export function configProblem(entry: FirmwareLogEntry): string | null {
  if (!isProblem(entry)) return null;
  const message = logMessage(entry);
  if (entry.tag === 'ConfigManager') return message;
  if (entry.tag === 'lvgl' && /^(fonts: config font |styles?[:.])/.test(message)) return message;
  return null;
}

/** What one emulator's firmware said so far that belongs under the virtual device. */
export class FirmwareNotes {
  private readonly config: string[] = [];
  /** Font file -> why the firmware did not load it. */
  private readonly fonts = new Map<string, string>();

  constructor(entries: readonly FirmwareLogEntry[] = []) {
    for (const entry of entries) this.add(entry);
  }

  /** Takes a log line; true when hint() changed. */
  add(entry: FirmwareLogEntry): boolean {
    const refusal = fontRefusal(entry);
    if (refusal) {
      if (this.fonts.get(refusal.key) === refusal.reason) return false;
      this.fonts.set(refusal.key, refusal.reason);
      return true;
    }
    const problem = configProblem(entry);
    if (problem === null || this.config.includes(problem)) return false;
    this.config.push(problem);
    return true;
  }

  /** The firmware gets another version of a font file: it says again when it cannot use that one. True when hint() changed. */
  fontChanged(key: string): boolean {
    return this.fonts.delete(key);
  }

  /** Font files the firmware was given and could not use. */
  refusedFonts(): string[] {
    return [...this.fonts.keys()].sort();
  }

  /** The parts of config.json the firmware said it does not use. */
  configProblems(): readonly string[] {
    return this.config;
  }

  /** What to say under the virtual device, '' when nothing. */
  hint(): string {
    const parts: string[] = [];
    const fonts = [...this.fonts].sort(([a], [b]) => a.localeCompare(b));
    if (fonts.length) {
      parts.push(`The firmware cannot use ${fonts.map(([key, reason]) => `${key} (${reason})`).join(', ')}: its fallback font shows`);
    }
    if (this.config.length) {
      const more = this.config.length - 1;
      parts.push(`The firmware ignores a part of config.json: ${this.config[0]}${more > 0 ? ` (and ${more} more, listed in the Virtual console at Run)` : ''}`);
    }
    return parts.join('. ');
  }
}
