// Contract between gui.js and the lazily loaded cart3d.js. Only plain host facts cross it, so the 3D bundle
// never calls RPC and the launcher can fall back to the CSS cartridge when it is missing or WebGL fails.
export interface CartFace {
  key: string;            // changes whenever anything printed or lit on the cartridge changes
  id: string;             // installation id
  name: string;
  package: string;
  version: string;
  imported: string;
  icon: string;           // host data URL, may be empty
  color: [number, number, number];
  tone: string;           // ready / running / warning / danger
  status: string;         // host status: damaged scuffs the cartridge, missing_external removes the gold
}
export interface Cart3D {
  // Moves the single hero canvas into `stage` and starts its loop; the returned function detaches it again.
  // `shown` fires once the first frame with a cartridge has been drawn after mounting.
  mount(stage: HTMLElement, shown: () => void): () => void;
  show(face: CartFace): void;
  // Prepares looks the hero is likely to show next (rail neighbours) while idle, so switching does not stall.
  prefetch(faces: CartFace[]): void;
  // Still frame for the rail, as a PNG data URL (img-src data: is the only image source the CSP allows).
  thumb(face: CartFace): Promise<string>;
}
declare global {
  interface Window { OGPlayCart3D?: Promise<Cart3D> }
}
