// Match CMAKE_SYSTEM_NAME; an override supports builds prepared on another host. `npm run build:android`
// targets Android without one.
export const targetPlatform = process.env.OGPLAY_WEBUI_PLATFORM ??
  (process.env.npm_lifecycle_event === 'build:android' ? 'Android' :
    ({ win32: 'Windows', darwin: 'Darwin', linux: 'Linux' })[process.platform]);
if (!['Windows', 'Darwin', 'Linux', 'Android'].includes(targetPlatform)) {
  throw new Error(`Unsupported Web UI target platform: ${targetPlatform}`);
}
// Android packages only the launcher, kept apart so it never replaces the desktop artifacts.
export const webuiApps = targetPlatform === 'Android' ? ['gui'] : ['gui', 'dashboard', 'controls'];
export const webuiOutDir = app => targetPlatform === 'Android' ? `../data/webui/android/${app}` : `../data/webui/${app}`;
