/**
 * Edits of config.json entries as minimal text changes (pure).
 *
 * The virtual device changes an entry (a drag, the properties popover) by
 * changing the text of exactly that entry in the editor: nothing else in
 * the file moves, so the indentation, the key order and the line ends stay
 * as the user wrote them. The text is never parsed to a value and written
 * out again. Like config-locate.ts this works on the JSON syntax tree.
 *
 *   setItemProps(doc, ref, props)  set (or with `undefined`: remove) direct
 *                                  properties of the entry `ref.list[ref.index]`
 *   removeItem(doc, ref)           take the entry out of its array
 *   addItem(doc, list, props)      append an entry, written like the last one
 *   readItem(doc, ref)             the entry's own values (to show them)
 *   listKeyFor(doc, kind)          the array the model reads for a kind
 *
 * `doc` is the editor state or the file's text. Every edit returns the
 * changes (positions in the given text, not overlapping, sorted) or a
 * refusal with the reason; a refusal changes nothing. Refused: text that is
 * not valid JSON, an entry that is not there or not an object, an array or
 * a key that the file has twice (which of the two the device reads is not
 * the IDE's to guess).
 *
 * Legacy files: the entry is addressed by the array's key as written
 * ("mqttdisplays", "mqttbuttons"), so those are edited in place like the
 * current names. A key that an older file spells differently and the
 * firmware still reads ("topLabel") is updated under the spelling the file
 * has.
 */
import { ensureSyntaxTree, syntaxTree } from '@codemirror/language';
import { jsonLanguage } from '@codemirror/lang-json';
import type { EditorState } from '@codemirror/state';
import type { SyntaxNode, Tree } from '@lezer/common';
import type { ConfigItemKind, ConfigRef } from './app-state';

export interface TextChange { from: number; to: number; insert: string }

export type EditResult =
  | { ok: true; changes: TextChange[]; /** addItem: where the new entry is. */ ref?: ConfigRef }
  | { ok: false; error: string };

/** A value an entry's property can be set to; `undefined` removes the key. */
export type PropValue = string | number | boolean | null | PropValue[] | undefined;

export type ItemRef = Pick<ConfigRef, 'list' | 'index'>;

const PARSE_TIMEOUT_MS = 200;
const VALUE_NODES = new Set(['Object', 'Array', 'String', 'Number', 'True', 'False', 'Null']);

/** The arrays the model reads per kind, in the order it looks for them (config-model.ts). */
const LIST_KEYS: Record<ConfigItemKind, string[]> = {
  button: ['buttons', 'mqttbuttons'],
  displayitem: ['displayitems', 'mqttdisplays'],
  list: ['lists'],
  image: ['images'],
};

/**
 * Where a new key goes: after the last key of the entry that comes before
 * it here (the order of the configs the firmware writes and the examples
 * use); a key that is not listed, or with none before it, goes last.
 */
const KEY_ORDER = [
  'displayitemid', 'name', 'position', 'page', 'x', 'y', 'width', 'height', 'rows', 'align', 'boxtype',
  'fontsize', 'font', 'labelfontsize', 'labelfont', 'label', 'toplabel', 'topfontsize', 'topfont', 'unit',
  'icon', 'src', 'iconsize', 'iconcolor', 'color', 'focuscolor', 'focustextcolor', 'items', 'selected',
];

/** Older spellings the firmware still reads: the key the file has is the one updated. */
const KEY_ALIASES: Record<string, string[]> = { toplabel: ['topLabel'] };

const INVALID = 'config.json is not valid JSON';

interface Doc {
  text: string;
  top: SyntaxNode;
  /** The file's line end. */
  eol: string;
}

function hasError(tree: Tree): boolean {
  let bad = false;
  tree.iterate({ enter: (n) => { if (n.type.isError) bad = true; return !bad; } });
  return bad;
}

