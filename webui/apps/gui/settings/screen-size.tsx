import type { SettingField, SettingValue } from './settings';
import { t } from '../shared/i18n';

// The guest screen size the presets give, as the host computes it (src/frontend/library/screen_settings.cpp and
// SurfaceLongSide/AdaptSurfaceToDisplay in run_options.cpp): screen_resolution is the short side or "custom" (the
// width/height rows as set), screen_aspect the long side or "screen" (the display's). Presets never exceed the
// display, so the page offers only those that fit it.
export interface Size { width: number; height: number }

const resolutions: Record<string, number> = { '480p': 480, '540p': 540, '720p': 720, '1080p': 1080, '1440p': 1440, '2160p': 2160 };
const aspects: Record<string, [number, number]> = {
  '4:3': [4, 3], '16:10': [16, 10], '16:9': [16, 9], '18:9': [18, 9], '19.5:9': [39, 18], '20:9': [20, 9],
};
const MAXIMUM = 4096;

// Nearest length, made even (rounded up), between the short side and 4096.
export function longSide(short: number, numerator: number, denominator: number): number {
  let long = Math.floor((2 * short * numerator + denominator) / (2 * denominator));
  long += long % 2;
  return Math.min(MAXIMUM, Math.max(short, long));
}

// This device's display in pixels, landscape; null when the page cannot tell.
export function deviceDisplay(): Size | null {
  if (typeof window === 'undefined' || !window.screen) return null;
  const ratio = window.devicePixelRatio || 1;
  const a = Math.round(window.screen.width * ratio), b = Math.round(window.screen.height * ratio);
  return a > 0 && b > 0 ? { width: Math.max(a, b), height: Math.min(a, b) } : null;
}

// The display's ratio as the aspect choices write it: a listed ratio when it is one, otherwise width per 9
// (2376 × 1080 is 19.8:9).
export function aspectName(display: Size): string {
  const ratio = display.width / display.height;
  const listed = Object.entries(aspects).find(([, [numerator, denominator]]) => Math.abs(ratio - numerator / denominator) < 0.01);
  return listed ? listed[0] : `${Math.round(ratio * 90) / 10}:9`;
}
// "跟随屏幕" with the ratio it stands for on this device.
export function followScreenLabel(display: Size | null = deviceDisplay()): string {
  return display ? t('跟随屏幕（{ratio}）', { ratio: aspectName(display) }) : t('跟随屏幕');
}

export const isCustomScreen = (values: Record<string, SettingValue>) => values.screen_resolution === 'custom';

// The landscape size a launch gets on `display` (null: unknown), or null for a custom size.
export function presetSize(values: Record<string, SettingValue>, display: Size | null): Size | null {
  let short = resolutions[String(values.screen_resolution)];
  if (!short) return null;
  const aspect = String(values.screen_aspect);
  const [numerator, denominator] = aspect === 'screen' ? [16, 9] : aspects[aspect] ?? [16, 9];
  let long = longSide(short, numerator, denominator);
  if (display) {
    if (short > display.height) { long = longSide(display.height, long, short); short = display.height; }
    if (aspect === 'screen' && display.width !== display.height) long = longSide(short, display.width, display.height);
  }
  return { width: long, height: short };
}

// The rows a page shows: width/height only for a custom size, the aspect only for a preset; the resolution row
// lists presets up to the display's height (and keeps the current one, which may come from another device).
export function screenFields(fields: SettingField[], values: Record<string, SettingValue>, display: Size | null): SettingField[] {
  const custom = isCustomScreen(values);
  return fields.flatMap(field => {
    if (field.key === 'screen_width' || field.key === 'screen_height') return custom ? [field] : [];
    if (field.key === 'screen_aspect') return custom ? [] : [field];
    if (field.key === 'screen_resolution' && display)
      return [{ ...field, choices: field.choices.filter(choice =>
        choice === values.screen_resolution || !(choice in resolutions) || resolutions[choice] <= display.height) }];
    return [field];
  });
}

// Under the resolution row: what a preset gives on this device (custom sizes show their own rows instead).
export function ScreenSizeNote({ values, display }: { values: Record<string, SettingValue>; display: Size | null }) {
  const size = presetSize(values, display);
  if (!size) return null;
  return <div class="row-extra screen-size">
    <p><b>{t('实际分辨率 {width} × {height}', { width: size.width, height: size.height })}</b></p>
    {display && <p>{t('本机屏幕 {width} × {height}，预设不超过它', { width: display.width, height: display.height })}</p>}
  </div>;
}
