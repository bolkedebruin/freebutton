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
    // Pins the virtual device's MQTT start mode to offline: no test opens a socket by accident.
    setupFiles: ['src/test-setup.ts'],
    exclude: [],
  },
});
