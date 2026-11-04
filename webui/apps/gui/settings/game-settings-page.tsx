import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc, type LibraryItem } from '../shared/rpc';
import { pickPath } from '../import/import';
import { displayValue, factRows, shownFields } from './settings';
import { gameGroups, gameSettingsError, inheritAllSettings, inheritSetting, isInstanceOnlySetting, type GameSettings } from './game-settings';
import { statuses, versionLabel } from '../library/library';
import { useIconColor } from '../cartridge/cartridge';
import { TileCart } from '../cartridge/cartridge3d';
import type { Cart3D } from '../cartridge/cart3d/api';
import { Note, Paths, SettingsFrame, useAutoSave, useToast } from './settings-ui';
import type { Skin } from '../kit';
import { t } from '../shared/i18n';

const modes = [['normal', '正常启动'], ['preflight', '预检'], ['diagnostic', '带诊断启动']] as const;
// argv from the host, one flag per line; the page never assembles arguments itself.
function Argv({ argv }: { argv: string[] }) {
  return <pre class="term" aria-label={t('生成的 argv')}>{argv.map((token, index) => <Fragment key={index}>
    {index > 0 && (token.startsWith('--') ? ' \\\n  ' : ' ')}
    <span class={index === 0 ? 'p' : token.startsWith('--') ? 'f' : ''}>{token}</span>
  </Fragment>)}</pre>;
}

