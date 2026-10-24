import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc, type LibraryItem } from './rpc';
import { pickPath } from './import';
import { changedKeys, displayValue, type SettingValue } from './settings';
import { gameGroups, gameSettingsError, inheritSetting, type GameSettings } from './game-settings';
import { statuses, versionLabel } from './library';
import { useIconColor } from './cartridge';
import { TileCart } from './cartridge3d';
import type { Cart3D } from './cart3d/api';
import { Note, Paths, SettingsFrame, type Skin, useToast } from './settings-ui';
import { t } from './i18n';

const modes = [['normal', '正常启动'], ['preflight', '预检'], ['diagnostic', '带诊断启动']] as const;
// argv from the host, one flag per line; the page never assembles arguments itself.
function Argv({ argv }: { argv: string[] }) {
  return <pre class="term" aria-label={t('生成的 argv')}>{argv.map((token, index) => <Fragment key={index}>
    {index > 0 && (token.startsWith('--') ? ' \\\n  ' : ' ')}
    <span class={index === 0 ? 'p' : token.startsWith('--') ? 'f' : ''}>{token}</span>
  </Fragment>)}</pre>;
}

export function GameSettingsPage({ id, item, skin, cart3d, onBack }: {
  id: string; item?: LibraryItem; skin: Skin; cart3d: Cart3D | 'waiting' | null; onBack: () => void;
}) {
  const [saved, setSaved] = useState<GameSettings | null>(null);
  const [draft, setDraft] = useState<Record<string, SettingValue>>({});
  const [group, setGroup] = useState<string>('performance');
  const [mode, setMode] = useState<string>('normal');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [discard, setDiscard] = useState<'back' | 'reload' | null>(null);
  const [toast, say] = useToast();
  const lock = useRef(false);
  const color = useIconColor(item?.icon ?? '');
  const changed = saved ? changedKeys(saved.values, draft) : [];
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  const accept = (value: GameSettings) => { setSaved(value); setDraft({ ...value.values }); };
  const load = async () => accept(await rpc<GameSettings>('game_settings.get', { installation_id: id }));
  useEffect(() => { void run(load); }, [id]);
  const leave = (action: 'back' | 'reload') => {
    if (busy) return;
    if (changed.length) setDiscard(action);
    else if (action === 'back') onBack();
    else void run(async () => { await load(); say(t('已重新载入')); });
  };
  const open = (kind: string) => void run(async () => { await rpc('library.open_dir', { installation_id: id, kind }); });
  const name = saved?.display_name ?? item?.display_name ?? id;
  const tone = item?.running ? 'running' : statuses[item?.status ?? '']?.tone ?? 'warning';
  const dirty = changed.length > 0;
  const preview = saved?.previews[mode];
  const actions = (...buttons: ComponentChildren[]) => <div class="acts">{buttons}</div>;
  const launch = <div class="launch">
    <div class="launch-h"><b>{t('启动与参数预览')}</b><span class={`tag ${dirty ? 'pending' : 'over'}`}>{dirty ? t('有未保存修改 · 预览为已保存设置') : t('来自已保存设置')}</span>
      <div class="seg" role="radiogroup" aria-label={t('启动模式')}>{modes.map(([key, label]) =>
        <button key={key} role="radio" aria-checked={mode === key} class={mode === key ? 'on' : ''} onClick={() => setMode(key)}>{t(label)}</button>)}</div></div>
    {preview?.error ? <pre class="term" role="alert">{preview.error}</pre> : <Argv argv={preview?.argv ?? []} />}
    <div class="launch-f"><span>{mode === 'preflight' ? t('预检不会启用 MCP，结果写入游戏日志。') : t('参数由宿主生成，前端不拼接自由参数。')}</span>
      <button class="pill main" disabled={busy || dirty || !!preview?.error} title={dirty ? t('先保存设置') : undefined}
        onClick={() => void run(async () => { await rpc('library.launch', { installation_id: id, mode }); say(t('已启动游戏进程；预检结果与诊断信息见游戏日志')); })}>
        {mode === 'preflight' ? t('执行预检') : t('执行启动')}</button></div>
  </div>;
  const top: Record<string, ComponentChildren> = saved ? {
    device: <Note icon="device" title="Android 4.4.4 · API 19"><p>{t('系统版本固定。CPU 核数和标称主频在下次启动生效，供游戏查询；主频不限制实际执行速度。机型预设尚未接入，其他预留项仅保存配置。')}</p>
      {actions(<button class="mini" disabled>{t('ANDROID_ID 查看 / 重新生成')}<em>{t('管理接口待接入')}</em></button>)}</Note>,
    data: <Note icon="data" title={t('存档与目录')}><Paths rows={[...(item ? [[t('沙盒'), item.sandbox_path], [t('日志'), item.log_directory]] as [string, string][] : []), [t('游戏配置'), saved.config_path]]} />
      {actions(<button class="mini" disabled={busy} onClick={() => open('sandbox')}>{t('打开沙盒目录')}</button>, <button class="mini" disabled={busy} onClick={() => open('log')}>{t('打开日志目录')}</button>,
        <button class="mini" disabled={busy} onClick={() => open('external')}>{t('打开数据包目录')}</button>, <button class="mini" disabled>{t('导出 / 导入存档')}<em>{t('预留')}</em></button>,
        <button class="mini" disabled>{t('重置沙盒')}<em>{t('暂未开放')}</em></button>)}</Note>,
    network: <Note icon="network" title={t('仅保存意向')} warn><p>{t('不改变运行时网络策略；“允许网络”是预留配置。')}</p></Note>,
    compatibility: <Note icon="compatibility" title={saved.profile_id ? t('精确 Profile · {id}', { id: saved.profile_id }) : t('兼容性目录')}>
      {Object.keys(saved.facts).length > 0 && <Paths rows={Object.entries(saved.facts).map(([key, value]) => [t(key), value] as [string, string])} />}</Note>,
  } : {};
  return <SettingsFrame skin={skin} art={item?.icon} tint={item ? color : undefined}
    crumb={<>{t('游戏设置')} · <b>{name}</b></>} idle={t('下次启动生效')} resetLabel={t('全部继承')} busy={busy} loaded={!!saved} changed={changed}
    who={<div class="who">
      <span class="cart">{item && <TileCart api={cart3d} item={item} tone={tone} />}</span>
      <div><b>{name}</b><small>{id}</small>
        <span class="chip"><i class={`led ${tone}`} />{item ? versionLabel(item) : ''}{saved ? t(' · 已覆盖 {n} 项', { n: Object.keys(saved.values).length }) : ''}</span></div>
    </div>}
    groupsTitle={t('游戏设置')} groups={gameGroups} group={group} setGroup={setGroup} fields={saved?.fields ?? []}
    sectionBefore={{ brand: t('设备身份 · 预留') }}
    model={field => {
      const overridden = Object.hasOwn(draft, field.key);
      return {
        value: overridden ? draft[field.key] : saved!.inherited[field.key], changed: changed.includes(field.key), disabled: busy,
        inherit: { overridden, saved: Object.hasOwn(saved!.values, field.key), source: ['external_dir', 'external_guest_dir'].includes(field.key) ? t('导入记录') : t('全局'),
          shown: displayValue(field, saved!.inherited[field.key]) },
      };
    }}
    change={(field, value) => setDraft(current => ({ ...current, [field.key]: value }))}
    toggleInherit={field => { if (!saved || busy) return; setDraft(current => Object.hasOwn(current, field.key) ? inheritSetting(current, field.key) : { ...current, [field.key]: saved.inherited[field.key] }); }}
    browse={field => void run(async () => { const path = await pickPath('directory'); if (path) setDraft(current => ({ ...current, [field.key]: path })); })}
    fileLabel={t('游戏配置')} filePath={saved?.config_path ?? ''} kicker={t('Game settings · 下次启动生效')} note={t('运行中可保存，下次启动生效')}
    top={top[group]} end={group === 'advanced' || group === 'compatibility' ? launch : undefined} error={error} toast={toast}
    discard={discard} keep={() => setDiscard(null)} confirmDiscard={() => {
      const action = discard; setDiscard(null);
      if (action === 'back') onBack(); else void run(async () => { await load(); say(t('已重新载入')); });
    }}
    back={() => leave('back')} reload={() => leave('reload')}
    reset={() => { if (!saved || busy) return; setDraft({}); say(t('已恢复继承，保存后生效')); }}
    save={() => { if (!saved || busy || !dirty) return; void run(async () => {
      const invalid = gameSettingsError(saved, draft); if (invalid) throw new Error(invalid);
      accept(await rpc<GameSettings>('game_settings.set', { installation_id: id, revision: saved.revision, values: draft }));
      say(t('游戏设置已保存 · 下次启动生效'));
    }); }} />;
}
