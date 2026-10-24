import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from './rpc';
import { pickPath } from './import';
import { changedKeys, groups, settingsError, type SettingValue, type Settings } from './settings';
import { Note, Paths, SettingsFrame, useToast } from './settings-ui';
import { setLanguage, t } from './i18n';

export function SettingsPage({ onBack, onSaved }: { onBack: () => void; onSaved: (settings: Settings) => void }) {
  const [saved, setSaved] = useState<Settings | null>(null);
  const [draft, setDraft] = useState<Record<string, SettingValue>>({});
  const [group, setGroup] = useState<string>('general');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [discard, setDiscard] = useState<'back' | 'reload' | null>(null);
  const [toast, say] = useToast();
  const lock = useRef(false);
  const changed = saved ? changedKeys(saved.values, draft) : [];
  const run = async (action: () => Promise<void>) => {
    if (lock.current) return;
    lock.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { lock.current = false; setBusy(false); }
  };
  const load = async () => { const value = await rpc<Settings>('settings.get'); setSaved(value); setDraft({ ...value.values }); onSaved(value); };
  useEffect(() => { void run(load); }, []);
  // Language previews while it is a draft, like the style; leaving without saving restores the saved one.
  const savedLanguage = useRef<unknown>(undefined);
  savedLanguage.current = saved?.values.language;
  useEffect(() => { if (saved) setLanguage(draft.language ?? saved.values.language); }, [draft.language, saved]);
  useEffect(() => () => { if (savedLanguage.current !== undefined) setLanguage(savedLanguage.current); }, []);
  const leave = (action: 'back' | 'reload') => {
    if (busy) return;
    if (changed.length) setDiscard(action);
    else if (action === 'back') onBack();
    else void run(async () => { await load(); say(t('已重新载入')); });
  };
  const openLibrary = () => void run(async () => { await rpc('settings.open_dir', { kind: 'library' }); });
  const actions = (...buttons: ComponentChildren[]) => <div class="acts">{buttons}</div>;
  const top: Record<string, ComponentChildren> = saved ? {
    general: <Note icon="info" title={t('关闭启动器后，游戏继续运行')}><p>{t('始终开启。启动器只负责导入与启动，游戏是独立进程。')}</p></Note>,
    storage: <Note icon="storage" title={t('游戏库')}><Paths rows={[[t('库根'), saved.library_root], [t('配置'), saved.config_path], [t('沙盒占用'), t('尚未统计')], ['FFmpeg', t('尚未探测')]]} />
      {actions(<button class="mini" disabled={busy} onClick={openLibrary}>{t('打开库目录')}</button>, <button class="mini" disabled>{t('清理沙盒')}<em>{t('暂未开放')}</em></button>)}</Note>,
    network: <Note icon="network" title={t('仅保存意向')} warn><p>{t('不改变当前运行时策略。回环仅指本机地址；公网能力以运行时契约为准。')}</p></Note>,
    about: <Note icon="about" title="OGPlay"><p>{t('安卓老游戏兼容层。版本及制品哈希以随程序交付的清单为准；清单缺失时不推测。')}</p>
      {Object.entries(saved.facts).map(([key, value]) => <details key={key} class="fact"><summary>{key}</summary><pre>{value}</pre></details>)}
      {actions(<button class="mini" disabled>{t('导出诊断包')}<em>{t('暂未开放')}</em></button>)}</Note>,
  } : {};
  const end: Record<string, ComponentChildren> = {
    audio: <Note icon="audio"><p>{t('采样率与块大小：启动器不创建音频设备；当前无运行时探测数据。')}</p></Note>,
    diagnostics: <Note icon="diagnostics"><p>{t('每个实例的日志写入其目录下的 last-run.log。')}</p>{actions(<button class="mini" disabled={busy} onClick={openLibrary}>{t('打开日志所在库目录')}</button>)}</Note>,
    control: <Note icon="control"><p>{t('MCP 只监听本机。固定端口被其他实例占用时启动失败；端口范围自动分配待后续接入。')}</p></Note>,
  };
  // The style setting previews while it is still a draft.
  const skin = (draft.settings_style ?? saved?.values.settings_style) === 'glass' ? 'glass' : 'console';
  return <SettingsFrame skin={skin} crumb={<b>{t('全局设置')}</b>} idle={t('影响之后启动的游戏')} resetLabel={t('恢复默认')} busy={busy} loaded={!!saved} changed={changed}
    groupsTitle={t('全局设置')} groups={groups} group={group} setGroup={setGroup} fields={saved?.fields ?? []}
    model={field => ({ value: draft[field.key] ?? field.initial, changed: changed.includes(field.key), disabled: busy })}
    change={(field, value) => setDraft(current => ({ ...current, [field.key]: value }))}
    browse={field => void run(async () => { const path = await pickPath('directory'); if (path) setDraft(current => ({ ...current, [field.key]: path })); })}
    fileLabel={t('配置文件')} filePath={saved?.config_path ?? ''} kicker={t('Settings · 全局')} note={t('预留项只保存配置，不改变运行时')}
    top={top[group]} end={end[group]} error={error} toast={toast}
    discard={discard} keep={() => setDiscard(null)} confirmDiscard={() => {
      const action = discard; setDiscard(null);
      if (action === 'back') onBack(); else void run(async () => { await load(); say(t('已重新载入')); });
    }}
    back={() => leave('back')} reload={() => leave('reload')}
    reset={() => { if (!saved || busy) return; setDraft(Object.fromEntries(saved.fields.map(field => [field.key, field.initial]))); say(t('默认值已填入，保存后生效')); }}
    save={() => { if (!saved || busy || !changed.length) return; void run(async () => {
      const invalid = settingsError(saved.fields, draft); if (invalid) throw new Error(invalid);
      const value = await rpc<Settings>('settings.set', { revision: saved.revision, values: draft });
      setSaved(value); setDraft({ ...value.values }); onSaved(value); say(t('设置已保存'));
    }); }} />;
}
