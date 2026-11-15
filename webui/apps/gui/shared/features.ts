import { rpc } from './rpc';

// What the launcher host offers on this platform (host.features). The host already leaves out the methods and
// setting fields of the rest; the page only stops drawing their entries, notes and key hints.
export const featureNames = [
  'key_editor', 'dashboard', 'mcp', 'launch_modes', 'open_directory', 'desktop_window', 'path_settings', 'file_upload',
  'developer',
] as const;
export type Feature = typeof featureNames[number];
export interface HostFeatures { platform: string; features: Record<Feature, boolean> }

const everything = Object.fromEntries(featureNames.map(name => [name, true])) as Record<Feature, boolean>;
let current: HostFeatures = { platform: '', features: everything };

// The phone build's host (Android) offers none of them, so there the answer is fixed and the build drops their code.
export const has = (feature: Feature): boolean => !__OGPLAY_PHONE__ && current.features[feature] === true;

// Which screens the page draws, fixed per build (vite.config.ts): the Android build is the phone layout (portrait,
// touch), desktop builds the console layout. Code shared by both branches on __OGPLAY_PHONE__ so each build keeps
// only its own side.
export type Layout = 'console' | 'phone';
export const layout = (): Layout => __OGPLAY_PHONE__ ? 'phone' : 'console';
export const hostPlatform = (): string => current.platform;

// Settings groups that exist only for a capability: with it left out their fields are gone, and so are their notes.
const groupCapabilities: Record<string, Feature[]> = {
  input: ['key_editor'], control: ['mcp', 'dashboard'], advanced: ['mcp', 'developer'],
};
export const groupAvailable = (key: string): boolean => !(key in groupCapabilities) || groupCapabilities[key].some(has);

// Read once before the first render. A host without the method (or the page opened outside it) keeps the full
// desktop set; real host failures surface on the first library request.
export async function loadFeatures(): Promise<void> {
  try {
    const value = await rpc<HostFeatures>('host.features');
    current = { platform: value.platform, features: { ...everything, ...value.features } };
  } catch { /* keep the desktop set */ }
}

// For tests only.
export function setFeatures(value: HostFeatures): void { current = value; }
