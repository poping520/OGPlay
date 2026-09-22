import { settingsError, type SettingField, type SettingValue } from './settings';
export interface GameSettings {
  schema: number; installation_id: string; display_name: string; profile_id: string;
  revision: string; config_path: string; fields: SettingField[];
  values: Record<string, SettingValue>; inherited: Record<string, SettingValue>;
  facts: Record<string, string>;
  previews: Record<string, { argv: string[]; error: string }>;
}
export const gameGroups = [
  ['performance', '性能'], ['device', '虚拟设备'], ['display', '显示'], ['audio', '音频'], ['input', '输入'],
  ['data', '数据与沙盒'], ['network', '网络'], ['compatibility', '兼容性'], ['advanced', '高级'],
] as const;
export function inheritSetting(values: Record<string, SettingValue>, key: string): Record<string, SettingValue> {
  const next = { ...values }; delete next[key]; return next;
}
export function gameSettingsError(saved: Pick<GameSettings, 'fields' | 'inherited'>, draft: Record<string, SettingValue>): string | null {
  const effective = { ...saved.inherited, ...draft };
  const error = settingsError(saved.fields, effective);
  if (error) return error;
  return effective.mcp_manual_step === true && effective.mcp_enabled !== true ? 'MCP 手动步进需要启用 MCP。' : null;
}
