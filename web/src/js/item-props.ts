/**
 * The properties of a config.json entry that the virtual device's popover
 * edits, per kind of entry (pure). Only keys the firmware reads
 * (DisplayItem.h, List.cpp, ImageItem.cpp, Clock.h, Button.h `configure`), with the
 * ranges it accepts. Fonts, sizes and colours are not properties of an
 * entry: an entry lists styles ("style": "big alarm"), which config.json
 * "styles" defines.
 *
 *   itemFields(kind, styles)    the fields, in the order shown
 *   fieldValue(field, input)    what the user entered -> the value written
 *                               (undefined: the key is removed and the
 *                               firmware's default applies)
 */
import type { ConfigItemKind } from './app-state';
import type { PropValue } from './config-edit';
import { LIST_MAX_ROWS, NAME_PATTERN } from './config-model';
import { ALIGN_OPTIONS } from './display-geometry';
import { STYLE_ITEM_MAX, styleListNames } from './style-config';

export type FieldType = 'text' | 'lines' | 'number' | 'styles' | 'select' | 'color';

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
  /** styles: the styles of config.json an entry can name (not "button", "displayitem", "list"), in definition order. */
  styles?: string[];
  /** Shown under the field. */
  hint?: string;
  /** Two of these share a row. */
  half?: boolean;
  /** text: what the value must match; anything else is not written. */
  pattern?: RegExp;
  /** text: the key cannot be removed (an empty field is not written). */
  required?: boolean;
  /** What applies when the key is absent (the placeholder). */
  unset?: string;
}

/** The entry's styles: up to four names of config.json "styles"; the last wins. */
function styleField(styles: readonly string[], kind: string): Field {
  return {
    key: 'style', label: 'Styles', type: 'styles', styles: [...styles], unset: `the look of "${kind}"`,
    hint: styles.length
      ? `Up to ${STYLE_ITEM_MAX} styles; where two set the same property the last one wins`
      : 'config.json "styles" defines no style an entry can name',
  };
}

function percent(key: string, label: string, min = 0, unset?: string): Field {
  return { key, label, type: 'number', min, max: 100, half: true, ...(unset ? { unset } : {}) };
}

/**
 * The entry's name: what a script calls it by (docs/NAMES.md). A list or an
 * image without one is not made by the device, so theirs cannot be emptied.
 */
function nameField(required: boolean): Field {
  return {
    key: 'name', label: 'Name', type: 'text', pattern: NAME_PATTERN, required,
    hint: 'A letter or _ first, then letters, digits and _, 24 at most. A script that uses the old name has to be changed too.',
  };
}

/** What kind of entry it is, for a title. */
export const KIND_LABEL: Record<ConfigItemKind, string> = {
  displayitem: 'Display item', list: 'List', image: 'Image', button: 'Button', clock: 'Clock',
};

