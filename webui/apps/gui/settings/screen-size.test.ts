import { describe, expect, it } from 'vitest';
import { aspectName, longSide, presetSize, screenFields } from './screen-size';
import type { SettingField } from './settings';

const field = (key: string, choices: string[] = []): SettingField =>
  ({ key, group: 'graphics', label: key, pending: '', initial: '', choices, min: 0, max: 0, directory: false, hidden: false });
const phone = { width: 2376, height: 1080 };

// Same cases as tests/frontend/game_run_args_tests.cpp: the page shows what the game process will use.
describe('screen size presets', () => {
  it('rounds the long side as the host does', () => {
    expect(longSide(480, 16, 9)).toBe(854);
    expect(longSide(480, 5, 3)).toBe(800);
    expect(longSide(480, 39, 18)).toBe(1040);
    expect(longSide(2160, 20, 9)).toBe(4096);
  });
  it('names the display ratio like the aspect choices', () => {
    expect(aspectName(phone)).toBe('19.8:9');
    expect(aspectName({ width: 2560, height: 1600 })).toBe('16:10');
    expect(aspectName({ width: 1920, height: 1080 })).toBe('16:9');
  });
  it('follows the display and never exceeds it', () => {
    expect(presetSize({ screen_resolution: '720p', screen_aspect: 'screen' }, phone)).toEqual({ width: 1584, height: 720 });
    expect(presetSize({ screen_resolution: '720p', screen_aspect: '16:9' }, phone)).toEqual({ width: 1280, height: 720 });
    expect(presetSize({ screen_resolution: '1440p', screen_aspect: '16:9' }, phone)).toEqual({ width: 1920, height: 1080 });
    expect(presetSize({ screen_resolution: '1440p', screen_aspect: 'screen' }, phone)).toEqual({ width: 2376, height: 1080 });
    expect(presetSize({ screen_resolution: 'custom', screen_aspect: '16:9' }, phone)).toBeNull();
  });
  it('shows custom rows only for a custom size and presets up to the display', () => {
    const fields = [field('screen_resolution', ['480p', '720p', '1080p', '1440p', '2160p', 'custom']), field('screen_aspect'),
      field('screen_width'), field('screen_height')];
    const preset = screenFields(fields, { screen_resolution: '720p' }, phone);
    expect(preset.map(item => item.key)).toEqual(['screen_resolution', 'screen_aspect']);
    expect(preset[0].choices).toEqual(['480p', '720p', '1080p', 'custom']);
    expect(screenFields(fields, { screen_resolution: '2160p' }, phone)[0].choices).toContain('2160p');
    expect(screenFields(fields, { screen_resolution: 'custom' }, phone).map(item => item.key))
      .toEqual(['screen_resolution', 'screen_width', 'screen_height']);
  });
});
