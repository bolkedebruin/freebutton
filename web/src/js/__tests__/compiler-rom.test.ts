import { describe, it, expect } from 'vitest';
import { romBaseFor } from '../compiler';

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