function docOf(input: EditorState | string): Doc | null {
  let tree: Tree;
  let text: string;
  if (typeof input === 'string') {
    text = input;
    tree = jsonLanguage.parser.parse(text);
  } else {
    text = input.doc.toString();
    tree = ensureSyntaxTree(input, input.doc.length, PARSE_TIMEOUT_MS) ?? syntaxTree(input);
    // A tree that stops before the end (parse budget): nothing to edit safely.
    if (tree.length < input.doc.length) return null;
  }
  if (hasError(tree)) return null;
  return { text, top: tree.topNode, eol: text.includes('\r\n') ? '\r\n' : '\n' };
}

function children(node: SyntaxNode, names: Set<string> | string): SyntaxNode[] {
  const out: SyntaxNode[] = [];
  for (let n = node.firstChild; n; n = n.nextSibling) {
    if (typeof names === 'string' ? n.name === names : names.has(n.name)) out.push(n);
  }
  return out;
}

interface Prop {
  node: SyntaxNode;
  name: string;
  value: SyntaxNode;
}

function propsOf(doc: Doc, object: SyntaxNode): Prop[] {
  const out: Prop[] = [];
  for (const node of children(object, 'Property')) {
    const key = node.getChild('PropertyName');
    const value = children(node, VALUE_NODES)[0];
    if (!key || !value) continue;
    const raw = doc.text.slice(key.from, key.to);
    let name = raw.slice(1, -1);
    try { name = JSON.parse(raw); } catch { /* the raw text */ }
    out.push({ node, name, value });
  }
  return out;
}

function rootObject(doc: Doc): SyntaxNode | null {
  return doc.top.getChild('Object');
}

type Found<T> = { ok: true; value: T } | { ok: false; error: string };

/** The root array `list`; null when the file has none. */
function findList(doc: Doc, list: string): Found<{ prop: Prop; array: SyntaxNode } | null> {
  const root = rootObject(doc);
  if (!root) return { ok: false, error: 'config.json is not a JSON object' };
  const found = propsOf(doc, root).filter(p => p.name === list);
  if (found.length > 1) return { ok: false, error: `config.json has "${list}" twice: remove one of them first` };
  if (found.length === 0) return { ok: true, value: null };
  if (found[0].value.name !== 'Array') return { ok: false, error: `"${list}" in config.json is not an array` };
  return { ok: true, value: { prop: found[0], array: found[0].value } };
}

function findItem(doc: Doc, ref: ItemRef): Found<{ array: SyntaxNode; entries: SyntaxNode[]; entry: SyntaxNode }> {
  const list = findList(doc, ref.list);
  if (!list.ok) return list;
  const entries = list.value ? children(list.value.array, VALUE_NODES) : [];
  const entry = entries[ref.index];
  if (!list.value || !entry) return { ok: false, error: `${ref.list}[${ref.index}] is not in config.json (any more)` };
  if (entry.name !== 'Object') return { ok: false, error: `${ref.list}[${ref.index}] is not an object` };
  return { ok: true, value: { array: list.value.array, entries, entry } };
}

/** Text of a value; null for one that cannot be written (NaN, an object). */
function encode(value: PropValue): string | null {
  if (value === null) return 'null';
  if (typeof value === 'string') return JSON.stringify(value);
  if (typeof value === 'number') return Number.isFinite(value) ? String(value) : null;
  if (typeof value === 'boolean') return String(value);
  if (Array.isArray(value)) {
    const parts = value.map(encode);
    return parts.some(p => p === null) ? null : `[${parts.join(', ')}]`;
  }
  return null;
}

/** The white space a line starts with, when `pos` is the first thing on its line; else null. */
function lineIndent(doc: Doc, pos: number): string | null {
  let start = pos;
  while (start > 0 && doc.text[start - 1] !== '\n' && doc.text[start - 1] !== '\r') start--;
  const lead = doc.text.slice(start, pos);
  return /^[ \t]*$/.test(lead) ? lead : null;
}

