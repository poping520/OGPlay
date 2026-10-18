// cart3d.js entry: the real-time hero cartridge and the rail stills. gui.js loads this bundle only when the
// cartridge setting is "3d" and falls back to the CSS cartridge if it fails.
import { loadKit, THREE } from './kit';
import type { Cart3D, CartFace } from './api';

const ease = (t: number) => 1 - Math.pow(1 - t, 3);

async function createCart3D(): Promise<Cart3D> {
  const kit = await loadKit();
  const scene = new THREE.Scene();
  const pivot = new THREE.Group();          // animation transforms
  pivot.add(kit.model);
  scene.add(pivot, ...kit.stageLights());
  const useEnv = (env: THREE.Texture) => { scene.environment = env; scene.environmentIntensity = 0.5; };
  const still = matchMedia('(prefers-reduced-motion: reduce)');

  // ---------------------------------------------------------------- hero
  const { renderer: hero, env: heroEnv } = kit.makeRenderer();
  const canvas = hero.domElement;
  canvas.className = 'cart3d';
  const camera = new THREE.PerspectiveCamera(26, 1, 0.1, 100);
  camera.position.set(0, 0.95, 10);
  camera.lookAt(0, -0.1, 0);
  let face: CartFace | null = null, look: Awaited<ReturnType<typeof kit.lookFor>> | null = null;
  let swapT = 1, drag = 0, dragging: { x: number; d: number } | null = null;
  const mouse = { x: 0, y: 0 };
  const track = (event: PointerEvent) => { mouse.x = event.clientX / innerWidth - 0.5; mouse.y = event.clientY / innerHeight - 0.5; };
  canvas.addEventListener('pointerdown', event => { dragging = { x: event.clientX, d: drag }; canvas.setPointerCapture(event.pointerId); });
  canvas.addEventListener('pointermove', event => { if (dragging) drag = dragging.d + (event.clientX - dragging.x) * 0.012; });
  canvas.addEventListener('pointerup', () => { dragging = null; });
  canvas.addEventListener('pointercancel', () => { dragging = null; });
  const fit = () => {
    const stage = canvas.parentElement;
    if (!stage || !stage.clientWidth || !stage.clientHeight) return;
    hero.setPixelRatio(Math.min(devicePixelRatio, 2));
    hero.setSize(stage.clientWidth, stage.clientHeight, false);
    camera.aspect = stage.clientWidth / stage.clientHeight; camera.updateProjectionMatrix();
  };
  const resize = new ResizeObserver(fit);

  // soft contact shadow on an invisible floor, follows the float
  const shadowTex = (() => {
    const c = document.createElement('canvas'); c.width = c.height = 256;
    const x = c.getContext('2d')!, gradient = x.createRadialGradient(128, 128, 0, 128, 128, 128);
    gradient.addColorStop(0, 'rgba(0,0,0,.75)'); gradient.addColorStop(0.45, 'rgba(0,0,0,.35)'); gradient.addColorStop(1, 'rgba(0,0,0,0)');
    x.fillStyle = gradient; x.fillRect(0, 0, 256, 256);
    return new THREE.CanvasTexture(c);
  })();
  const shadow = new THREE.Mesh(new THREE.PlaneGeometry(kit.modelSize.x * 1.6, kit.modelSize.z * 3.2),
    new THREE.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false, toneMapped: false }));
  shadow.rotation.x = -Math.PI / 2;
  shadow.position.y = -kit.modelSize.y / 2 - 0.4;
  shadow.visible = false;
  scene.add(shadow);

  const clock = new THREE.Clock();
  let reveal: (() => void) | null = null;
  const frame = () => {
    if (!look) return;
    const dt = Math.min(clock.getDelta(), 0.05), t = still.matches ? 0 : clock.elapsedTime;
    swapT = still.matches ? 1 : Math.min(1, swapT + dt / 0.75);
    if (!dragging) drag *= 0.93;
    const e = ease(swapT);
    pivot.position.y = Math.sin(t * 1.05) * 0.09 + (1 - e) * 0.6;
    pivot.rotation.set(0.1 + Math.sin(t * 0.8) * 0.025 + mouse.y * 0.12,
      -0.42 + Math.sin(t * 0.5) * 0.1 + mouse.x * 0.25 + drag + (1 - e) * Math.PI,
      Math.sin(t * 0.7) * 0.02);
    kit.wear(look);
    look.LED.emissiveIntensity = look.tone === 'running' && !still.matches ? 1.4 + Math.sin(clock.elapsedTime * 4.5) * 1.1 : 2.2;
    useEnv(heroEnv);
    const lift = pivot.position.y + 0.1;
    shadow.scale.setScalar(1 - lift * 0.35);
    shadow.material.opacity = THREE.MathUtils.clamp(0.9 - lift * 0.9, 0, 1);
    shadow.visible = true;
    hero.render(scene, camera);
    shadow.visible = false;
    reveal?.(); reveal = null;
  };

  // ---------------------------------------------------------------- rail stills
  const { renderer: thumbs, env: thumbEnv } = kit.makeRenderer({ preserveDrawingBuffer: true });
  thumbs.setPixelRatio(1);
  thumbs.setSize(270, 400, false);
  const thumbCam = new THREE.PerspectiveCamera(22, 270 / 400, 0.1, 100);
  thumbCam.position.set(0, 0.5, 11.2);
  thumbCam.lookAt(0, 0, 0);
  const stills = new Map<string, Promise<string>>();
  let queue = Promise.resolve();
  const thumb = (entry: CartFace) => {
    let url = stills.get(entry.key);
    if (!url) {
      // one at a time, so a long library never builds every look at once
      url = queue.then(async () => {
        const stillLook = await kit.lookFor(entry, () => face?.key ?? null);
        kit.wear(stillLook);
        stillLook.LED.emissiveIntensity = 2.2;
        const saved = [pivot.position.clone(), pivot.rotation.clone()] as const;
        pivot.position.set(0, 0, 0); pivot.rotation.set(0.08, -0.32, 0);
        useEnv(thumbEnv);
        thumbs.render(scene, thumbCam);
        pivot.position.copy(saved[0]); pivot.rotation.copy(saved[1]);
        return thumbs.domElement.toDataURL('image/png');
      });
      queue = url.then(() => {}, () => {});
      stills.set(entry.key, url);
    }
    return url;
  };

  return {
    mount(stage, shown) {
      if (!canvas.parentElement) canvas.animate([{ opacity: 0 }, { opacity: 1 }], 700);
      reveal = shown;
      stage.append(canvas);
      resize.disconnect(); resize.observe(stage);
      addEventListener('pointermove', track);
      fit();
      hero.setAnimationLoop(frame);
      return () => {
        if (canvas.parentElement !== stage) return;
        hero.setAnimationLoop(null); resize.disconnect(); removeEventListener('pointermove', track);
        canvas.remove();
      };
    },
    show(next) {
      if (face?.key === next.key) return;
      if (face && face.id !== next.id) swapT = 0;
      face = next;
      void kit.lookFor(next, () => face?.key ?? null).then(value => { if (face?.key === next.key) look = value; });
    },
    thumb,
  };
}

window.OGPlayCart3D = createCart3D();