// `clearData` returns to the library, whose confirmation dialog clears the saves.
export function GameSettingsPage({ id, item, skin, cart3d, onBack, clearData }: {
  id: string; item?: LibraryItem; skin: Skin; cart3d: Cart3D | 'waiting' | null; onBack: () => void; clearData: () => void;
}) {
  const [group, setGroup] = useState<string>(gameGroups[0][0]);
  const [mode, setMode] = useState<string>('normal');
  const [busy, setBusy] = useState(false);
  const [toast, say] = useToast();
  const lock = useRef(false);
  const color = useIconColor(item?.icon ?? '');
  const store = useAutoSave<GameSettings>({
    load: () => rpc<GameSettings>('game_settings.get', { installation_id: id }),
    write: (saved, values) => rpc<GameSettings>('game_settings.set', { installation_id: id, revision: saved.revision, values }),
    check: gameSettingsError,
  });
  const { saved, draft, saving, setError } = store;
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  useEffect(() => { void run(store.load); }, [id]);
  const open = (kind: string) => void run(async () => { await rpc('library.open_dir', { installation_id: id, kind }); });
  const name = item?.display_name ?? saved?.display_name ?? id;
  const tone = item?.running ? 'running' : statuses[item?.status ?? '']?.tone ?? 'warning';
  const preview = saved?.previews[mode];
  const actions = (...buttons: ComponentChildren[]) => <div class="acts">{buttons}</div>;
  const launch = <div class="launch">
    <div class="launch-h"><b>{t('启动与参数预览')}</b><span class="tag over">{t('来自已保存设置')}</span>
      <div class="seg" role="radiogroup" aria-label={t('启动模式')}>{modes.map(([key, label]) =>
        <button key={key} role="radio" aria-checked={mode === key} class={mode === key ? 'on' : ''} onClick={() => setMode(key)}>{t(label)}</button>)}</div></div>
    {preview?.error ? <pre class="term" role="alert">{preview.error}</pre> : <Argv argv={preview?.argv ?? []} />}
    <div class="launch-f"><span>{mode === 'preflight' ? t('预检不会启用 MCP，结果写入游戏日志。') : t('参数由宿主生成，前端不拼接自由参数。')}</span>
      <button class="pill main" disabled={busy || saving || !!preview?.error}
        onClick={() => void run(async () => { await rpc('library.launch', { installation_id: id, mode }); say(t('已启动游戏进程；预检结果与诊断信息见游戏日志')); })}>
        {mode === 'preflight' ? t('执行预检') : t('执行启动')}</button></div>
  </div>;
  const top: Record<string, ComponentChildren> = saved ? {
    input: <Note icon="input" title={t('按键设置')}><p>{t('游戏运行中按 F1 查看键位，按 F2 打开按键设置。')}</p></Note>,
    device: <Note icon="device" title="Android 4.4.4 · API 19"><p>{t('系统版本固定。CPU 核数和标称主频在下次启动生效，供游戏查询；主频不限制实际执行速度。')}{saved.reserved_visible && t('机型预设尚未接入，其他预留项仅保存配置。')}</p>
      {actions(<button class="mini" disabled>{t('ANDROID_ID 查看 / 重新生成')}<em>{t('管理接口待接入')}</em></button>)}</Note>,
    data: <Note icon="data" title={t('存档与目录')}><Paths rows={[...(item ? [[t('沙盒'), item.sandbox_path], [t('日志'), item.log_directory]] as [string, string][] : []), [t('游戏配置'), saved.config_path]]} />
      {actions(<button class="mini" disabled={busy} onClick={() => open('sandbox')}>{t('打开沙盒目录')}</button>, <button class="mini" disabled={busy} onClick={() => open('log')}>{t('打开日志目录')}</button>,
        <button class="mini" disabled={busy} onClick={() => open('external')}>{t('打开数据包目录')}</button>,
        saved.reserved_visible && <button class="mini" disabled>{t('导出 / 导入存档')}<em>{t('预留')}</em></button>,
        <button class="mini" disabled={busy || !item || item.running} title={item?.running ? t('游戏运行中，退出后才能清空数据。') : undefined}
          onClick={() => store.afterSave(clearData)}>{t('清空游戏数据…')}</button>)}</Note>,
    network: <Note icon="network" title={t('仅保存意向')} warn><p>{t('不改变运行时网络策略；“允许网络”是预留配置。')}</p></Note>,
    compatibility: <Note icon="compatibility" title={saved.profile_id ? t('精确 Profile · {id}', { id: saved.profile_id }) : t('兼容性目录')}>
      {Object.keys(saved.facts).length > 0 && <Paths rows={factRows(saved.facts)} />}</Note>,
  } : {};
  const reservedVisible = saved?.reserved_visible ?? true;
  const shown = new Set(shownFields(saved?.fields ?? [], reservedVisible).map(field => field.key));
  const resetKeys = Object.keys(draft).filter(key => shown.has(key) && !isInstanceOnlySetting(key));
  return <SettingsFrame skin={skin} art={item?.icon} tint={item ? color : undefined}
    crumb={<>{t('游戏设置')} · <b>{name}</b></>} idle={t('下次启动生效')} resetLabel={t('全部继承')}
    resetTitle={t('将 {n} 项改回继承？', { n: resetKeys.length })} resetKeys={resetKeys} busy={busy} saving={saving} loaded={!!saved}
    who={<div class="who">
      <span class="cart">{item && <TileCart api={cart3d} item={item} tone={tone} />}</span>
      <div><b>{name}</b><small>{id}</small>
        <span class="chip"><i class={`led ${tone}`} />{item ? versionLabel(item) : ''}{saved ? t(' · 已单独设置 {n} 项', { n: Object.keys(saved.values).filter(key => !isInstanceOnlySetting(key)).length }) : ''}</span></div>
    </div>}
    groupsTitle={t('游戏设置')} groups={gameGroups} group={group} setGroup={setGroup} fields={saved?.fields ?? []} reservedVisible={reservedVisible}
    sectionBefore={{ brand: t('设备身份 · 预留') }}
    model={field => {
      const overridden = Object.hasOwn(draft, field.key);
      return {
        value: overridden ? draft[field.key] : saved!.inherited[field.key], disabled: busy, confirmed: isInstanceOnlySetting(field.key),
        inherit: isInstanceOnlySetting(field.key) ? undefined : { overridden, source: t('全局'),
          shown: displayValue(field, saved!.inherited[field.key]) },
      };
    }}
    change={(field, value) => store.edit(current => ({ ...current, [field.key]: value }))}
    restoreInherit={field => { if (saved && !busy) store.edit(current => inheritSetting(current, field.key)); }}
    browse={(field, apply) => void run(async () => { const path = await pickPath('directory'); if (path) { if (apply) apply(path); else store.edit(current => ({ ...current, [field.key]: path })); } })}
    fileLabel={t('游戏配置')} filePath={saved?.config_path ?? ''} kicker={t('Game settings · 下次启动生效')} note={t('修改即时保存，下次启动生效')}
    top={top[group]} end={group === 'advanced' || group === 'compatibility' ? launch : undefined} error={store.error} toast={toast}
    back={() => { if (!busy) store.afterSave(onBack); }}
    reload={() => void run(async () => { await store.load(); say(t('已重新载入')); })}
    reset={() => store.edit(inheritAllSettings)} />;
}
