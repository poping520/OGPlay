import { defineConfig } from 'vite';

// The 3D cartridge (three.js, Draco decoder, model) ships as its own IIFE next to gui.js; gui.js adds it only when
// the cartridge setting is "3d", so the CSS cartridge never pays for it. Assets are inlined (lib mode).
export default defineConfig({
  assetsInclude: ['**/*.glb'],
  build: {
    outDir: '../data/webui/gui', emptyOutDir: false,
    lib: { entry: 'apps/gui/cart3d/index.ts', name: 'OGPlayCart3D', formats: ['iife'], fileName: () => 'cart3d.js' },
  },
});
