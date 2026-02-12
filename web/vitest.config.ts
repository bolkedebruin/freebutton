import { defineConfig } from 'vitest/config';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [
    tailwindcss(),
  ],
  resolve: {
    alias: {
      '@': '/src',
    },
  },
  test: {
    environment: 'happy-dom',
    include: ['src/**/*.test.ts'],
    exclude: [
      // Phase 2 tests — dependencies not yet installed
      'src/js/autocomplete.test.ts',
      'src/js/validator.test.ts',
    ],
  },
});
