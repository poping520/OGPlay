// Cartridge viewer: the hero's canvas, renderer and scene re-hosted with orbit controls, camera presets, an exploded
// view of the board and a readout of the part under the pointer. Hero and viewer never hold the canvas at once.
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { THREE, type Kit } from './kit';
import type { CartFace, InspectEvents, Inspector, View } from './api';

type Look = Awaited<ReturnType<Kit['lookFor']>>;
type Vec = [number, number, number];
export interface ViewerHost {
  kit: Kit;
  renderer: THREE.WebGLRenderer;
  env: THREE.Texture;
  scene: THREE.Scene;
  pivot: THREE.Group;                                      // the hero's animation transform, held still here
  shadow: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
  look(face: CartFace): Promise<Look>;                     // cached, pinned and compiled for this renderer
}

const PRESETS: Record<View, Vec> = {
  three: [4.8, 1.9, 8.2], front: [0, 0, 9.8], back: [0, 0, -9.8], side: [9.8, 0, 0], top: [0, 9.8, 0.01], bottom: [0, -9.8, 0.01],
};
// Exploded view: the shell halves part along the depth axis, what is mounted on a half travels further the same
// way, the screws back out of the rear half and the board slides out sideways with everything on it.
const HALF = 0.6;
const EXPLODE: Record<string, Vec | Record<string, Vec>> = {
  Shell: { front: [0, 0, HALF], back: [0, 0, -HALF] }, Trim: { front: [0, 0, HALF], back: [0, 0, -HALF] },
  Art: [0, 0, HALF + 0.9], Title: [0, -0.12, HALF + 0.75], LED: [0, 0.25, HALF + 0.6], Spine: [0, 0.6, HALF * 0.5],
  BackLabel: [0, 0, -HALF - 0.9], Holo: [0, 0, -HALF - 1.3], Screw: [0, 0, -HALF - 0.65],
  PCB: [2.5, 0, 0], Chip: [2.5, 0, 0], Metal: [2.5, 0, 0], Passive: [2.5, 0, 0], Gold: [2.5, 0, 0], Marking: [2.5, 0, 0],
};
const easeInOut = (u: number) => u < 0.5 ? 4 * u * u * u : 1 - Math.pow(-2 * u + 2, 3) / 2;

