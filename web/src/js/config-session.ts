/**
 * Edits of config.json made on the virtual device, applied to the editor.
 *
 * config.json has to be the open file: the edit is a text change in the
 * editor (config-edit.ts), so the user sees it, can undo it, and Save and
 * Deploy work as after typing. When another file is open, config.json is
 * opened first; the workspace asks before it drops unsaved changes of that
 * file, as for a click on a device item.
 */
import type { EditorState } from '@codemirror/state';
import { emit, type ConfigRef } from './app-state';
import { itemRange, readItem, type EditResult, type ItemRef } from './config-edit';
import { applyConfigChanges, configState } from './editor';
import { CONFIG_KEY } from './fs-rules';
import { workspace } from './workspace';

export type EditOutcome = { ok: true; ref?: ConfigRef } | { ok: false; error: string };

const NOT_OPEN = 'config.json is not open in the editor';

/** Make config.json the open file and show the editor. False when it could not be opened (or the user kept the other file). */
export async function ensureConfigOpen(): Promise<boolean> {
  try {
    if (workspace.snapshot().currentKey !== CONFIG_KEY && !(await workspace.open(CONFIG_KEY))) return false;
  } catch {
    return false;
  }
  emit('main-view', { view: 'editor' });
  return configState() !== null;
}

/** The entry's own keys and values in the open config.json; null when it cannot be read. */
export function readConfigItem(ref: ItemRef): Record<string, unknown> | null {
  const state = configState();
  return state ? readItem(state, ref) : null;
}

/**
 * Make one edit: `make` gives the changes for the editor's text. One undo
 * step; the virtual device shows the result at once. `reveal`: the entry
 * the cursor goes to when the edit changes no text.
 */
export function applyConfigEdit(
  make: (state: EditorState) => EditResult, opts: { reveal?: ItemRef; userEvent?: string } = {},
): EditOutcome {
  const state = configState();
  if (!state) return { ok: false, error: NOT_OPEN };
  const result = make(state);
  if (!result.ok) return result;
  const at = opts.reveal ? itemRange(state, opts.reveal)?.from : undefined;
  if (!applyConfigChanges(result.changes, { at: at === undefined ? undefined : at + 1, userEvent: opts.userEvent })) {
    return { ok: false, error: NOT_OPEN };
  }
  workspace.flushConfigPreview();
  return { ok: true, ...(result.ref ? { ref: result.ref } : {}) };
}
