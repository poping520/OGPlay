import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { pickPath } from '../import/import';
import { factRows, groups, isLauncherGroup, overlayFields, settingsError, shownFields, type Settings } from './settings';
import { Note, Paths, SettingsFrame, useAutoSave, useToast } from './settings-ui';
import { IconPreview } from './icon-preview';
import { Credits } from './credits';
import { StarCard } from './star-card';
import { deviceDisplay, ScreenSizeNote, screenFields } from './screen-size';
import { groupAvailable, has } from '../shared/features';
import { skinOf } from '../kit';
import { resolvedLanguage, setLanguage, t } from '../shared/i18n';

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
  useEffect(() => { if (saved) setLanguage(resolvedLanguage(draft.language ?? saved.values.language, saved.system_language)); }, [draft.language, saved]);
  useEffect(() => () => { const latest = store.latest(); if (latest) setLanguage(resolvedLanguage(latest.values.language, latest.system_language)); }, []);
  const openLibrary = () => void run(async () => { await rpc('settings.open_dir', { kind: 'library' }); });
  const actions = (...buttons: ComponentChildren[]) => <div class="acts">{buttons}</div>;
  // The version is shown on its own line; the other facts stay folded.
  const { ogplay_version: _version, ...facts } = saved?.facts ?? {};
  const top: Record<string, ComponentChildren> = saved ? {
    general: <Note icon="info" title={t('settings.global.keepRunning')}><p>{t('settings.global.keepRunningNote')}</p></Note>,
    input: <Note icon="input" title={t('common.keySettings')}><p>{t('settings.keysHint')}</p></Note>,
    data: <Note icon="data" title={t('common.library')}><Paths rows={[[t('settings.global.libraryRoot'), saved.library_root], [t('settings.global.config'), saved.config_path], [t('settings.global.sandboxUsage'), t('settings.global.notMeasured')], ['FFmpeg', t('settings.global.notProbed')]]} />
      {actions(has('open_directory') && <button class="mini" disabled={busy} onClick={openLibrary}>{t('settings.global.openLibrary')}</button>, <button class="mini" disabled>{t('settings.global.cleanSandboxes')}<em>{t('settings.notAvailableYet')}</em></button>)}</Note>,
    device: <Note icon="device" title="Android 4.4.4 · API 19"><p>{t('settings.game.deviceNote')}{saved.reserved_visible && t('settings.game.presetNote')}</p></Note>,
    network: <Note icon="network" title={t('settings.intentOnly')} warn><p>{t('settings.global.networkNote')}</p></Note>,
    about: <><StarCard onError={setError} /><Note icon="about" title="OGPlay"><p>{t('settings.global.about')}</p>
      <Paths rows={[[t('common.version'), saved.facts.ogplay_version ?? t('common.unknown')]]} />
      {factRows(facts).map(([label, value]) => <details key={label} class="fact"><summary>{label}</summary><pre>{value}</pre></details>)}
      {actions(<button class="mini" disabled>{t('settings.global.exportDiagnostics')}<em>{t('settings.notAvailableYet')}</em></button>)}</Note>
      <Credits /></>,
  } : {};
  const end: Record<string, ComponentChildren> = {
    audio: <Note icon="audio"><p>{t('settings.global.audioNote')}</p></Note>,
    diagnostics: <Note icon="diagnostics"><p>{t('settings.global.logsNote')}</p>{has('open_directory') && actions(<button class="mini" disabled={busy} onClick={openLibrary}>{t('settings.global.openLogsLibrary')}</button>)}</Note>,
    developer: <Note icon="developer"><p>{t('settings.global.mcpNote')}</p></Note>,
  };
  // The style setting previews while it is still a draft.
  const skin = skinOf(draft.settings_style ?? saved?.values.settings_style);
  const display = deviceDisplay();
  const resetKeys = saved ? shownFields(saved.fields, saved.reserved_visible).filter(field => draft[field.key] !== field.initial).map(field => field.key) : [];
  return <SettingsFrame skin={skin} crumb={<b>{t('settings.global.title')}</b>} idle={t('settings.global.subtitle')} resetLabel={t('settings.restoreDefaults')}
    resetTitle={t('settings.confirmRestoreDefaults', { n: resetKeys.length })} resetKeys={resetKeys} busy={busy} saving={store.saving} loaded={!!saved}
    groupsTitle={t('settings.global.title')} groups={groups.filter(([key]) => groupAvailable(key))} group={group} setGroup={setGroup} fields={overlayFields(screenFields(saved?.fields ?? [], draft, display), draft)} reservedVisible={saved?.reserved_visible ?? true}
    model={field => ({ value: draft[field.key] ?? field.initial, disabled: busy })}
    change={(field, value) => store.edit(current => ({ ...current, [field.key]: value }))}
    browse={field => void run(async () => { const path = await pickPath('directory'); if (path) store.edit(current => ({ ...current, [field.key]: path })); })}
    fileLabel={t('settings.global.configFile')} filePath={saved?.config_path ?? ''} kicker={t('settings.global.crumb')} note={saved?.reserved_visible === false ? '' : t('settings.global.reservedNote')}
    top={<>{!isLauncherGroup(group) && <p class="scope-note">{t('settings.global.sharedGroup')}</p>}{top[group]}</>} end={end[group]} error={store.error} toast={toast}
    sectionBefore={{ brand: t('settings.game.identityReserved') }}
    after={saved ? { running_icon: <IconPreview value={String(draft.running_icon ?? saved.values.running_icon)} disabled={busy}
      choose={value => store.edit(current => ({ ...current, running_icon: value }))} />,
      screen_resolution: <ScreenSizeNote values={draft} display={display} /> } : undefined}
    back={() => { if (!busy) store.afterSave(onBack); }}
    reload={() => void run(async () => { await store.load(); say(t('settings.reloaded')); })}
    reset={() => { if (saved) store.edit(() => Object.fromEntries(saved.fields.map(field => [field.key, field.initial]))); }} />;
}