/** The text between a key and its value (": "), as this property writes it. */
function colonOf(doc: Doc, p: Prop | undefined): string {
  const key = p?.node.getChild('PropertyName');
  const text = key && p ? doc.text.slice(key.to, p.value.from) : '';
  return /^\s*:\s*$/.test(text) ? text : ': ';
}

/** One indent step of the file: what the root's first key is indented by. */
function indentUnit(doc: Doc): string {
  const root = rootObject(doc);
  const first = root ? propsOf(doc, root)[0] : undefined;
  return (first && lineIndent(doc, first.node.from)) || '  ';
}

/** How an object is written, read off an existing one. */
interface ObjectStyle {
  /** Indent of its keys when each is on its own line; null = on one line. */
  propIndent: string | null;
  /** One line: the text after "{" and before "}", and between two properties. */
  pad: string;
  sep: string;
  colon: string;
}

function styleOf(doc: Doc, object: SyntaxNode): ObjectStyle | null {
  const props = propsOf(doc, object);
  if (props.length === 0) return null;
  const propIndent = lineIndent(doc, props[0].node.from);
  const pad = doc.text.slice(object.from + 1, props[0].node.from);
  const sep = props.length > 1 ? doc.text.slice(props[0].node.to, props[1].node.from) : ', ';
  return {
    propIndent,
    pad: /^[ \t]*$/.test(pad) ? pad : '',
    sep: /^[ \t]*,[ \t]*$/.test(sep) ? sep : ', ',
    colon: colonOf(doc, props[0]),
  };
}

function formatObject(doc: Doc, props: Array<[string, string]>, style: ObjectStyle, closeIndent: string): string {
  const parts = props.map(([k, v]) => `${JSON.stringify(k)}${style.colon}${v}`);
  if (style.propIndent === null) return `{${style.pad}${parts.join(style.sep)}${style.pad}}`;
  return `{${doc.eol}${parts.map(p => style.propIndent + p).join(',' + doc.eol)}${doc.eol}${closeIndent}}`;
}

/** Sorted, and checked: two changes may touch but not overlap. */
function finish(changes: TextChange[]): EditResult {
  const sorted = changes
    .map((c, i) => ({ c, i }))
    .sort((a, b) => a.c.from - b.c.from || (a.c.to - a.c.from) - (b.c.to - b.c.from) || a.i - b.i)
    .map(e => e.c);
  for (let i = 1; i < sorted.length; i++) {
    if (sorted[i].from < sorted[i - 1].to) return { ok: false, error: 'This change cannot be made in one step' };
  }
  return { ok: true, changes: sorted };
}

/**
 * Set direct properties of the entry `ref`. A key that is there gets the new
 * value in place; one that is absent is added in the entry's style (own
 * line and indentation, or after a comma on the entry's line); `undefined`
 * removes the key. Values that are already what the text says change nothing.
 */
