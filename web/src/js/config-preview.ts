/**
 * Live preview of config.json while it is edited (unsaved).
 *
 * The workspace calls schedule() on every user edit of config.json; after a
 * short pause the editor text is parsed with parseConfigText and shown on the
 * virtual device (device-config setPreviewConfig). Text that is not valid
 * JSON (mid-edit) keeps the last valid preview, flagged stale. reset() drops
 * the draft and shows the stored config again (saved, discarded, switched
 * away). The draft is never written anywhere.
 */
import { parseConfigText } from './config-model';
import { clearPreviewConfig, markPreviewStale, setPreviewConfig } from './device-config';

export const PREVIEW_DEBOUNCE_MS = 250;

export class ConfigPreview {
  private timer: ReturnType<typeof setTimeout> | null = null;

  /** `readText` returns the draft, or null when config.json is not the open file. */
  constructor(private readonly readText: () => string | null, private readonly delayMs = PREVIEW_DEBOUNCE_MS) {}

  schedule(): void {
    this.cancel();
    this.timer = setTimeout(() => this.flush(), this.delayMs);
  }

  /** Render the draft now (also what the debounce timer runs). */
  flush(): void {
    this.cancel();
    const text = this.readText();
    if (text === null) return;
    const model = parseConfigText(text);
    if (model) setPreviewConfig(model);
    else markPreviewStale();
  }

  /** Back to the stored config. */
  reset(): void {
    this.cancel();
    clearPreviewConfig();
  }

  private cancel(): void {
    if (this.timer !== null) clearTimeout(this.timer);
    this.timer = null;
  }
}
