/**
 * What the device says about its user script after a deploy.
 *
 * Firmware with the script restart answers an upload of the boot script with
 * "restart": true and counts the script's starts in /api/device
 * ("scriptStarts", with "scriptError": "" when the last start ran without
 * exception). Older firmware has neither: the new script runs after a reboot.
 */
export interface ScriptStatus {
  starts: number;
  error: string;
}

export type DeployOutcome =
  | { kind: 'running' }
  | { kind: 'failed'; error: string }
  /** The device did not report a new start in time. */
  | { kind: 'unknown' };

/** The script fields of an /api/device document, or null without them (older firmware). */
export function scriptStatusOf(doc: unknown): ScriptStatus | null {
  if (!doc || typeof doc !== 'object') return null;
  const d = doc as Record<string, unknown>;
  if (typeof d.scriptStarts !== 'number') return null;
  return { starts: d.scriptStarts, error: typeof d.scriptError === 'string' ? d.scriptError : '' };
}

/**
 * Waits for the restart a deploy asked for. A start after `before` decides
 * it: without error the script is running. A bytecode that cannot be loaded
 * is never started, so without a new start the last status with an error
 * decides after the wait.
 */
export async function waitForScriptStart(
  read: () => Promise<ScriptStatus | null>,
  before: ScriptStatus | null,
  opts: { tries?: number; delayMs?: number; sleep?: (ms: number) => Promise<void> } = {},
): Promise<DeployOutcome> {
  const tries = opts.tries ?? 10;
  const delayMs = opts.delayMs ?? 500;
  const sleep = opts.sleep ?? ((ms: number) => new Promise<void>(r => setTimeout(r, ms)));
  let last: ScriptStatus | null = null;
  for (let i = 0; i < tries; i++) {
    await sleep(delayMs);
    try {
      last = await read();
    } catch {
      continue;   // the device is busy restarting: ask again
    }
    if (last && before && last.starts > before.starts) {
      return last.error ? { kind: 'failed', error: last.error } : { kind: 'running' };
    }
  }
  if (last && last.error && (!before || last.error !== before.error || last.starts === before.starts)) {
    return { kind: 'failed', error: last.error };
  }
  return { kind: 'unknown' };
}
