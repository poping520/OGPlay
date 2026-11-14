import { defineConfig } from 'vite';
import { targetPlatform, webuiOutDir } from './scripts/platform.mjs';

// Classic IIFE avoids file:// ES module CORS restrictions in the system WebView.
export default defineConfig({
  define: { __OGPLAY_WINDOWS__: JSON.stringify(targetPlatform === 'Windows') },
  build: {
    outDir: webuiOutDir('gui'), emptyOutDir: true,
    lib: { entry: 'apps/gui/main.tsx', name: 'OGPlayGui', formats: ['iife'], fileName: () => 'gui.js' },
    cssCodeSplit: false,
  },
});
