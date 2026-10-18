declare module '*.png' { const source: string; export default source; }
declare module '*.glb' { const source: string; export default source; }
declare module 'three/addons/libs/draco/gltf/draco_decoder.js' {
  const DracoDecoderModule: (config: { onModuleLoaded: (draco: unknown) => void }) => unknown;
  export default DracoDecoderModule;
}
