// Row glyphs for the settings pages, keyed by host setting key. Drawn like the group icons in settings-ui.tsx: 24-unit
// grid, round 2px strokes, no fill except dots; the chip keeps its group's colour, so the group still reads at a glance
// and the glyph says what the row is. One concept keeps one glyph wherever it appears (global and per-game pages).
const WIDTH = '<rect x="3" y="6" width="18" height="12" rx="2"/><path d="M7 12h10M9 10l-2 2 2 2M15 10l2 2-2 2"/>';
const HEIGHT = '<rect x="6" y="3" width="12" height="18" rx="2"/><path d="M12 7v10M10 9l2-2 2 2M10 15l2 2 2-2"/>';
const DATA_PACK = '<path d="M21 8l-9-5-9 5v8l9 5 9-5z"/><path d="M3 8l9 5 9-5M12 13v8"/>';
const SPEAKER = '<path d="M11 5 6 9H3v6h3l5 4z"/>';

export const SETTING_ICONS: Record<string, string> = {
  // general
  language: '<path d="M4 5h9M8.5 3v2M6 5c0 4 3 7 6 8M11 5c0 3-2.5 7-6.5 9"/><path d="M13 21l4-9 4 9M14.5 18h5"/>',
  theme: '<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 0 0 16z" fill="currentColor"/>',
  density: '<path d="M4 6h16M4 10h16M4 14h16M4 18h16"/>',
  cartridge: '<path d="M6 3h12a1 1 0 0 1 1 1v13H5V4a1 1 0 0 1 1-1z"/><path d="M7 17v3h10v-3"/><rect x="8" y="6" width="8" height="7" rx="1"/>',
  settings_style: '<path d="M12 3a9 9 0 1 0 0 18c1.1 0 1.5-.8 1.5-1.6 0-1.2-1-1.6-1-2.6 0-.9.8-1.6 1.8-1.6H17a4 4 0 0 0 4-4c0-4.6-4-8.2-9-8.2z"/><path d="M7.5 11h.01M10 7h.01M15 7.5h.01"/>',
  minimize_on_launch: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M12 8v6M9 11l3 3 3-3M8 17h8"/>',
  show_exit_log: '<path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4M9 12h6M9 16h4"/>',
  confirm_delete: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
  // storage, data and compatibility
  profiles_dir: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M8 12h8M8 15h5"/>',
  default_external_dir: DATA_PACK,
  external_dir: DATA_PACK,
  external_guest_dir: '<path d="M7 3h8l4 4v12a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M9 7v3M12 7v3M15 8v2"/>',
  data_mount_hint: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-4 10.5c.8.8 1 1.5 1 2.5h6c0-1 .2-1.7 1-2.5A6 6 0 0 0 12 3z"/>',
  log_retention: '<rect x="3" y="4" width="18" height="5" rx="1"/><path d="M5 9v10h14V9M10 13h4"/>',
  // graphics and display
  angle_backend: '<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>',
  surface_width: WIDTH,
  surface_height: HEIGHT,
  supersample: '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M4 12h16M12 4v16"/>',
  window_mode: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
  scaling: '<rect x="3" y="3" width="10" height="10" rx="1.5"/><path d="M14 21h7v-7M21 21l-8-8"/>',
  orientation: '<rect x="8" y="2" width="8" height="14" rx="1.5"/><path d="M4 14a8 8 0 0 0 8 8M4 14v3M4 14h3"/>',
  fps_overlay: '<rect x="3" y="4" width="18" height="14" rx="2"/><path d="M6.5 8h4M6.5 11h2.5M8 21h8"/>',
  // audio and input
  audio_device: '<rect x="6" y="2" width="12" height="20" rx="2"/><circle cx="12" cy="14" r="3"/><path d="M12 6.5h.01"/>',
  volume: `${SPEAKER}<path d="M15 9a4 4 0 0 1 0 6"/>`,
  mute: `${SPEAKER}<path d="M16 9l5 6M21 9l-5 6"/>`,
  mute_unfocused: `${SPEAKER}<path d="M21 14.5A4.5 4.5 0 0 1 16 9.5a4.5 4.5 0 1 0 5 5z"/>`,
  keymap: '<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
  gamepad_deadzone: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3.5"/>',
  // virtual machine and performance
  interpreter: '<path d="M8 6l-6 6 6 6M16 6l6 6-6 6M14 4l-4 16"/>',
  clock_mode: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  device_preset: '<rect x="6" y="2" width="12" height="20" rx="2"/><path d="M9 12l2 2 4-4M11 18h2"/>',
  max_fps: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 4v16M17 4v16M3 9h4M3 15h4M17 9h4M17 15h4"/>',
  clock_rate: '<path d="M3 6l8 6-8 6zM13 6l8 6-8 6z"/>',
  jit_cache_mb: '<path d="M13 2 4 14h7l-1 8 9-12h-7z"/>',
  cpu_limit_parallelism: '<path d="M6 4v16M12 4v16M18 4v16M4 10h4M10 14h4M16 8h4"/>',
  cpu_max_mticks_per_second: '<circle cx="12" cy="13" r="8"/><path d="M12 13l3-3M10 2h4"/>',
  // virtual device and its identity
  cpu_cores: '<rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 9h2v2H9zM13 9h2v2h-2zM9 13h2v2H9zM13 13h2v2h-2z"/>',
  cpu_frequency_mhz: '<path d="M3 16h3V8h4v8h4V8h4v8h3"/>',
  screen_width: WIDTH,
  screen_height: HEIGHT,
  density_dpi: '<path d="M6 6h.01M12 6h.01M18 6h.01M6 12h.01M12 12h.01M18 12h.01M6 18h.01M12 18h.01M18 18h.01"/>',
  memory_mb: '<rect x="2" y="7" width="20" height="9" rx="1"/><path d="M6 10v3M10 10v3M14 10v3M18 10v3M6 16v3M10 16v3M14 16v3M18 16v3"/>',
  brand: '<path d="M3 12V4h8l10 10-8 8z"/><path d="M7.5 8.5h.01"/>',
  manufacturer: '<path d="M3 21V10l6 4v-4l6 4V6h6v15z"/><path d="M7 18h2M13 18h2"/>',
  model: '<rect x="7" y="2" width="10" height="20" rx="2"/><path d="M10 7h4M10 10h3"/>',
  device: '<path d="M5 9h14M5 15h14M10 3 8 21M16 3l-2 18"/>',
  product: '<path d="M4 5v14M7 5v14M11 5v14M13 5v14M17 5v14M20 5v14"/>',
  cpu_name: '<rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 10h6M9 14h4M9 2v3M15 2v3M9 19v3M15 19v3"/>',
  gpu_vendor: '<rect x="2" y="6" width="20" height="11" rx="2"/><circle cx="9" cy="11.5" r="3"/><path d="M15 10h3M15 13h3M5 17v3"/>',
  gpu_renderer: '<path d="M12 3l9 17H3z"/><path d="M12 3v17M7.5 11.5 12 20l4.5-8.5"/>',
  locale: '<path d="M12 21s-7-6.5-7-12a7 7 0 0 1 14 0c0 5.5-7 12-7 12z"/><circle cx="12" cy="9" r="2.5"/>',
  timezone: '<circle cx="12" cy="12" r="9"/><path d="M3 12h3M18 12h3M12 7v5l3 3"/>',
  // network, diagnostics, control and advanced
  network_policy: '<path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/>',
  log_level: '<path d="M3 5h18l-7 8v6l-4 2v-8z"/>',
  jsonl: '<path d="M8 3H7a2 2 0 0 0-2 2v4a2 2 0 0 1-2 2 2 2 0 0 1 2 2v4a2 2 0 0 0 2 2h1M16 3h1a2 2 0 0 1 2 2v4a2 2 0 0 0 2 2 2 2 0 0 0-2 2v4a2 2 0 0 1-2 2h-1"/>',
  stall_timeout: '<path d="M6 3h12M6 21h12M7 3v3l5 6 5-6V3M7 21v-3l5-6 5 6v3"/>',
  teardown_timeout: '<path d="M12 3v8M6.3 7a8 8 0 1 0 11.4 0"/>',
  crash_dump_dir: '<rect x="7" y="7" width="10" height="13" rx="5"/><path d="M12 7v13M3 12h4M17 12h4M4 7l3 2M20 7l-3 2M4 19l3-2M20 19l-3-2M9 4l1.5 2M15 4l-1.5 2"/>',
  mcp_enabled: '<path d="M9 2v5M15 2v5M6 7h12v4a6 6 0 0 1-12 0zM12 17v5"/>',
  mcp_port: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 19v-5h8v5M10 14v-2M14 14v-2"/>',
  dashboard_auto_open: '<rect x="3" y="3" width="7" height="9" rx="1"/><rect x="14" y="3" width="7" height="5" rx="1"/><rect x="14" y="12" width="7" height="9" rx="1"/><rect x="3" y="16" width="7" height="5" rx="1"/>',
  mcp_manual_step: '<path d="M5 5l9 7-9 7z"/><path d="M18 5v14"/>',
  ephemeral_sandbox: '<rect x="4" y="4" width="16" height="16" rx="2" stroke-dasharray="3 3"/><path d="M9 12h6"/>',
};
// The same concept drawn twice on purpose; any other repeated glyph is a mistake (settings.test.ts).
export const SHARED_ICONS = [['surface_width', 'screen_width'], ['surface_height', 'screen_height'], ['default_external_dir', 'external_dir']];
