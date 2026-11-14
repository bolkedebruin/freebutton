/**
 * The pill on a pane divider as that pane's vertical scrollbar.
 *
 * While the pane's content fits, the pill is the divider's drag grip: at mid
 * height. When the content is taller than the pane, the pill is the scroll
 * thumb as well: its place shows the scroll position and it can be dragged
 * up and down. Its length is always the same: it does not depend on the
 * content and does not change while the pane is scrolled.
 */
export const PILL_REST_PX = 64;

export interface PillGeometry {
  /** Length and top of the pill inside its track, in px. */
  len: number;
  top: number;
  /** The pane scrolls: the pill is its thumb. */
  scrolls: boolean;
}

/** `track`: height of the divider; `client` / `content`: visible and whole height of the pane. */
export function pillGeometry(track: number, client: number, content: number, scrollTop: number): PillGeometry {
  const rest = Math.min(PILL_REST_PX, Math.max(0, track));
  const max = content - client;
  if (!(track > 0) || !(client > 0) || !(max > 1)) {
    return { len: rest, top: Math.max(0, (track - rest) / 2), scrolls: false };
  }
  const at = Math.min(1, Math.max(0, scrollTop / max));
  return { len: rest, top: Math.round((track - rest) * at), scrolls: true };
}

/** New scrollTop of the pane after the pill was dragged `dy` px. */
export function scrollTopAfterDrag(track: number, client: number, content: number, scrollTop: number, dy: number): number {
  const max = content - client;
  const travel = track - Math.min(PILL_REST_PX, Math.max(0, track));
  if (!(max > 1) || !(client > 0) || travel <= 0) return scrollTop;
  return Math.min(max, Math.max(0, scrollTop + dy * max / travel));
}
