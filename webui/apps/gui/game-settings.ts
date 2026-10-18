import { settingsError, type SettingField, type SettingValue } from './settings';
export interface GameSettings {
  schema: number; installation_id: string; display_name: string; profile_id: string;
  revision: string; config_path: string; fields: SettingField[];
  values: Record<string, SettingValue>; inherited: Record<string, SettingValue>;
  facts: Record<string, string>;
  previews: Record<string, { argv: string[]; error: string }>;
}
export const gameGroups = [
  ['performance', '性能', '解释器、帧率与原生 CPU 限速。'], ['device', '虚拟设备', '游戏查询到的设备参数与身份。'],
  ['display', '显示', '分辨率倍率、窗口与方向。'], ['audio', '音频', '音量与静音。'], ['input', '输入', '键位模板与手柄。运行中按 F2 打开按键设置。'],
  ['data', '数据与沙盒', '存档沙盒、日志与外部数据包。'], ['network', '网络', '此游戏的网络意向。'],
  ['compatibility', '兼容性', '导入记录的 Profile 与覆盖目录。'], ['advanced', '高级', 'MCP、临时沙盒与启动参数预览。'],
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
