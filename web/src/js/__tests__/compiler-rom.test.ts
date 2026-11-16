import { describe, it, expect } from 'vitest';
import { bytecodeUploadUrl, romBaseFor } from '../compiler';

describe('romBaseFor', () => {
  const info = { rom_base: '0x3c1ed800', atoms_id: '0x1b542472' };
  it('gives the device table address when device and compiler have the same built-in names', () => {
    expect(romBaseFor(info, 0x1b542472)).toBe(0x3c1ed800);
  });
  it('is 0 when they differ, so the compiler does not write addresses of another table', () => {
    expect(romBaseFor(info, 0x1b542473)).toBe(0);
  });
  it('is 0 for firmware that does not report them, and for a compiler that cannot tell', () => {
    expect(romBaseFor({}, 0x1b542472)).toBe(0);
    expect(romBaseFor({ rom_base: '0x3c1ed800' }, 0x1b542472)).toBe(0);
    expect(romBaseFor(info, 0)).toBe(0);
  });
  it('handles ids with the top bit set', () => {
    expect(romBaseFor({ rom_base: '0x3c1ed800', atoms_id: '0xf0287d53' }, 0xf0287d53)).toBe(0x3c1ed800);
  });
});

describe('bytecodeUploadUrl', () => {
  it('passes the image size, so the device takes room for the image and not for the request', () => {
    expect(bytecodeUploadUrl('', 21340)).toBe('/api/bytecode/upload?size=21340');
    expect(bytecodeUploadUrl('http://10.0.0.5', new Uint8Array(16).length)).toBe('http://10.0.0.5/api/bytecode/upload?size=16');
  });
  it('adds the atoms id when the compiler resolved the built-in names', () => {
    expect(bytecodeUploadUrl('', 512, '0x1b542472')).toBe('/api/bytecode/upload?size=512&atoms=0x1b542472');
    expect(bytecodeUploadUrl('', 512, '')).toBe('/api/bytecode/upload?size=512');
  });
  it('is a query the device parses: plain decimal size, escaped atoms id', () => {
    const url = new URL(bytecodeUploadUrl('http://device', 262064, 'a b&c'));
    expect(url.searchParams.get('size')).toBe('262064');
    expect(url.searchParams.get('atoms')).toBe('a b&c');
  });
});
