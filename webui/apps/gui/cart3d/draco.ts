import { BufferAttribute, BufferGeometry } from 'three';
import type { DRACOLoader } from 'three/addons/loaders/DRACOLoader.js';
import DracoDecoderModule from 'three/addons/libs/draco/gltf/draco_decoder.js';

// The launcher CSP allows no fetch, blob: workers or WebAssembly, which DRACOLoader's worker pool needs. The
// pure-JS Draco build is bundled instead and decodes on the main thread; GLTFLoader only calls preload() and
// decodeDracoFile(), and glTF always addresses attributes by unique id.
type Draco = any;   // untyped Emscripten module
const ready = new Promise<{ draco: Draco }>(resolve => {
  // The module object is promise-like, so it is wrapped before resolving.
  DracoDecoderModule({ onModuleLoaded: (draco: Draco) => resolve({ draco }) });
});
const arrays = { Float32Array, Int8Array, Int16Array, Int32Array, Uint8Array, Uint16Array, Uint32Array };
type ArrayName = keyof typeof arrays;
const dataType = (draco: Draco, name: ArrayName) => ({
  Float32Array: draco.DT_FLOAT32, Int8Array: draco.DT_INT8, Int16Array: draco.DT_INT16, Int32Array: draco.DT_INT32,
  Uint8Array: draco.DT_UINT8, Uint16Array: draco.DT_UINT16, Uint32Array: draco.DT_UINT32,
})[name];

function decode(draco: Draco, bytes: Int8Array, ids: Record<string, number>, types: Record<string, ArrayName>): BufferGeometry {
  const decoder = new draco.Decoder(), mesh = new draco.Mesh();
  try {
    const status = decoder.DecodeArrayToMesh(bytes, bytes.byteLength, mesh);
    if (!status.ok() || mesh.ptr === 0) throw new Error(`Draco 解码失败：${status.error_msg()}`);
    const geometry = new BufferGeometry();
    const copy = <T extends ArrayName>(name: T, count: number, fill: (size: number, ptr: number) => void) => {
      const Type = arrays[name], size = count * Type.BYTES_PER_ELEMENT, ptr = draco._malloc(size);
      try { fill(size, ptr); return new Type(draco.HEAPF32.buffer, ptr, count).slice(); } finally { draco._free(ptr); }
    };
    for (const [name, id] of Object.entries(ids)) {
      const attribute = decoder.GetAttributeByUniqueId(mesh, id), components = attribute.num_components();
      const values = copy(types[name], mesh.num_points() * components,
        (size, ptr) => decoder.GetAttributeDataArrayForAllPoints(mesh, attribute, dataType(draco, types[name]), size, ptr));
      geometry.setAttribute(name, new BufferAttribute(values, components));
    }
    geometry.setIndex(new BufferAttribute(copy('Uint32Array', mesh.num_faces() * 3,
      (size, ptr) => decoder.GetTrianglesUInt32Array(mesh, size, ptr)), 1));
    return geometry;
  } finally {
    draco.destroy(mesh); draco.destroy(decoder);
  }
}

export const mainThreadDraco = {
  preload() { return this; },
  decodeDracoFile(buffer: ArrayBuffer, done: (geometry: BufferGeometry) => void, ids: Record<string, number>,
    types: Record<string, ArrayName>, _colorSpace: string, fail: (error: unknown) => void) {
    ready.then(({ draco }) => done(decode(draco, new Int8Array(buffer), ids, types))).catch(fail);
  },
} as unknown as DRACOLoader;
