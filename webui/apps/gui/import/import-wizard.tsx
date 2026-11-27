import { useEffect, useRef, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { archivePreview, formatBytes, guestPathError, importingStage, normalizeGuestPath, pause, pickSource, upload, type ArchiveData, type DataPack, type ExternalMode, type ImportJob } from './import';
import { MountPreview } from './mount-preview';
import { gameName, t, uiText } from '../shared/i18n';
import { has } from '../shared/features';

// The target choice for another copy; installation ids never start with a space.
const NEW_INSTANCE = ' new';

const MODES: { value: ExternalMode; label: string }[] = [
  { value: 'copy', label: 'import.mode.copy' },
  { value: 'link', label: 'import.mode.link' },
];

export function ImportWizard({ file, defaultExternal = '', onClose, onImported }: {
  file: File | null; defaultExternal?: string; onClose: () => void; onImported: (id: string) => Promise<void>;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const activeJob = useRef('');
  const locked = useRef(false);
  const disposed = useRef(false);
  const [job, setJob] = useState<ImportJob | null>(null);
  const [stage, setStage] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [externalGuest, setExternalGuest] = useState('/sdcard');
  // Typed mount positions win over the one inferred from the picked directory.
  const [guestEdited, setGuestEdited] = useState(false);
  const [externalMode, setExternalMode] = useState<ExternalMode>('copy');
  const [committing, setCommitting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  // Null uses the first matching instance by default; the user may choose another target or a new copy.
  const [targetOverride, setTarget] = useState<string | null>(null);
  const [source, setSource] = useState(file?.name ?? '');
  const [label, setLabel] = useState(file?.name ?? '');
  // Archive parts of no known use the user chose to bring along (mounted under /sdcard).
  const [extras, setExtras] = useState<string[]>([]);
  // A data pack picked apart from the APK (directory or archive), as the host planned it; its data and extras stand
  // in for the APK archive's.
  const [dataPack, setDataPack] = useState<(DataPack & { label: string }) | null>(null);
  const summary = job?.summary;
  const archive = summary?.archive ?? null;
  // The data pack comes out of the APK's archive (recognized, or extras chosen); otherwise one may be picked.
  const archiveData = !!archive && !dataPack && (!!archive.data || extras.length > 0);
  const linking = dataPack?.kind === 'directory' && externalMode === 'link';
  const withData = archiveData || !!dataPack;
  const guest = normalizeGuestPath(externalGuest);
  const guestError = withData ? guestPathError(guest) : null;
  const message = (reason: unknown) => reason instanceof Error ? reason.message : String(reason);
  async function poll(result: ImportJob, onPoll?: (job: ImportJob) => void): Promise<ImportJob> {
    while (result.state === 'analyzing' || result.state === 'importing') {
      onPoll?.(result);
      await pause();
      if (disposed.current) throw new Error(t('import.wizardClosed'));
      result = await rpc<ImportJob>('library.job.poll', { job: result.job });
    }
    if (result.state === 'failed') {
      setJob(result);
      setStage(importingStage(result));
      throw new Error(result.failure ? uiText(result.failure) : t('import.failed'));
    }
    return result;
  }
  async function run(action: () => Promise<void>) {
    if (locked.current) return;
    locked.current = true; setBusy(true); setError('');
    try { await action(); } catch (reason) { if (!disposed.current) setError(message(reason)); }
    finally { locked.current = false; if (!disposed.current) setBusy(false); }
  }
  // The wizard shows once there is an APK to read: cancelling the first pick closes it unseen, cancelling a
  // later one keeps what was analyzed.
  async function analyze(dropped: File | null) {
    await run(async () => {
      const picked = dropped ? null : await pickSource('file');
      if (!dropped && !picked) {
        if (!dialog.current?.open) onClose();
        return;
      }
      if (!dialog.current?.open) dialog.current?.showModal();
      if (activeJob.current) await rpc('library.job.cancel', { job: activeJob.current });
      activeJob.current = ''; setJob(null); setTarget(null); setExternalGuest('/sdcard'); setGuestEdited(false); setExternalMode('copy');
      setExtras([]); setDataPack(null);
      let initial: ImportJob;
      if (dropped) {
        setStage(t('import.readingDropped'));
        initial = await upload(dropped, id => { activeJob.current = id; }, value => setStage(t('import.readingApk', { percent: value })));
      } else {
        setSource(picked!.path); setLabel(picked!.label); setStage(t('import.analyzingPackage'));
        initial = await rpc<ImportJob>('library.analyze', { path: picked!.path });
        activeJob.current = initial.job;
      }
      setStage(t('import.analyzingApk'));
      const ready = await poll(initial);
      setJob(ready); setStage('');
      // The default data directory is offered once the package is known; failing to read it does not stop the import.
      if (defaultExternal && !ready.summary?.archive?.data) {
        try { await attach(defaultExternal, defaultExternal); }
        catch (reason) { if (!disposed.current) setError(message(reason)); }
      }
    });
  }
  // Another APK from the same archive: analyzed afresh, the archive's data plan follows its package.
  async function chooseApk(apk: string) {
    await run(async () => {
      if (activeJob.current) await rpc('library.job.cancel', { job: activeJob.current });
      activeJob.current = ''; setJob(null); setTarget(null); setExtras([]); setDataPack(null); setGuestEdited(false);
      setStage(t('import.analyzingPackage'));
      const initial = await rpc<ImportJob>('library.analyze', { path: source, apk });
      activeJob.current = initial.job;
      setJob(await poll(initial)); setStage('');
    });
  }
  async function close() {
    if (locked.current) return;
    await run(async () => {
      if (activeJob.current && job?.state !== 'imported') await rpc('library.job.cancel', { job: activeJob.current });
      onClose();
    });
  }
  // Copying a data pack can take a while; the host stops it before publication.
  async function cancelImport() {
    if (!activeJob.current || cancelling) return;
    setCancelling(true);
    try { await rpc('library.job.cancel', { job: activeJob.current }); }
    catch (reason) { if (!disposed.current) setError(message(reason)); }
  }
  useEffect(() => {
    void analyze(file);
    return () => { disposed.current = true; dialog.current?.close(); };
  }, []);
  // The mount is the host's plan, or /sdcard once extras come along, until the user types another.
  const planned = dataPack ?? archive;
  useEffect(() => {
    if (planned && !guestEdited) setExternalGuest(extras.length ? '/sdcard' : planned.data?.guest_dir ?? '/sdcard');
  }, [planned, extras, guestEdited]);
  // The host plans the pick for the analyzed package; with nothing recognized in an archive, all of it comes along.
  async function attach(path: string, shown: string) {
    const found = await rpc<DataPack>('library.data_pack', { job: activeJob.current, path });
    setGuestEdited(false); setExternalMode('copy');
    setExtras(found.data ? [] : found.extras.map(extra => extra.name));
    setDataPack({ ...found, label: shown });
  }
  async function pickData(kind: 'directory' | 'file') {
    await run(async () => {
      setStage(kind === 'directory' ? t('import.readingDataFolder') : t('import.readingDataArchive'));
      try {
        const picked = await pickSource(kind);
        if (picked) await attach(picked.path, picked.label);
      } finally { if (!disposed.current) setStage(''); }
    });
  }
  async function clearData() {
    await run(async () => {
      await rpc('library.data_pack', { job: activeJob.current });
      setDataPack(null); setExtras([]); setGuestEdited(false); setExternalMode('copy');
    });
  }
  const existing = summary?.existing ?? [];
  const target = targetOverride ?? existing[0]?.installation_id ?? NEW_INSTANCE;
  const replacing = existing.length > 0 && target !== NEW_INSTANCE;
  const replacementNote = t(withData ? 'import.replaceWithData' : 'import.replaceKeepsData');
  const canImport = summary && job?.state === 'ready' && !guestError &&
    (!dataPack || !!dataPack.data || extras.length > 0);
  // What the host made of the picked data pack, shown above its mount.
  const packNote = (pack: DataPack): [string, string] => {
    if (pack.kind === 'archive')
      return pack.data ? ['', t('import.dataFoundInArchive')] : ['', t('import.noDataAllToSdcard')];
    if (!pack.package) return ['warn', t('import.noPackageFolder', { package: summary?.package ?? '' })];
    if (pack.package.toLowerCase() !== summary?.package.toLowerCase())
      return ['warn', t('import.packageFolderDiffers', { found: pack.package })];
    return ['', t('import.packageFolderFound', { package: pack.package })];
  };
  const extrasList = (from: ArchiveData, caption: string) => from.extras.length > 0 && <div class="iw-extras">
    <div class="iw-title"><b>{t('import.otherContents')}</b><small>{t(caption)}</small></div>
    {from.extras.map(extra => <label key={extra.name} class="iw-check">
      <input type="checkbox" checked={extras.includes(extra.name)} disabled={busy}
        onChange={event => { const on = event.currentTarget.checked; setExtras(list => on ? [...list, extra.name] : list.filter(name => name !== extra.name)); }} />
      <span><code>{extra.name}</code>{t('import.dataFileCount', { n: extra.files, size: formatBytes(extra.bytes) })}</span></label>)}
  </div>;
  const mountRow = <>
    <div class="iw-row"><span>{t('common.mountAt')}</span>
      <label class={guestError ? 'iw-txt bad' : 'iw-txt'}><input aria-label={t('import.dataMountLocation')} value={externalGuest} disabled={busy} spellcheck={false}
        onInput={event => { setExternalGuest(event.currentTarget.value); setGuestEdited(true); }} /></label>
    </div>
    {guestError && <p class="iw-note bad">{guestError}</p>}
  </>;
  const chosenLabel = dataPack?.label ?? '';
  const led = error || job?.state === 'failed' ? 'danger' : committing || busy ? 'running' : summary ? 'ready' : '';
  const sourceName = label.split(/[\\/]/).pop();
  return <dialog ref={dialog} class="import-wizard" aria-label={t('common.importGame')} onCancel={event => { event.preventDefault(); void close(); }}>
    <header class="iw-head"><h2>{t('common.importGame')}</h2>{stage && <span class="iw-stage" role="status"><i class={`led ${led}`} />{stage}</span>}</header>
    <div class="iw-source">
      <p class="iw-file" title={label}><bdi>{sourceName}</bdi></p>
      <button disabled={busy} aria-label={t('import.chooseAnotherApk')} onClick={() => void analyze(null)}>{source ? t('import.change') : t('common.chooseApk')}</button>
    </div>
    {summary && <>
      <section class="iw-game">
        {summary.icon ? <img src={summary.icon} alt="" /> : <span class="iw-icon">?</span>}
        <div>
          <h3>{gameName(summary)}</h3>
          <p class="mono">{summary.package}</p>
          <ul class="iw-tags">
            <li>{summary.version_name || t('common.version')} ({summary.version_code})</li>
            <li>API {summary.min_sdk ?? '?'}{summary.target_sdk ? ` → ${summary.target_sdk}` : ''}</li>
            <li>{summary.abis.join(' / ') || t('import.noNativeLibraries')}</li>
            {summary.profile && <li>Profile {summary.profile}</li>}
          </ul>
        </div>
      </section>
      {archive && <section class="iw-data iw-archive">
        {archive.apks.length > 1 && <>
          <div class="iw-title"><b>{t('import.package')}</b><small>{t('import.archiveApkCount', { n: archive.apks.length })}</small></div>
          <div class="iw-choices" role="radiogroup" aria-label={t('import.chooseImportApk')}>{archive.apks.map(item =>
            <button key={item.path} role="radio" aria-checked={item.path === archive.apk} class={item.path === archive.apk ? 'on' : ''} disabled={busy}
              onClick={() => { if (item.path !== archive.apk) void chooseApk(item.path); }}>
              <code>{item.path}</code><small>{formatBytes(item.size)}</small></button>)}</div>
        </>}
        {!dataPack && <>
          <div class="iw-title"><b>{t('common.dataPack')}</b><small>{archive.data ? t('import.foundInArchive') : t('import.noDataInArchive')}</small></div>
          {archiveData && <>
            {mountRow}
            <MountPreview caption={t('import.mountPreview')} guest={guest} invalid={!!guestError} data={archivePreview(archive, extras)} error="" />
          </>}
          {extrasList(archive, 'import.extrasDefault')}
        </>}
      </section>}
      {!archiveData && <section class="iw-data">
        <div class="iw-title"><b>{t('common.dataPack')}</b><small>{t('import.optional')}</small></div>
        <div class="iw-pick">
          <span class={chosenLabel ? 'iw-host' : 'iw-host empty'} title={chosenLabel}><span><bdi>{chosenLabel || t('import.noneChosen')}</bdi></span></span>
          {chosenLabel
            ? <button disabled={busy} onClick={() => void clearData()}>{t('import.clear')}</button>
            : <>
              <button disabled={busy} onClick={() => void pickData('directory')}>{t('import.chooseFolder')}</button>
              <button disabled={busy} onClick={() => void pickData('file')}>{t('import.chooseArchive')}</button>
            </>}
        </div>
        {dataPack && <>
          {(([tone, text]) => <p class={tone ? `iw-note ${tone}` : 'iw-note'}>{text}</p>)(packNote(dataPack))}
          {dataPack.kind === 'directory' && <div class="iw-row"><span>{t('import.importAs')}</span>
            <div class="seg" role="radiogroup" aria-label={t('import.dataImportMode')}>{MODES.filter(item => item.value === 'copy' || has('path_settings')).map(item =>
              <button role="radio" aria-checked={item.value === externalMode} class={item.value === externalMode ? 'on' : ''} disabled={busy}
                onClick={() => { setExternalMode(item.value); if (item.value === 'link') setExtras([]); }}>{t(item.label)}</button>)}</div>
          </div>}
          {linking && <p class="iw-note warn">{t('import.mode.linkWarning')}</p>}
          {(dataPack.data || extras.length > 0) && <>
            {mountRow}
            <MountPreview caption={t('import.mountPreview')} guest={guest} invalid={!!guestError} data={archivePreview(dataPack, extras)} error="" />
          </>}
          {!linking && extrasList(dataPack, dataPack.data ? 'import.extrasDefault' : 'import.extrasTicked')}
        </>}
      </section>}
      {existing.length > 0 && <section class="iw-data iw-target">
        <div class="iw-title"><b>{t('import.target')}</b><small>{t('import.targetHint', { n: existing.length })}</small></div>
        <div class="iw-installed"><small>{t('import.installedVersion')}</small>
          {existing.map(item => <span key={item.installation_id} class="pill iw-installed-version">
            {existing.length > 1 && `${gameName(item)} · `}{item.version_name || t('common.version')} ({item.version_code})
          </span>)}
        </div>
        <div class="iw-choices" role="radiogroup" aria-label={t('import.target')}>
          {existing.map(item => <button key={item.installation_id} role="radio" aria-checked={target === item.installation_id}
            class={target === item.installation_id ? 'on' : ''} disabled={busy} onClick={() => setTarget(item.installation_id)}>
            <span><b class="iw-target-name" title={t('import.replaceInstance', { name: gameName(item) })}>{t('import.replaceInstance', { name: gameName(item) })}</b>
              <small class="iw-target-note" title={replacementNote}>{replacementNote}</small>
            </span></button>)}
          <button role="radio" aria-checked={target === NEW_INSTANCE} class={target === NEW_INSTANCE ? 'on' : ''} disabled={busy}
            onClick={() => setTarget(NEW_INSTANCE)}><span><b>{t('import.addAnother')}</b><small class="iw-target-note" title={t('import.addAnotherHint')}>{t('import.addAnotherHint')}</small></span></button>
        </div>
      </section>}
    </>}
    {error && <pre role="alert">{error}</pre>}
    <div class="modal-actions">
      {committing
        ? <button disabled={cancelling} onClick={() => void cancelImport()}>{cancelling ? t('import.cancelling') : t('import.cancel')}</button>
        : <button disabled={busy} onClick={() => void close()}>{t('common.cancel')}</button>}
      <button class="primary" disabled={busy || !canImport} onClick={() => void run(async () => {
        setStage(importingStage({ job: activeJob.current, state: 'importing' })); setCommitting(true); setCancelling(false);
        try {
          // Planned data: the host's mount unless the user typed one; a linked directory takes no extras.
          // Overwriting without a data pack keeps the instance's own pack and mount.
          const params = withData ? { extras: linking ? [] : extras, ...(guestEdited ? { external_guest_dir: guest } : {}), ...(linking ? { external_mode: 'link' } : {}) }
            : replacing ? {} : { external_guest_dir: '/sdcard' };
          const unpacking = archiveData || dataPack?.kind === 'archive';
          const initial = await rpc<ImportJob>('library.import', { job: activeJob.current, ...(replacing ? { replace: target } : { new_instance: true }), ...params });
          const result = await poll(initial, value => setStage(importingStage(value, unpacking))); setJob(result);
          if (result.state === 'cancelled') { activeJob.current = ''; setStage(t('import.cancelled')); return; }
          if (result.state !== 'imported' || !result.installation_id) throw new Error(t('import.noResult'));
          setStage(t(replacing ? 'import.replaced' : 'import.added')); activeJob.current = '';
          await onImported(result.installation_id);
        } catch (reason) {
          if (!disposed.current) setStage(t('import.failed'));
          throw reason;
        } finally { if (!disposed.current) { setCommitting(false); setCancelling(false); } }
      })}>{t(replacing ? 'import.replace' : 'import.addToLibrary')}</button>
    </div>
  </dialog>;
}
