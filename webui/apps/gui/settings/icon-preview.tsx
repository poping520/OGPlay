import { useEffect, useState } from 'preact/hooks';
import { rpc } from '../shared/rpc';
import { t } from '../shared/i18n';

// The running_icon choices as the game will show them: the host draws both with the same compositor, from a
// placeholder game icon. Shown under the running_icon row, indented to its label; picking a card edits the same
// draft value as the row's segmented control.
const styles = [
  ['badge', 'setting.choice.badge', 'settings.icon.badgeHint'],
  ['cradle', 'setting.choice.cradle', 'settings.icon.cradleHint'],
] as const;

export function IconPreview({ value, disabled, choose }: { value: string; disabled: boolean; choose: (value: string) => void }) {
  const [images, setImages] = useState<Record<string, string> | null>(null);
  const [failed, setFailed] = useState('');
  useEffect(() => {
    void rpc<Record<string, string>>('settings.icon_preview')
      .then(setImages, reason => setFailed(reason instanceof Error ? reason.message : String(reason)));
  }, []);
  return <div class="row-extra">
    <p>{t('settings.icon.placeholderNote')}</p>
    {failed ? <p class="err">{failed}</p> : <div class="icon-preview" role="radiogroup" aria-label={t('setting.running_icon.label')}>
      {styles.map(([key, label, hint]) => <button key={key} role="radio" aria-checked={value === key} class={value === key ? 'on' : ''}
        disabled={disabled} onClick={() => choose(key)}>
        {images ? <img src={images[key]} alt="" /> : <i class="placeholder" />}<b>{t(label)}</b><small>{t(hint)}</small>
      </button>)}
    </div>}
  </div>;
}