/** The fields of an entry of `kind`; `styles` = the styles of the config an entry can name. */
export function itemFields(kind: ConfigItemKind, styles: readonly string[] = []): Field[] {
  switch (kind) {
    case 'displayitem':
      return [
        nameField(false),
        { key: 'label', label: 'Label', type: 'text' },
        { key: 'unit', label: 'Unit', type: 'text' },
        styleField(styles, 'displayitem'),
        { key: 'align', label: 'Align', type: 'select', options: ALIGN_OPTIONS, half: true },
        percent('width', 'Width %', 0, '100'),
        // x / y are offsets from the align corner: negative from the right or the bottom.
        { key: 'x', label: 'X %', type: 'number', min: -100, max: 100, half: true, unset: '0' },
        { key: 'y', label: 'Y %', type: 'number', min: -100, max: 100, half: true, unset: '0' },
      ];
    case 'list':
      return [
        nameField(true),
        { key: 'items', label: 'Items (one per line)', type: 'lines' },
        styleField(styles, 'list'),
        percent('width', 'Width %', 1, '50'),
        { key: 'rows', label: 'Rows', type: 'number', min: 1, max: LIST_MAX_ROWS, half: true, unset: '3' },
        percent('x', 'X %', 0, '0'), percent('y', 'Y %', 0, '0'),
      ];
    case 'image':
      return [
        nameField(true),
        { key: 'width', label: 'Width px', type: 'number', min: 1, max: 320, half: true },
        { key: 'height', label: 'Height px', type: 'number', min: 1, max: 320, half: true },
        { key: 'x', label: 'X px', type: 'number', min: 0, max: 319, half: true, unset: '0' },
        { key: 'y', label: 'Y px', type: 'number', min: 0, max: 239, half: true, unset: '0' },
        { key: 'color', label: 'Colour (single-colour image)', type: 'color' },
      ];
    case 'clock': {
      // Clock.h: pixels from the top left corner; "align": 9 centres it instead.
      const hint = 'With "align": 9 the clock is centred and x and y are not used';
      return [
        { key: 'width', label: 'Width px', type: 'number', min: 1, max: 320, half: true, unset: '260' },
        { key: 'height', label: 'Height px', type: 'number', min: 1, max: 320, half: true, unset: '260' },
        { key: 'x', label: 'X px', type: 'number', min: 0, max: 319, half: true, unset: '0', hint },
        { key: 'y', label: 'Y px', type: 'number', min: 0, max: 239, half: true, unset: '0' },
      ];
    }
    case 'button':
      return [
        nameField(false),
        { key: 'label', label: 'Label', type: 'text' },
        { key: 'toplabel', label: 'Top label', type: 'text' },
        styleField(styles, 'button'),
      ];
  }
}

/**
 * The names of a styles field as the device counts them (StyleRef.h parse,
 * style_logic_list_add): a name given twice counts once, at its last place.
 * "a a a a" is one style.
 */
export function styleFieldNames(text: string): string[] {
  let list: string[] = [];
  for (const name of styleListNames(text)) list = [...list.filter(n => n !== name), name];
  return list;
}

/**
 * The style list after a press on `name` in a styles field: a name the
 * list has is taken out; another is added as the last (so it wins). null
 * when that would be more names than an entry can have. The names are
 * counted as the device counts them (styleFieldNames).
 */
export function toggleStyle(current: string, name: string): string | null {
  const list = styleFieldNames(current);
  if (list.includes(name)) return list.filter(n => n !== name).join(' ');
  return list.length >= STYLE_ITEM_MAX ? null : [...list, name].join(' ');
}

/**
 * The value to write for what was entered in a field. undefined = remove
 * the key. null = not a value for this field (nothing is written).
 */
export function fieldValue(field: Field, input: string): PropValue | null {
  switch (field.type) {
    case 'text':
      if (!field.pattern) return input;
      if (input === '') return field.required ? null : undefined;
      return field.pattern.test(input) ? input : null;
    case 'lines':
      return input.split(/\r?\n/).map(l => l.trim()).filter(l => l !== '');
    case 'styles': {
      // The names, joined by one space (as the device writes them); none: the key goes.
      const names = styleListNames(input.trim());
      return names.length ? names.join(' ') : undefined;
    }
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
      return m ? '#' + m[1].toLowerCase() : null;
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
    case 'styles':
      // Only a string is a style list: the device ignores anything else (fieldProblem says so).
      return typeof value === 'string' ? value : '';
    default:
      return typeof value === 'string' ? value : typeof value === 'number' ? String(value) : '';
  }
}

/**
 * What is wrong with the entry's own value for a field, in the device's
 * words; null when nothing is (or the field has no such check). A "style"
 * that is no string is ignored by the device, and shown as empty here.
 */
export function fieldProblem(field: Field, value: unknown): string | null {
  if (field.type === 'styles' && value !== undefined && value !== null && typeof value !== 'string') {
    return `"${field.key}" is not a string of style names, ignored (it is ${JSON.stringify(value)})`;
  }
  return null;
}