export function createInspector(host: ViewerHost, stage: HTMLElement, events: InspectEvents): Inspector {
  const { kit, renderer, scene, pivot, shadow } = host;
  const canvas = renderer.domElement;
  const still = matchMedia('(prefers-reduced-motion: reduce)');
  pivot.position.set(0, 0, 0); pivot.rotation.set(0, 0, 0);
  pivot.updateMatrixWorld(true);

  // each part's rest position and its exploded offset in its parent's space
  const homes = new Map<THREE.Object3D, { at: THREE.Vector3; dir: THREE.Vector3 }>();
  for (const [name, spec] of Object.entries(EXPLODE)) for (const mesh of kit.parts[name] ?? []) {
    const offset = Array.isArray(spec) ? spec : spec[mesh.userData.ogp_half as string];
    if (!offset) continue;
    const turn = mesh.parent!.getWorldQuaternion(new THREE.Quaternion()).invert();
    const scale = mesh.parent!.getWorldScale(new THREE.Vector3());
    homes.set(mesh, { at: mesh.position.clone(), dir: new THREE.Vector3(...offset).applyQuaternion(turn).divide(scale) });
  }
  const spread = (k: number) => { const e = easeInOut(k); for (const [mesh, home] of homes) mesh.position.copy(home.at).addScaledVector(home.dir, e); };

  const camera = new THREE.PerspectiveCamera(28, 1, 0.05, 100);
  camera.position.set(...PRESETS.three);
  const controls = new OrbitControls(camera, canvas);
  Object.assign(controls, { enableDamping: true, dampingFactor: 0.08, enablePan: false, minDistance: 4, maxDistance: 20, autoRotateSpeed: 1.6, zoomSpeed: 0.8 });
  let tween: { t: number; from: THREE.Vector3; to: THREE.Vector3 } | null = null;
  controls.addEventListener('start', () => { tween = null; events.orbit(); });

  const fit = () => {
    if (!stage.clientWidth || !stage.clientHeight) return;
    renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    renderer.setSize(stage.clientWidth, stage.clientHeight, false);
    camera.aspect = stage.clientWidth / stage.clientHeight; camera.updateProjectionMatrix();
  };
  const resize = new ResizeObserver(fit);

  // part readout: raycast once per frame after the pointer moved, never while a button is held (orbiting). Parts are
  // known by mesh, since each entry's look replaces the named GLB materials.
  const partOf = new Map(Object.entries(kit.parts).flatMap(([name, meshes]) => meshes.map(mesh => [mesh as THREE.Object3D, name] as const)));
  const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
  let pointer: PointerEvent | null = null, under: string | null = null;
  const report = (name: string | null, event?: PointerEvent) => { if (name !== under || name) events.part(name, event?.clientX ?? 0, event?.clientY ?? 0); under = name; };
  const move = (event: PointerEvent) => { pointer = event.buttons ? null : event; if (!pointer) report(null); };
  const leave = () => { pointer = null; report(null); };
  const hover = () => {
    if (!pointer) return;
    const box = canvas.getBoundingClientRect();
    ndc.set((pointer.clientX - box.left) / box.width * 2 - 1, -(pointer.clientY - box.top) / box.height * 2 + 1);
    ray.setFromCamera(ndc, camera);
    const hit = ray.intersectObject(kit.model, true).find(entry => entry.object.visible);
    report(hit ? partOf.get(hit.object) ?? null : null, pointer);
    pointer = null;
  };

  let look: Look | null = null, shownKey = '';
  let open = 0, opening = 0;
  const floor = -kit.modelSize.y / 2 - 0.03;
  const savedShadow = shadow.position.y;
  const clock = new THREE.Clock();
  const frame = () => {
    if (!look) return;
    const dt = Math.min(clock.getDelta(), 0.05);
    if (tween) {
      tween.t = still.matches ? 1 : Math.min(1, tween.t + dt / 0.6);
      const e = 1 - Math.pow(1 - tween.t, 3), r = tween.from.length() + (tween.to.length() - tween.from.length()) * e;
      const dir = tween.from.clone().normalize().lerp(tween.to.clone().normalize(), e);
      if (dir.lengthSq() < 1e-6) dir.set(0.01, 0, 1);     // opposite presets swing through the side
      controls.target.set(0, 0, 0);
      camera.position.copy(dir.normalize().multiplyScalar(r));
      if (tween.t >= 1) tween = null;
    }
    if (open !== opening) { open = still.matches ? opening : opening > open ? Math.min(opening, open + dt / 0.9) : Math.max(opening, open - dt / 0.9); spread(open); }
    controls.update(dt);
    kit.wear(look);
    look.LED.emissiveIntensity = look.tone === 'running' && !still.matches ? 1.4 + Math.sin(clock.elapsedTime * 4.5) * 1.1 : 2.2;
    scene.environment = host.env; scene.environmentIntensity = 0.5;
    shadow.position.y = floor; shadow.scale.setScalar(1.15);
    shadow.material.opacity = 1 - open * 0.6;
    shadow.visible = camera.position.y > floor + 0.05;
    hover();
    renderer.render(scene, camera);
    shadow.visible = false;
  };

  if (!canvas.parentElement) canvas.animate([{ opacity: 0 }, { opacity: 1 }], 500);
  stage.append(canvas);
  resize.observe(stage);
  canvas.addEventListener('pointermove', move);
  canvas.addEventListener('pointerleave', leave);
  fit();
  renderer.setAnimationLoop(frame);

  return {
    show(face) {
      if (face.key === shownKey) return;
      shownKey = face.key;
      void host.look(face).then(value => { if (shownKey === face.key) look = value; });
    },
    view(name) {
      const to = new THREE.Vector3(...PRESETS[name]);
      tween = { t: 0, from: camera.position.clone().sub(controls.target), to };
    },
    open(on) { opening = on ? 1 : 0; },
    spin(on) { controls.autoRotate = on && !still.matches; },
    close() {
      renderer.setAnimationLoop(null);
      resize.disconnect(); controls.dispose();
      canvas.removeEventListener('pointermove', move); canvas.removeEventListener('pointerleave', leave);
      spread(0); shadow.position.y = savedShadow; shadow.scale.setScalar(1);
      if (canvas.parentElement === stage) canvas.remove();
    },
  };
}
