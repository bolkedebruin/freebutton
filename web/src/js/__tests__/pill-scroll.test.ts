import { describe, it, expect } from 'vitest';
import { pillGeometry, scrollTopAfterDrag, PILL_REST_PX } from '../pill-scroll';

describe('pillGeometry', () => {
  it('rests at mid height while the content fits', () => {
    expect(pillGeometry(800, 700, 700, 0)).toEqual({ len: PILL_REST_PX, top: (800 - PILL_REST_PX) / 2, scrolls: false });
    expect(pillGeometry(800, 700, 650, 0)).toEqual({ len: PILL_REST_PX, top: (800 - PILL_REST_PX) / 2, scrolls: false });
  });
  it('keeps its length whatever the content, and shows the scroll position', () => {
    expect(pillGeometry(800, 700, 1400, 0)).toEqual({ len: PILL_REST_PX, top: 0, scrolls: true });
    expect(pillGeometry(800, 700, 1400, 700)).toEqual({ len: PILL_REST_PX, top: 800 - PILL_REST_PX, scrolls: true });
    expect(pillGeometry(800, 700, 1400, 350).top).toBe((800 - PILL_REST_PX) / 2);
    expect(pillGeometry(800, 700, 70000, 0).len).toBe(PILL_REST_PX);
  });
  it('copes with a pane that is not laid out', () => {
    expect(pillGeometry(0, 0, 0, 0)).toEqual({ len: 0, top: 0, scrolls: false });
  });
});

describe('scrollTopAfterDrag', () => {
  it('moves the content by the share of the track the pill was moved', () => {
    // track 800, pill 64: 736 px of travel for 736 px of scroll.
    expect(scrollTopAfterDrag(800, 700, 1436, 0, 200)).toBe(200);
    expect(scrollTopAfterDrag(800, 700, 2172, 400, -100)).toBe(200);
  });
  it('stops at both ends', () => {
    expect(scrollTopAfterDrag(800, 700, 1400, 600, 900)).toBe(700);
    expect(scrollTopAfterDrag(800, 700, 1400, 100, -900)).toBe(0);
  });
  it('does nothing when the pane does not scroll', () => {
    expect(scrollTopAfterDrag(800, 700, 700, 0, 50)).toBe(0);
  });
});