export function setItemProps(input: EditorState | string, ref: ItemRef, props: Record<string, PropValue>): EditResult {
  const doc = docOf(input);
  if (!doc) return { ok: false, error: INVALID };
  const found = findItem(doc, ref);
  if (!found.ok) return found;
  const object = found.value.entry;
  const existing = propsOf(doc, object);
  const changes: TextChange[] = [];
  const removed = new Set<Prop>();
  const added: Array<[string, string]> = [];

  for (const [key, value] of Object.entries(props)) {
    let matches = existing.filter(p => p.name === key);
    if (matches.length === 0) {
      matches = existing.filter(p => (KEY_ALIASES[key] ?? []).includes(p.name));
    }
    if (matches.length > 1) {
      return { ok: false, error: `${ref.list}[${ref.index}] has "${matches[0].name}" twice: remove one of them first` };
    }
    const prop = matches[0];
    if (value === undefined) {
      if (prop) removed.add(prop);
      continue;
    }
    const text = encode(value);
    if (text === null) return { ok: false, error: `"${key}" cannot be set to that value` };
    if (!prop) {
      added.push([key, text]);
    } else if (doc.text.slice(prop.value.from, prop.value.to) !== text) {
      changes.push({ from: prop.value.from, to: prop.value.to, insert: text });
    }
  }

  // Removals: with the comma before the key; at the start of the entry, with the one after it.
  let leading = true;
  existing.forEach((p, i) => {
    if (!removed.has(p)) { leading = false; return; }
    if (leading) {
      const next = existing[i + 1];
      changes.push({ from: p.node.from, to: next ? next.node.from : p.node.to, insert: '' });
    } else {
      changes.push({ from: existing[i - 1].node.to, to: p.node.to, insert: '' });
    }
  });

  const kept = existing.filter(p => !removed.has(p));
  if (added.length > 0 && kept.length === 0) {
    const colon = colonOf(doc, existing[0]);
    changes.push({
      from: object.from + 1, to: object.from + 1,
      insert: added.map(([k, v]) => `${JSON.stringify(k)}${colon}${v}`).join(', '),
    });
  } else if (added.length > 0) {
    const style = styleOf(doc, object)!;
    for (const [key, text] of added) {
      const order = KEY_ORDER.indexOf(key);
      const before = order < 0 ? [] : kept.filter((p) => {
        const o = KEY_ORDER.indexOf(p.name);
        return o >= 0 && o < order;
      });
      const anchor = before.length > 0 ? before[before.length - 1] : kept[kept.length - 1];
      const lead = style.propIndent === null
        ? style.sep
        : `,${doc.eol}${lineIndent(doc, anchor.node.from) ?? style.propIndent}`;
      changes.push({
        from: anchor.node.to, to: anchor.node.to,
        insert: `${lead}${JSON.stringify(key)}${colonOf(doc, anchor)}${text}`,
      });
    }
  }
  return finish(changes);
}

/** Take the entry `ref` out of its array, with its comma. */
export function removeItem(input: EditorState | string, ref: ItemRef): EditResult {
  const doc = docOf(input);
  if (!doc) return { ok: false, error: INVALID };
  const list = findList(doc, ref.list);
  if (!list.ok) return list;
  const entries = list.value ? children(list.value.array, VALUE_NODES) : [];
  const entry = entries[ref.index];
  if (!list.value || !entry) return { ok: false, error: `${ref.list}[${ref.index}] is not in config.json (any more)` };
  if (entries.length === 1) {
    return finish([{ from: list.value.array.from + 1, to: list.value.array.to - 1, insert: '' }]);
  }
  return finish([ref.index > 0
    ? { from: entries[ref.index - 1].to, to: entry.to, insert: '' }
    : { from: entry.from, to: entries[1].from, insert: '' }]);
}

/**
 * Append an entry with `props` (in that order) to the root array `list`,
 * written like the array's last entry (on one line or a key per line, its
 * indentation). An empty or absent array is written a key per line with the
 * file's indent; an absent one is added as the root's last key.
 */
