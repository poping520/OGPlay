import { defineConfig } from 'vite';

// Classic IIFE avoids file:// ES module CORS restrictions in the system WebView.
export default defineConfig({
  build: {
    outDir: '../data/webui/gui', emptyOutDir: true,
    lib: { entry: 'apps/gui/main.tsx', name: 'OGPlayGui', formats: ['iife'], fileName: () => 'gui.js' },
    cssCodeSplit: false,
  },
});
