/**
 * MD5 of a byte array, as 32 lower-case hex digits (RFC 1321).
 *
 * Only used to compare a raster with the ETag the device lists for it: the
 * media store keeps the MD5 of every object as its ETag, so an unchanged
 * raster is not uploaded again (icon-sync.ts). Not for anything security
 * related.
 */

// Per-round shift amounts.
const S = [
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];

// K[i] = floor(abs(sin(i + 1)) * 2^32).
const K = new Uint32Array(64);
for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000) >>> 0;

export function md5Hex(data: Uint8Array): string {
  const len = data.length;
  // Message + 0x80 + zero padding + 64-bit length, a multiple of 64 bytes.
  const padded = new Uint8Array(((len + 8) >> 6) * 64 + 64);
  padded.set(data);
  padded[len] = 0x80;
  const bitLenLo = (len * 8) >>> 0;
  const bitLenHi = Math.floor(len / 0x20000000) >>> 0;
  const tail = padded.length - 8;
  for (let i = 0; i < 4; i++) {
    padded[tail + i] = (bitLenLo >>> (8 * i)) & 0xff;
    padded[tail + 4 + i] = (bitLenHi >>> (8 * i)) & 0xff;
  }

  let a0 = 0x67452301;
  let b0 = 0xefcdab89;
  let c0 = 0x98badcfe;
  let d0 = 0x10325476;
  const m = new Uint32Array(16);

  for (let off = 0; off < padded.length; off += 64) {
    for (let i = 0; i < 16; i++) {
      const j = off + i * 4;
      m[i] = (padded[j]! | (padded[j + 1]! << 8) | (padded[j + 2]! << 16) | (padded[j + 3]! << 24)) >>> 0;
    }
    let a = a0;
    let b = b0;
    let c = c0;
    let d = d0;
    for (let i = 0; i < 64; i++) {
      let f: number;
      let g: number;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const sum = (a + f + K[i]! + m[g]!) >>> 0;
      a = d;
      d = c;
      c = b;
      b = (b + ((sum << S[i]!) | (sum >>> (32 - S[i]!)))) >>> 0;
    }
    a0 = (a0 + a) >>> 0;
    b0 = (b0 + b) >>> 0;
    c0 = (c0 + c) >>> 0;
    d0 = (d0 + d) >>> 0;
  }

  let hex = '';
  for (const word of [a0, b0, c0, d0]) {
    for (let i = 0; i < 4; i++) hex += ((word >>> (8 * i)) & 0xff).toString(16).padStart(2, '0');
  }
  return hex;
}
