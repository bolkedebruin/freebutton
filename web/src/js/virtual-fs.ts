/**
 * Virtual Filesystem - In-memory file storage for the browser IDE.
 * Emits files-change events via app-state on any mutation.
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

/** Seed with default mock files so the UI matches Phase 1 visuals */
export function initDefaultFiles(): void {
  const encoder = new TextEncoder();
  // Sizes chosen to match mock data: 2.1KB, 1.4KB, 24.3KB, 3.8KB
  virtualFS.addFile('logo.svg', encoder.encode('x'.repeat(Math.round(2.1 * 1024))));
  virtualFS.addFile('icon-home.svg', encoder.encode('x'.repeat(Math.round(1.4 * 1024))));
  virtualFS.addFile('banner.png', encoder.encode('x'.repeat(Math.round(24.3 * 1024))));
  virtualFS.addFile('bg-pattern.svg', encoder.encode('x'.repeat(Math.round(3.8 * 1024))));
}
