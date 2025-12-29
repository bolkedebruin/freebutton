import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import typescript from '@rollup/plugin-typescript';
import terser from '@rollup/plugin-terser';
import copy from 'rollup-plugin-copy';
import gzip from 'rollup-plugin-gzip';

const production = !process.env.ROLLUP_WATCH;

export default {
  input: 'src/js/main.ts',
  output: {
    file: 'dist/bundle.js',
    format: 'iife',
    sourcemap: !production
  },
  plugins: [
    typescript({
      tsconfig: './tsconfig.json',
      sourceMap: !production,
      inlineSources: !production
    }),
    resolve(),
    commonjs(),
    copy({
      targets: [
        { src: 'src/index.html', dest: 'dist' },
        { src: 'src/css/*', dest: 'dist/css' },
        { src: '../lib/mquickjs/mquickjs.wasm', dest: 'dist/assets' },
        { src: '../lib/mquickjs/mquickjs.js', dest: 'dist/assets' },
        { src: '../lib/mquickjs/mquickjs-compiler.js', dest: 'dist/assets' }
      ]
    }),
    production && terser(),
    production && gzip()
  ]
};
