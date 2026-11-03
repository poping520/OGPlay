import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { pickPath } from '../import/import';
import { factRows, groups, settingsError, shownFields, type Settings } from './settings';
import { Note, Paths, SettingsFrame, useAutoSave, useToast } from './settings-ui';
import { IconPreview } from './icon-preview';
import { Credits } from './credits';
import { skinOf } from '../kit';
import { setLanguage, t } from '../shared/i18n';

export function SettingsPage({ onBack, onSaved }: { onBack: () => void; onSaved: (settings: Settings) => void }) {
  const [group, setGroup] = useState<string>('general');
  const [busy, setBusy] = useState(false);
  const [toast, say] = useToast();
  const lock = useRef(false);
  const store = useAutoSave<Settings>({
    load: () => rpc<Settings>('settings.get'),
    write: (saved, values) => rpc<Settings>('settings.set', { revision: saved.revision, values }),
    check: (saved, values) => settingsError(saved.fields, values),
    accepted: onSaved,
  });
  const { saved, draft, setError } = store;
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  useEffect(() => { void run(store.load); }, []);
  // Language follows the page at once; if a write never lands, leaving restores the saved one.
  useEffect(() => { if (saved) setLanguage(draft.language ?? saved.values.language); }, [draft.language, saved]);
  useEffect(() => () => { const language = store.latest()?.values.language; if (language !== undefined) setLanguage(language); }, []);
  const openLibrary = () => void run(async () => { await rpc('settings.open_dir', { kind: 'library' }); });
  const actions = (...buttons: ComponentChildren[]) => <div class="acts">{buttons}</div>;
  // The version is shown on its own line; the other facts stay folded.
  const { ogplay_version: _version, ...facts } = saved?.facts ?? {};
  const top: Record<string, ComponentChildren> = saved ? {
    general: <Note icon="info" title={t('关闭启动器后，游戏继续运行')}><p>{t('始终开启。启动器只负责导入与启动，游戏是独立进程。')}</p></Note>,
    input: <Note icon="input" title={t('按键设置')}><p>{t('游戏运行中按 F1 查看键位，按 F2 打开按键设置。')}</p></Note>,
    storage: <Note icon="storage" title={t('游戏库')}><Paths rows={[[t('库根'), saved.library_root], [t('配置'), saved.config_path], [t('沙盒占用'), t('尚未统计')], ['FFmpeg', t('尚未探测')]]} />
      {actions(<button class="mini" disabled={busy} onClick={openLibrary}>{t('打开库目录')}</button>, <button class="mini" disabled>{t('清理沙盒')}<em>{t('暂未开放')}</em></button>)}</Note>,
    network: <Note icon="network" title={t('仅保存意向')} warn><p>{t('不改变当前运行时策略。回环仅指本机地址；公网能力以运行时契约为准。')}</p></Note>,
    about: <><Note icon="about" title="OGPlay"><p>{t('安卓老游戏兼容层。版本及制品哈希以随程序交付的清单为准；清单缺失时不推测。')}</p>
      <Paths rows={[[t('版本'), saved.facts.ogplay_version ?? t('未知')]]} />
      {factRows(facts).map(([label, value]) => <details key={label} class="fact"><summary>{label}</summary><pre>{value}</pre></details>)}
      {actions(<button class="mini" disabled>{t('导出诊断包')}<em>{t('暂未开放')}</em></button>)}</Note>
      <Credits /></>,
  } : {};
  const end: Record<string, ComponentChildren> = {
    audio: <Note icon="audio"><p>{t('采样率与块大小：启动器不创建音频设备；当前无运行时探测数据。')}</p></Note>,
    diagnostics: <Note icon="diagnostics"><p>{t('每个游戏的日志写入其目录下的 last-run.log。')}</p>{actions(<button class="mini" disabled={busy} onClick={openLibrary}>{t('打开日志所在库目录')}</button>)}</Note>,
    control: <Note icon="control"><p>{t('MCP 只监听本机。固定端口被其他游戏占用时启动失败；端口范围自动分配待后续接入。')}</p></Note>,
  };
  // The style setting previews while it is still a draft.
  const skin = skinOf(draft.settings_style ?? saved?.values.settings_style);
  const resetKeys = saved ? shownFields(saved.fields, saved.reserved_visible).filter(field => draft[field.key] !== field.initial).map(field => field.key) : [];
  return <SettingsFrame skin={skin} crumb={<b>{t('全局设置')}</b>} idle={t('影响之后启动的游戏')} resetLabel={t('恢复默认')}
    resetTitle={t('将 {n} 项恢复为默认值？', { n: resetKeys.length })} resetKeys={resetKeys} busy={busy} saving={store.saving} loaded={!!saved}
    groupsTitle={t('全局设置')} groups={groups} group={group} setGroup={setGroup} fields={saved?.fields ?? []} reservedVisible={saved?.reserved_visible ?? true}
    model={field => ({ value: draft[field.key] ?? field.initial, disabled: busy })}
    change={(field, value) => store.edit(current => ({ ...current, [field.key]: value }))}
    browse={field => void run(async () => { const path = await pickPath('directory'); if (path) store.edit(current => ({ ...current, [field.key]: path })); })}
    fileLabel={t('配置文件')} filePath={saved?.config_path ?? ''} kicker={t('Settings · 全局')} note={saved?.reserved_visible === false ? '' : t('预留项只保存配置，不改变运行时')}
    top={top[group]} end={end[group]} error={store.error} toast={toast}
    after={saved ? { running_icon: <IconPreview value={String(draft.running_icon ?? saved.values.running_icon)} disabled={busy}
      choose={value => store.edit(current => ({ ...current, running_icon: value }))} /> } : undefined}
    back={() => { if (!busy) store.afterSave(onBack); }}
    reload={() => void run(async () => { await store.load(); say(t('已重新载入')); })}
    reset={() => { if (saved) store.edit(() => Object.fromEntries(saved.fields.map(field => [field.key, field.initial]))); }} />;
}
