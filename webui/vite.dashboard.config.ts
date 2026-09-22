import { defineConfig } from 'vite';
export default defineConfig({ build: {
  outDir: '../data/webui/dashboard', emptyOutDir: true,
  lib: { entry: 'apps/dashboard/main.tsx', name: 'OGPlayDashboard', formats: ['iife'], fileName: () => 'dashboard.js', cssFileName: 'dashboard' },
  cssCodeSplit: false,
} });
