// Vite's eager raw glob, used by tests that read sources.
declare const __OGPLAY_SN_PRO__: boolean;
// The Android launcher build (phone layout only); false in desktop builds and tests.
declare const __OGPLAY_PHONE__: boolean;
declare module '*.css';
interface ImportMeta {
  glob(pattern: string | string[], options: { query: '?raw'; import: 'default'; eager: true }): Record<string, string>;
}
declare module '*.png' { const source: string; export default source; }
declare module '*.glb' { const source: string; export default source; }
declare module 'three/addons/libs/draco/gltf/draco_decoder.js' {
  const DracoDecoderModule: (config: { onModuleLoaded: (draco: unknown) => void }) => unknown;
  export default DracoDecoderModule;
}
