/**
 * The emulator's time, injectable so tests are deterministic.
 *
 * Everything that waits goes through one Clock: the script's setTimeout and
 * Date.now(), the key scan (press / long press / repeat), the loop() passes
 * that deliver MQTT messages, the sensor samples. In the page that is
 * RealClock (setTimeout to the next due time, never a poll); in tests
 * ManualClock, which only moves when advance() is called.
 */
export type TimerHandle = number;

export interface Clock {
  /** Milliseconds since the Unix epoch (what the script's Date.now() gives). */
  now(): number;
  /** Calls fn once, delayMs from now. */
  setTimer(fn: () => void, delayMs: number): TimerHandle;
  clearTimer(handle: TimerHandle): void;
}

export class RealClock implements Clock {
  now(): number {
    return Date.now();
  }

  setTimer(fn: () => void, delayMs: number): TimerHandle {
    return setTimeout(fn, Math.max(0, delayMs)) as unknown as number;
  }

  clearTimer(handle: TimerHandle): void {
    clearTimeout(handle);
  }
}

interface Pending {
  handle: TimerHandle;
  due: number;
  seq: number;
  fn: () => void;
}

/** A clock that stands still until advance(). Timers run in due order. */
export class ManualClock implements Clock {
  private time: number;
  private seq = 0;
  private pending: Pending[] = [];

  /** Default start: 2026-01-01T12:00:00Z. */
  constructor(start: number = Date.UTC(2026, 0, 1, 12, 0, 0)) {
    this.time = start;
  }

  now(): number {
    return this.time;
  }

  setTimer(fn: () => void, delayMs: number): TimerHandle {
    const seq = ++this.seq;
    this.pending.push({ handle: seq, due: this.time + Math.max(0, delayMs), seq, fn });
    return seq;
  }

  clearTimer(handle: TimerHandle): void {
    this.pending = this.pending.filter(p => p.handle !== handle);
  }

  /** Timers waiting (for tests: "nothing is polling"). */
  get timers(): number {
    return this.pending.length;
  }

  /**
   * Moves the time forward by ms and runs every timer that comes due, in
   * order, including the ones those timers set. advance(0) runs what is due
   * now.
   */
  advance(ms: number): void {
    const target = this.time + Math.max(0, ms);
    // A timer that keeps re-arming itself with delay 0 would never end.
    let budget = 1_000_000;
    for (;;) {
      let next: Pending | undefined;
      for (const p of this.pending) {
        if (p.due <= target && (!next || p.due < next.due || (p.due === next.due && p.seq < next.seq))) next = p;
      }
      if (!next) break;
      if (--budget < 0) throw new Error('ManualClock.advance: timers never settle');
      this.pending = this.pending.filter(p => p !== next);
      if (next.due > this.time) this.time = next.due;
      next.fn();
    }
    this.time = target;
  }
}
