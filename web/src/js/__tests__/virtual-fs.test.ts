import { describe, it, expect, beforeEach } from 'vitest';
import { virtualFS, formatFileSize } from '../virtual-fs';

// Reset virtualFS between tests by deleting all files
beforeEach(() => {
  for (const f of virtualFS.getFileList()) {
    virtualFS.deleteFile(f.name);
  }
});

describe('virtual-fs', () => {
  it('starts empty', () => {
    expect(virtualFS.getFileList()).toHaveLength(0);
    expect(virtualFS.usedSpace).toBe(0);
  });

  it('addFile stores a file and reports size', () => {
    const data = new Uint8Array(100);
    expect(virtualFS.addFile('test.js', data)).toBe(true);
    expect(virtualFS.getFileList()).toHaveLength(1);
    expect(virtualFS.usedSpace).toBe(100);
  });

  it('addFile detects type from extension', () => {
    virtualFS.addFile('a.js', new Uint8Array(1));
    virtualFS.addFile('b.json', new Uint8Array(1));
    virtualFS.addFile('c.svg', new Uint8Array(1));
    virtualFS.addFile('d.png', new Uint8Array(1));
    virtualFS.addFile('e.bin', new Uint8Array(1));
    virtualFS.addFile('f.txt', new Uint8Array(1));

    const files = virtualFS.getFileList();
    const byName = (n: string) => files.find(f => f.name === n)!;
    expect(byName('a.js').type).toBe('js');
    expect(byName('b.json').type).toBe('json');
    expect(byName('c.svg').type).toBe('svg');
    expect(byName('d.png').type).toBe('png');
    expect(byName('e.bin').type).toBe('bin');
    expect(byName('f.txt').type).toBe('other');
  });

  it('deleteFile removes a file', () => {
    virtualFS.addFile('test.js', new Uint8Array(50));
    expect(virtualFS.getFileList()).toHaveLength(1);

    virtualFS.deleteFile('test.js');
    expect(virtualFS.getFileList()).toHaveLength(0);
    expect(virtualFS.usedSpace).toBe(0);
  });

  it('getFile returns stored data', () => {
    const data = new Uint8Array([1, 2, 3]);
    virtualFS.addFile('data.bin', data);
    const retrieved = virtualFS.getFile('data.bin');
    expect(retrieved).toEqual(data);
  });

  it('getFile returns undefined for missing file', () => {
    expect(virtualFS.getFile('nope.js')).toBeUndefined();
  });

  it('rejects file that exceeds total space', () => {
    const tooBig = new Uint8Array(128 * 1024 + 1);
    expect(virtualFS.addFile('huge.bin', tooBig)).toBe(false);
    expect(virtualFS.getFileList()).toHaveLength(0);
  });

  it('allows replacing an existing file within budget', () => {
    // Fill most of the space
    virtualFS.addFile('big.bin', new Uint8Array(120 * 1024));
    // Replace with same-sized file (should succeed since old is counted)
    expect(virtualFS.addFile('big.bin', new Uint8Array(120 * 1024))).toBe(true);
    expect(virtualFS.getFileList()).toHaveLength(1);
  });

  it('emits files-change event on add', () => {
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('files-change', handler as EventListener);

    virtualFS.addFile('x.js', new Uint8Array(10));
    expect(received).not.toBeNull();
    expect(received.files).toHaveLength(1);
    expect(received.used).toBe(10);
    expect(received.total).toBe(128 * 1024);

    window.removeEventListener('files-change', handler as EventListener);
  });

  it('emits files-change event on delete', () => {
    virtualFS.addFile('x.js', new Uint8Array(10));
    let received: any = null;
    const handler = (e: CustomEvent) => { received = e.detail; };
    window.addEventListener('files-change', handler as EventListener);

    virtualFS.deleteFile('x.js');
    expect(received).not.toBeNull();
    expect(received.files).toHaveLength(0);
    expect(received.used).toBe(0);

    window.removeEventListener('files-change', handler as EventListener);
  });

  it('totalSpace is 128KB', () => {
    expect(virtualFS.totalSpace).toBe(128 * 1024);
  });
});

describe('text helpers', () => {
  it('writeText / readText round-trip UTF-8', () => {
    expect(virtualFS.writeText('scripts/main.js', 'print("héllo")')).toBe(true);
    expect(virtualFS.readText('scripts/main.js')).toBe('print("héllo")');
  });

  it('readText returns null for a missing file', () => {
    expect(virtualFS.readText('nope.js')).toBeNull();
  });
});

describe('formatFileSize', () => {
  it('formats bytes', () => {
    expect(formatFileSize(500)).toBe('500 B');
  });

  it('formats KB with decimal', () => {
    expect(formatFileSize(2150)).toBe('2.1 KB');
  });

  it('formats exact KB without decimal', () => {
    expect(formatFileSize(2048)).toBe('2 KB');
  });

  it('formats large KB', () => {
    expect(formatFileSize(24883)).toBe('24.3 KB');
  });
});
