import { defineConfig } from 'vite';
import { targetPlatform, webuiOutDir } from './scripts/platform.mjs';

const phone = targetPlatform === 'Android';

// Classic IIFE avoids file:// ES module CORS restrictions in the system WebView.
export default defineConfig({
  // Windows and Android bundle SN Pro; macOS and Linux use their system faces. Android is the phone launcher: its
  // own entry, and shared code drops the console side (and desktop-only features) on __OGPLAY_PHONE__.
  define: {
    __OGPLAY_SN_PRO__: JSON.stringify(targetPlatform === 'Windows' || targetPlatform === 'Android'),
    __OGPLAY_PHONE__: JSON.stringify(phone),
  },
  build: {
    outDir: webuiOutDir('gui'), emptyOutDir: true,
    lib: { entry: phone ? 'apps/gui/phone/main.tsx' : 'apps/gui/main.tsx', name: 'OGPlayGui', formats: ['iife'], fileName: () => 'gui.js' },
    cssCodeSplit: false,
  },
});
