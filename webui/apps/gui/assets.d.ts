// Vite's eager raw glob, used by tests that read sources.
interface ImportMeta {
  glob(pattern: string | string[], options: { query: '?raw'; import: 'default'; eager: true }): Record<string, string>;
}
declare module '*.png' { const source: string; export default source; }
declare module '*.glb' { const source: string; export default source; }
declare module 'three/addons/libs/draco/gltf/draco_decoder.js' {
  const DracoDecoderModule: (config: { onModuleLoaded: (draco: unknown) => void }) => unknown;
  export default DracoDecoderModule;
}