export function addItem(input: EditorState | string, list: string, props: Record<string, PropValue>): EditResult {
  const doc = docOf(input);
  if (!doc) return { ok: false, error: INVALID };
  const root = rootObject(doc);
  if (!root) return { ok: false, error: 'config.json is not a JSON object' };
  const found = findList(doc, list);
  if (!found.ok) return found;
  const encoded: Array<[string, string]> = [];
  for (const [key, value] of Object.entries(props)) {
    const text = value === undefined ? undefined : encode(value);
    if (text === null) return { ok: false, error: `"${key}" cannot be set to that value` };
    if (text !== undefined) encoded.push([key, text]);
  }
  const kind = (Object.keys(LIST_KEYS) as ConfigItemKind[]).find(k => LIST_KEYS[k].includes(list)) ?? 'displayitem';
  const unit = indentUnit(doc);
  const entries = found.value ? children(found.value.array, VALUE_NODES) : [];
  const ref: ConfigRef = { kind, list, index: entries.length };
  /** A key per line inside an array whose own line is indented by `arrayIndent`. */
  const block = (arrayIndent: string) => {
    const style: ObjectStyle = { propIndent: arrayIndent + unit + unit, pad: '', sep: ', ', colon: ': ' };
    return `${doc.eol}${arrayIndent}${unit}${formatObject(doc, encoded, style, arrayIndent + unit)}${doc.eol}${arrayIndent}`;
  };

  if (found.value && entries.length > 0) {
    const last = entries[entries.length - 1];
    const indent = lineIndent(doc, last.from);
    const like = last.name === 'Object' ? styleOf(doc, last) : null;
    const style: ObjectStyle = like
      ?? { propIndent: indent === null ? null : indent + unit, pad: '', sep: ', ', colon: ': ' };
    // An entry that starts in the middle of a line is followed on that line: no key per line there.
    if (indent === null) style.propIndent = null;
    const text = formatObject(doc, encoded, style, indent ?? '');
    const lead = indent === null ? ', ' : `,${doc.eol}${indent}`;
    return { ...finish([{ from: last.to, to: last.to, insert: lead + text }]), ref } as EditResult;
  }
  if (found.value) {
    const array = found.value.array;
    const arrayIndent = lineIndent(doc, found.value.prop.node.from) ?? unit;
    return { ...finish([{ from: array.from + 1, to: array.to - 1, insert: block(arrayIndent) }]), ref } as EditResult;
  }
  const rootProps = propsOf(doc, root);
  const lastProp = rootProps[rootProps.length - 1];
  const rootIndent = (lastProp && lineIndent(doc, lastProp.node.from)) ?? unit;
  const text = `${JSON.stringify(list)}: [${block(rootIndent)}]`;
  const change: TextChange = lastProp
    ? { from: lastProp.node.to, to: lastProp.node.to, insert: `,${doc.eol}${rootIndent}${text}` }
    : { from: root.from + 1, to: root.to - 1, insert: `${doc.eol}${rootIndent}${text}${doc.eol}` };
  return { ...finish([change]), ref } as EditResult;
}

/** The entry's own keys and values; null when it is not there or the text is not valid JSON. */
export function readItem(input: EditorState | string, ref: ItemRef): Record<string, unknown> | null {
  const doc = docOf(input);
  if (!doc) return null;
  const found = findItem(doc, ref);
  if (!found.ok) return null;
  try {
    const value: unknown = JSON.parse(doc.text.slice(found.value.entry.from, found.value.entry.to));
    return typeof value === 'object' && value !== null ? value as Record<string, unknown> : null;
  } catch {
    return null;
  }
}

/** The text range of the entry `ref` (to select it after an edit); null when it is not there. */
export function itemRange(input: EditorState | string, ref: ItemRef): { from: number; to: number } | null {
  const doc = docOf(input);
  const found = doc ? findItem(doc, ref) : null;
  return found?.ok ? { from: found.value.entry.from, to: found.value.entry.to } : null;
}

/**
 * The root array entries of `kind` go to: the one the model reads (the
 * current name, else the legacy one when the file has only that), or the
 * current name for a file that has neither.
 */
export function listKeyFor(input: EditorState | string, kind: ConfigItemKind): string {
  const doc = docOf(input);
  const root = doc ? rootObject(doc) : null;
  const names = root && doc ? propsOf(doc, root).filter(p => p.value.name === 'Array').map(p => p.name) : [];
  return LIST_KEYS[kind].find(k => names.includes(k)) ?? LIST_KEYS[kind][0];
}

/** `text` with `changes` (as an edit returns them) applied. */
export function applyChanges(text: string, changes: TextChange[]): string {
  let out = '';
  let at = 0;
  for (const c of changes) {
    out += text.slice(at, c.from) + c.insert;
    at = c.to;
  }
  return out + text.slice(at);
}
