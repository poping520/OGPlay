import { t } from '../shared/i18n';
import { settingsError, type SettingField, type SettingValue } from './settings';
export interface GameSettings {
  schema: number; installation_id: string; display_name: string; profile_id: string | null;
  revision: string; config_path: string; fields: SettingField[];
  values: Record<string, SettingValue>; inherited: Record<string, SettingValue>;
  facts: Record<string, string>;
  previews: Record<string, { argv: string[]; error: string }>;
  reserved_visible: boolean;                     // false in release builds
}
// Most-changed, effective groups first; developer switches last.
export const gameGroups = [
  ['display', '显示'], ['data', '数据与沙盒'], ['input', '输入'], ['audio', '音频'], ['performance', '性能'],
  ['device', '虚拟设备'], ['compatibility', '兼容性'], ['network', '网络'], ['advanced', '高级'],
] as const;

export function inheritSetting(values: Record<string, SettingValue>, key: string): Record<string, SettingValue> {
  const next = { ...values }; delete next[key]; return next;
}
// One-time setup of this game's data pack, not an override of anything: rows show no inheritance,
// the customized count skips them, and "inherit all" keeps them.
const instanceOnlySettings = ['external_dir', 'external_guest_dir'];
export const isInstanceOnlySetting = (key: string) => instanceOnlySettings.includes(key);
export function inheritAllSettings(values: Record<string, SettingValue>): Record<string, SettingValue> {
  return Object.fromEntries(Object.entries(values).filter(([key]) => isInstanceOnlySetting(key)));
}
export function gameSettingsError(saved: Pick<GameSettings, 'fields' | 'inherited'>, draft: Record<string, SettingValue>): string | null {
  const effective = { ...saved.inherited, ...draft };
  const error = settingsError(saved.fields, effective);
  if (error) return error;
  return effective.mcp_manual_step === true && effective.mcp_enabled !== true ? t('MCP 手动步进需要启用 MCP。') : null;
}
