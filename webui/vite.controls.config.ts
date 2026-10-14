import { defineConfig } from 'vite';
export default defineConfig({ build: {
  outDir: '../data/webui/controls', emptyOutDir: true,
  lib: { entry: 'apps/controls/main.tsx', name: 'OGPlayControls', formats: ['iife'], fileName: () => 'controls.js', cssFileName: 'controls' },
  cssCodeSplit: false,
} });
