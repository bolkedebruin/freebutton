/**
 * Virtual Filesystem - In-memory file storage for the browser IDE.
 *
 * Used as the workspace's fallback store when no device storage API is
 * reachable (dev without hardware, older firmware): files saved there live
 * only for this browser session. Keys follow the device's object keys
 * ("scripts/main.js"). Emits files-change events via app-state on mutation.
 */
import { emit, type FileEntry } from './app-state';

function detectType(name: string): FileEntry['type'] {
  const ext = name.split('.').pop()?.toLowerCase() ?? '';
  if (ext === 'js') return 'js';
  if (ext === 'json') return 'json';
  if (ext === 'svg') return 'svg';
  if (ext === 'png') return 'png';
  if (ext === 'bin') return 'bin';
  return 'other';
}

export function formatFileSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const kb = bytes / 1024;
  if (kb >= 1024) return `${(kb / 1024).toFixed(1)} MB`;
  return `${kb % 1 === 0 ? kb.toFixed(0) : kb.toFixed(1)} KB`;
}

class VirtualFS {
  private _files = new Map<string, { data: Uint8Array; type: FileEntry['type'] }>();
  readonly totalSpace = 128 * 1024; // 128 KB

  addFile(name: string, data: Uint8Array): boolean {
    if (this.usedSpace - (this._files.get(name)?.data.byteLength ?? 0) + data.byteLength > this.totalSpace) {
      return false;
    }
    this._files.set(name, { data, type: detectType(name) });
    this._notify();
    return true;
  }

  deleteFile(name: string): void {
    this._files.delete(name);
    this._notify();
  }

  getFile(name: string): Uint8Array | undefined {
    return this._files.get(name)?.data;
  }

  /** Store UTF-8 text. Returns false when it does not fit. */
  writeText(name: string, text: string): boolean {
    return this.addFile(name, new TextEncoder().encode(text));
  }

  /** Read a file as UTF-8 text, or null if it does not exist. */
  readText(name: string): string | null {
    const data = this.getFile(name);
    return data ? new TextDecoder().decode(data) : null;
  }

  getFileList(): FileEntry[] {
    return Array.from(this._files.entries()).map(([name, { data, type }]) => ({
      name,
      size: data.byteLength,
      type,
    }));
  }

  get usedSpace(): number {
    let total = 0;
    this._files.forEach(({ data }) => { total += data.byteLength; });
    return total;
  }

  private _notify(): void {
    emit('files-change', {
      files: this.getFileList(),
      used: this.usedSpace,
      total: this.totalSpace,
    });
  }
}

export const virtualFS = new VirtualFS();
