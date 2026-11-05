// Match CMAKE_SYSTEM_NAME; an override supports builds prepared on another host.
export const targetPlatform = process.env.OGPLAY_WEBUI_PLATFORM ??
  ({ win32: 'Windows', darwin: 'Darwin', linux: 'Linux' })[process.platform];
if (!['Windows', 'Darwin', 'Linux'].includes(targetPlatform)) {
  throw new Error(`Unsupported Web UI target platform: ${targetPlatform}`);
}
