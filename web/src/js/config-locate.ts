/**
 * config.json text <-> the entries the virtual device draws.
 *
 * Works on the editor's JSON syntax tree (no second parse, and it still
 * answers while the text is not valid JSON mid-edit):
 *   itemAt(state, pos)   the top-level array entry the position lies in
 *   rangeOf(state, ref)  the text range of an entry
 * An entry is addressed as a ConfigRef: the array's key as written
 * ("buttons", "mqttdisplays", "lists", "images") and its index, which is
 * what configToModel puts on every mapped item (`ref`).
 */
import { ensureSyntaxTree, syntaxTree } from '@codemirror/language';
import type { EditorState } from '@codemirror/state';
import type { SyntaxNode } from '@lezer/common';
import type { ConfigRef } from './app-state';
import { configListKind } from './config-model';

/** Parse budget for a whole config.json (<= 64 KiB: well within it). */
const PARSE_TIMEOUT_MS = 200;

function topNode(state: EditorState): SyntaxNode {
  return (ensureSyntaxTree(state, state.doc.length, PARSE_TIMEOUT_MS) ?? syntaxTree(state)).topNode;
}

const VALUE_NODES = new Set(['Object', 'Array', 'String', 'Number', 'True', 'False', 'Null']);

/** The entries of an Array node (brackets, commas and error nodes are not entries). */
function elements(array: SyntaxNode): SyntaxNode[] {
  const out: SyntaxNode[] = [];
  for (let n = array.firstChild; n; n = n.nextSibling) {
    if (VALUE_NODES.has(n.name)) out.push(n);
  }
  return out;
}

/** A Property node's key without the quotes. */
function propertyName(state: EditorState, property: SyntaxNode): string | null {
  const name = property.getChild('PropertyName');
  if (!name) return null;
  const text = state.sliceDoc(name.from, name.to);
  return text.length >= 2 ? text.slice(1, -1) : null;
}

/** Whether `property` is a member of the document's root object. */
function isRootProperty(property: SyntaxNode): boolean {
  const object = property.parent;
  return object?.name === 'Object' && object.parent?.name === 'JsonText';
}

function itemAtSide(state: EditorState, pos: number, side: -1 | 1): ConfigRef | null {
  let node: SyntaxNode | null = topNode(state).resolveInner(pos, side);
  for (; node; node = node.parent) {
    const array = node.parent;
    const property = array?.parent;
    if (array?.name !== 'Array' || property?.name !== 'Property' || !isRootProperty(property)) continue;
    const list = propertyName(state, property);
    const kind = list ? configListKind(list) : null;
    if (!list || !kind) return null;
    const index = elements(array).findIndex(e => e.from === node!.from && e.to === node!.to);
    return index < 0 ? null : { kind, list, index };
  }
  return null;
}

/**
 * The drawn config entry (key, display item, list, image) that contains
 * `pos`; both ends of the entry count as inside it.
 */
export function itemAt(state: EditorState, pos: number): ConfigRef | null {
  const p = Math.max(0, Math.min(pos, state.doc.length));
  return itemAtSide(state, p, -1) ?? itemAtSide(state, p, 1);
}

/** The text range of the entry `ref` points at; null when it is not there. */
export function rangeOf(state: EditorState, ref: Pick<ConfigRef, 'list' | 'index'>): { from: number; to: number } | null {
  const root = topNode(state).getChild('Object');
  if (!root) return null;
  for (const property of root.getChildren('Property')) {
    if (propertyName(state, property) !== ref.list) continue;
    const array = property.getChild('Array');
    const entry = array ? elements(array)[ref.index] : undefined;
    if (entry) return { from: entry.from, to: entry.to };
  }
  return null;
}
