/**
 * The properties of a config.json entry that the virtual device's popover
 * edits, per kind of entry (pure). Only keys the firmware reads
 * (DisplayItem.h, List.cpp, ImageItem.cpp, Button.h `configure`), with the
 * ranges it accepts.
 *
 *   itemFields(kind, raw)       the fields, in the order shown
 *   fieldValue(field, input)    what the user entered -> the value written
 *                               (undefined: the key is removed and the
 *                               firmware's default or the style applies)
 */
import type { ConfigItemKind } from './app-state';
import type { PropValue } from './config-edit';
import { LIST_MAX_ROWS } from './config-model';
import { ALIGN_OPTIONS } from './display-geometry';
import { FONTSIZE_MAX } from './font-config';

export type FieldType = 'text' | 'lines' | 'number' | 'font' | 'select' | 'color';

export interface Field {
  /** The key in config.json. */
  key: string;
  label: string;
  type: FieldType;
  /** number: whole numbers in this range. */
  min?: number;
  max?: number;
  /** select */
  options?: Array<{ value: number; label: string }>;
  /** color: written as "#rrggbb" (true) or "rrggbb" (lists, as docs/LISTS.md). */
  hash?: boolean;
  /** Shown under the field. */
  hint?: string;
  /** Two of these share a row. */
  half?: boolean;
  /** What applies when the key is absent (the placeholder). */
  unset?: string;
}

const SIZE_HINT = `6–${FONTSIZE_MAX} = px; 1–5 = 14, 24, 32, 48, 72 px`;

function font(key: string, label: string): Field {
  return { key, label, type: 'font', half: true };
}

function size(key: string, label: string): Field {
  return { key, label, type: 'number', min: 1, max: FONTSIZE_MAX, half: true, unset: 'style', hint: SIZE_HINT };
}

function percent(key: string, label: string, min = 0, unset?: string): Field {
  return { key, label, type: 'number', min, max: 100, half: true, ...(unset ? { unset } : {}) };
}

/** What kind of entry it is, for a title. */
export const KIND_LABEL: Record<ConfigItemKind, string> = {
  displayitem: 'Display item', list: 'List', image: 'Image', button: 'Key',
};

/** The fields of an entry of `kind`; `raw` = its own keys (a field can depend on them). */
export function itemFields(kind: ConfigItemKind, raw: Record<string, unknown> = {}): Field[] {
  switch (kind) {
    case 'displayitem':
      return [
        { key: 'label', label: 'Label', type: 'text' },
        { key: 'unit', label: 'Unit', type: 'text' },
        font('font', 'Font'), size('fontsize', 'Size'),
        { key: 'align', label: 'Align', type: 'select', options: ALIGN_OPTIONS, half: true },
        percent('width', 'Width %', 0, '100'),
        // x / y are offsets from the align corner: negative from the right or the bottom.
        { key: 'x', label: 'X %', type: 'number', min: -100, max: 100, half: true, unset: '0' },
        { key: 'y', label: 'Y %', type: 'number', min: -100, max: 100, half: true, unset: '0' },
        ...(typeof raw.icon === 'string' && raw.icon
          ? [{ key: 'iconcolor', label: 'Icon colour', type: 'color', hash: true } as Field] : []),
      ];
    case 'list':
      return [
        { key: 'items', label: 'Items (one per line)', type: 'lines' },
        font('font', 'Font'), size('fontsize', 'Size'),
        percent('width', 'Width %', 1, '50'),
        { key: 'rows', label: 'Rows', type: 'number', min: 1, max: LIST_MAX_ROWS, half: true, unset: '3' },
        percent('x', 'X %', 0, '0'), percent('y', 'Y %', 0, '0'),
        { key: 'color', label: 'Text colour', type: 'color', hash: false },
        { key: 'focuscolor', label: 'Selection bar', type: 'color', hash: false },
        { key: 'focustextcolor', label: 'Selected text', type: 'color', hash: false },
      ];
    case 'image':
      return [
        { key: 'width', label: 'Width px', type: 'number', min: 1, max: 320, half: true },
        { key: 'height', label: 'Height px', type: 'number', min: 1, max: 320, half: true },
        { key: 'x', label: 'X px', type: 'number', min: 0, max: 319, half: true, unset: '0' },
        { key: 'y', label: 'Y px', type: 'number', min: 0, max: 239, half: true, unset: '0' },
        { key: 'color', label: 'Colour (single-colour image)', type: 'color', hash: true },
      ];
    case 'button':
      return [
        { key: 'label', label: 'Label', type: 'text' },
        font('font', 'Font'), size('fontsize', 'Size'),
        { key: 'toplabel', label: 'Top label', type: 'text' },
        font('topfont', 'Top font'), size('topfontsize', 'Top size'),
      ];
  }
}

/**
 * The value to write for what was entered in a field. undefined = remove
 * the key. null = not a value for this field (nothing is written).
 */
export function fieldValue(field: Field, input: string): PropValue | null {
  switch (field.type) {
    case 'text':
      return input;
    case 'lines':
      return input.split(/\r?\n/).map(l => l.trim()).filter(l => l !== '');
    case 'font':
      return input === '' ? undefined : input;
    case 'select': {
      const n = Number(input);
      return field.options?.some(o => o.value === n) ? n : null;
    }
    case 'number': {
      if (input.trim() === '') return undefined;
      const n = Number(input);
      if (!Number.isFinite(n)) return null;
      return Math.min(field.max ?? Infinity, Math.max(field.min ?? -Infinity, Math.round(n)));
    }
    case 'color': {
      if (input === '') return undefined;
      const m = /^#?([0-9a-fA-F]{6})$/.exec(input);
      return m ? (field.hash ? '#' : '') + m[1].toLowerCase() : null;
    }
  }
}

/** What a field shows for the entry's own value ('' = the key is absent or not of the field's type). */
export function fieldText(field: Field, value: unknown): string {
  switch (field.type) {
    case 'lines':
      return Array.isArray(value) ? value.filter(v => typeof v === 'string').join('\n') : '';
    case 'color':
      return typeof value === 'string' && /^#?[0-9a-fA-F]{6}$/.test(value) ? '#' + value.replace(/^#/, '').toLowerCase() : '';
    case 'number': case 'select':
      return typeof value === 'number' || (typeof value === 'string' && value.trim() !== '' && Number.isFinite(Number(value)))
        ? String(Number(value)) : '';
    default:
      return typeof value === 'string' ? value : typeof value === 'number' ? String(value) : '';
  }
}
