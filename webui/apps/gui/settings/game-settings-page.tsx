import { Fragment, type ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc, type LibraryItem } from '../shared/rpc';
import { pickPath } from '../import/import';
import { displayValue, factRows, overlayFields, shownFields } from './settings';
import { gameGroups, gameSettingsError, inheritAllSettings, inheritSetting, isInstanceOnlySetting, type GameSettings } from './game-settings';
import { statuses, versionLabel } from '../library/library';
import { useIconColor } from '../cartridge/cartridge';
import { TileCart } from '../cartridge/cartridge3d';
import type { Cart3D } from '../cartridge/cart3d/api';
import { Note, Paths, SettingsFrame, useAutoSave, useToast } from './settings-ui';
import { deviceDisplay, ScreenSizeNote, screenFields } from './screen-size';
import type { Skin } from '../kit';
import { t, uiText } from '../shared/i18n';
import { groupAvailable, has } from '../shared/features';

const modes = [['normal', 'settings.game.launchNormal'], ['preflight', 'settings.game.launchPreflight'], ['diagnostic', 'settings.game.launchDiagnostics']] as const;
// argv from the host, one flag per line; the page never assembles arguments itself.
function Argv({ argv }: { argv: string[] }) {
  return <pre class="term" aria-label={t('settings.game.argv')}>{argv.map((token, index) => <Fragment key={index}>
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
    <div class="launch-h"><b>{t('settings.game.launchPreview')}</b><span class="tag over">{t('settings.game.fromSaved')}</span>
      <div class="seg" role="radiogroup" aria-label={t('settings.game.launchMode')}>{modes.filter(([key]) => key === 'normal' || has('launch_modes')).map(([key, label]) =>
        <button key={key} role="radio" aria-checked={mode === key} class={mode === key ? 'on' : ''} onClick={() => setMode(key)}>{t(label)}</button>)}</div></div>
    {preview?.error ? <pre class="term" role="alert">{uiText(preview.error)}</pre> : <Argv argv={preview?.argv ?? []} />}
    <div class="launch-f"><span>{mode === 'preflight' ? t('settings.game.preflightNote') : t('settings.game.argvNote')}</span>
      <button class="pill main" disabled={busy || saving || !!preview?.error}
        onClick={() => void run(async () => { await rpc('library.launch', { installation_id: id, mode }); say(t('settings.game.launched')); })}>
        {mode === 'preflight' ? t('settings.game.runPreflight') : t('settings.game.runLaunch')}</button></div>
  </div>;
  const top: Record<string, ComponentChildren> = saved ? {
    input: <Note icon="input" title={t('common.keySettings')}><p>{t('settings.keysHint')}</p></Note>,
    device: <Note icon="device" title="Android 4.4.4 · API 19"><p>{t('settings.game.deviceNote')}{saved.reserved_visible && t('settings.game.presetNote')}</p>
      {actions(<button class="mini" disabled>{t('settings.game.androidId')}<em>{t('settings.game.managementPending')}</em></button>)}</Note>,
    data: <Note icon="data" title={t('settings.game.savesFolders')}><Paths rows={[...(item ? [[t('settings.game.sandbox'), item.sandbox_path], [t('settings.game.logs'), item.log_directory]] as [string, string][] : []), [t('settings.game.config'), saved.config_path]]} />
      {actions(...(has('open_directory') ? [<button class="mini" disabled={busy} onClick={() => open('sandbox')}>{t('common.openSandboxFolder')}</button>, <button class="mini" disabled={busy} onClick={() => open('log')}>{t('common.openLogFolder')}</button>,
        <button class="mini" disabled={busy} onClick={() => open('external')}>{t('settings.game.openDataFolder')}</button>] : []),
        saved.reserved_visible && <button class="mini" disabled>{t('settings.game.exportImportSaves')}<em>{t('settings.reservedTag')}</em></button>,
        <button class="mini" disabled={busy || !item || item.running} title={item?.running ? t('library.clear.running') : undefined}
          onClick={() => store.afterSave(clearData)}>{t('library.clear.menu')}</button>)}</Note>,
    network: <Note icon="network" title={t('settings.intentOnly')} warn><p>{t('settings.game.networkNote')}</p></Note>,
  } : {};
  // The compatibility profile this game resolved, beside the folder setting that chooses where profiles come from.
  const profile = saved && <Note icon="compatibility" title={saved.profile_id ? t('settings.game.exactProfile', { id: saved.profile_id }) : t('settings.game.compatFolders')}>
    {Object.keys(saved.facts).length > 0 && <Paths rows={factRows(saved.facts)} />}</Note>;
  const reservedVisible = saved?.reserved_visible ?? true;
  // This game's values over the global ones: what decides which screen rows show and the size they give.
  const effective = { ...saved?.inherited, ...draft }, display = deviceDisplay();
  const shown = new Set(shownFields(saved?.fields ?? [], reservedVisible).map(field => field.key));
  const resetKeys = Object.keys(draft).filter(key => shown.has(key) && !isInstanceOnlySetting(key));
  return <SettingsFrame skin={skin} art={item?.icon} tint={item ? color : undefined}
    crumb={<>{t('common.gameSettings')} · <b>{name}</b></>} idle={t('common.appliesNextLaunch')} resetLabel={t('settings.inheritAll')}
    resetTitle={t('settings.confirmInheritAll', { n: resetKeys.length })} resetKeys={resetKeys} busy={busy} saving={saving} loaded={!!saved}
    who={<div class="who">
      <span class="cart">{item && <TileCart api={cart3d} item={item} tone={tone} />}</span>
      <div><b>{name}</b><small>{id}</small>
        <span class="chip"><i class={`led ${tone}`} />{item ? versionLabel(item) : ''}{saved ? t('settings.customizedCount', { n: Object.keys(saved.values).filter(key => !isInstanceOnlySetting(key)).length }) : ''}</span></div>
    </div>}
    groupsTitle={t('common.gameSettings')} groups={gameGroups.filter(([key]) => groupAvailable(key))} group={group} setGroup={setGroup} fields={overlayFields(screenFields(saved?.fields ?? [], effective, display), effective)} reservedVisible={reservedVisible}
    sectionBefore={{ brand: t('settings.game.identityReserved') }}
    after={saved ? { screen_resolution: <ScreenSizeNote values={effective} display={display} /> } : undefined}
    model={field => {
      const overridden = Object.hasOwn(draft, field.key);
      return {
        value: overridden ? draft[field.key] : saved!.inherited[field.key], disabled: busy, confirmed: isInstanceOnlySetting(field.key),
        inherit: isInstanceOnlySetting(field.key) ? undefined : { overridden,
          shown: displayValue(field, saved!.inherited[field.key]), fromDefault: field.scope === 'game' },
      };
    }}
    change={(field, value) => store.edit(current => ({ ...current, [field.key]: value }))}
    restoreInherit={field => { if (saved && !busy) store.edit(current => inheritSetting(current, field.key)); }}
    browse={(field, apply) => void run(async () => { const path = await pickPath('directory'); if (path) { if (apply) apply(path); else store.edit(current => ({ ...current, [field.key]: path })); } })}
    fileLabel={t('settings.game.config')} filePath={saved?.config_path ?? ''} kicker={t('settings.game.crumb')} note={t('settings.savesInstantly')}
    top={top[group]} end={group === 'data' ? profile : group === 'developer' ? launch : undefined} error={store.error} toast={toast}
    back={() => { if (!busy) store.afterSave(onBack); }}
    reload={() => void run(async () => { await store.load(); say(t('settings.reloaded')); })}
    reset={() => store.edit(inheritAllSettings)} />;
}
