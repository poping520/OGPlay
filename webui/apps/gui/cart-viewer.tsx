import { useEffect, useRef, useState } from 'preact/hooks';
import type { LibraryItem } from './rpc';
import { cycle, statusOf, versionLabel } from './library';
import { Backdrop } from './cartridge';
import { Inspect3D } from './cartridge3d';
import { VIEWS, type Cart3D, type View } from './cart3d/api';
import { Back, Brand, Key, type Skin } from './kit';
import { t } from './i18n';

// Cartridge viewer: the selected library entry's 3D cartridge full screen, to turn round, look at from the presets
// and open up to see the board. Everything printed or lit on it is still a host fact; ←/→ walk the same list as
// the rail and the library keeps that selection on return.
const VIEW_LABELS: Record<View, string> = { three: '斜视', front: '正面', back: '背面', side: '侧面', top: '顶部', bottom: '底部' };
// model part (material name) -> what the pointer readout calls it
const PARTS: Record<string, string> = {
  Shell: '外壳', Art: '正面贴纸', Title: '标题条', BackLabel: '背面贴纸', LED: '状态灯', PCB: '电路板', Chip: '芯片', Metal: '金属元件',
  Passive: '贴片元件', Gold: '金手指', Screw: '三翼螺丝', Spine: '侧脊标签', Holo: '镭射防伪标', Trim: '金属饰件', Marking: '芯片丝印',
};

export function CartViewer({ api, items, selected, skin, select, onBack }: {
  api: Cart3D; items: LibraryItem[]; selected: LibraryItem; skin: Skin; select: (id: string) => void; onBack: () => void;
}) {
  const item = selected;
  const status = statusOf(item);
  const tone = item.running ? 'running' : status.tone;
  const [view, setView] = useState<View | null>('three');
  const [open, setOpen] = useState(false);
  const [spin, setSpin] = useState(true);
  const [part, setPart] = useState<string | null>(null);
  const tip = useRef<HTMLDivElement>(null);
  const pick = (next: View) => { setView(next); setSpin(false); };
  const reset = () => { setView('three'); setOpen(false); };
  const step = (by: number) => { if (items.length > 1) select(cycle(items, item, by).installation_id); };
  // Keyboard stands in for the footer's gamepad legend.
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      const at = view ? VIEWS.indexOf(view) : 0;
      if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') step(event.key === 'ArrowRight' ? 1 : -1);
      else if (event.key === 'q' || event.key === 'e') pick(VIEWS[(at + (event.key === 'e' ? 1 : VIEWS.length - 1)) % VIEWS.length]);
      else if (event.key >= '1' && event.key <= String(VIEWS.length)) pick(VIEWS[Number(event.key) - 1]);
      else if (event.key === 'a' || (event.key === 'Enter' && event.target === document.body)) setOpen(value => !value);
      else if (event.key === 'y') setSpin(value => !value);
      else if (event.key === 'x') reset();
      else if (event.key === 'b' || event.key === 'Escape') onBack();
      else return;
      event.preventDefault();
    };
    window.addEventListener('keydown', keydown);
    return () => window.removeEventListener('keydown', keydown);
  });
  // The readout follows the pointer without a render per move; only a change of part re-renders.
  const hover = (name: string | null, x: number, y: number) => {
    setPart(name && name in PARTS ? name : null);
    if (tip.current) tip.current.style.transform = `translate(${x + 16}px, ${y + 14}px)`;
  };
  const at = items.findIndex(game => game.installation_id === item.installation_id);
  return <div class={`console viewer skin-${skin}`}>
    <Backdrop item={item} skin={skin} />
    <header class="top">
      <Brand />
      <Back onClick={onBack} />
      <span class="crumb">{t('卡带')} · <b>{item.display_name}</b></span>
      <button class={`pill ${spin ? 'on' : ''}`} aria-pressed={spin} onClick={() => setSpin(value => !value)}><Key>Y</Key>{t('自动旋转')}</button>
      <button class="pill" onClick={reset}><Key>X</Key>{t('复位')}</button>
      <button class="pill main" aria-pressed={open} onClick={() => setOpen(value => !value)}><Key>A</Key>{open ? t('合上卡带') : t('拆开看看')}</button>
    </header>
    <section class="viewer-stage" aria-label={t('卡带展示')}>
      <Inspect3D api={api} item={item} tone={tone} view={view} open={open} spin={spin} orbit={() => setView(null)} part={hover} />
      <div class="viewer-info" key={item.installation_id}>
        <div class="kicker"><i />{versionLabel(item)} · {status.label}</div>
        <h1 title={item.display_name}>{item.display_name}</h1>
        <div class="sub"><span class="mono">{item.package || item.installation_id}</span>{items.length > 1 && at >= 0 ? <span> · {at + 1} / {items.length}</span> : null}</div>
      </div>
      <div class="seg viewer-views" role="radiogroup" aria-label={t('视角')}>{VIEWS.map((name, index) =>
        <button key={name} role="radio" aria-checked={view === name} class={view === name ? 'on' : ''} onClick={() => pick(name)}><b>{index + 1}</b>{t(VIEW_LABELS[name])}</button>)}</div>
      <div class="part-tip" ref={tip} hidden={!part} role="status">{part && <>{t(PARTS[part])}{part === 'LED' ? ` · ${status.label}` : ''}</>}</div>
    </section>
    <footer class="foot">
      <span class="hint"><Key>← →</Key>{t('游戏')}</span><span class="hint"><Key>L R</Key>{t('视角')}<kbd>Q E</kbd></span>
      <span class="hint"><Key>A</Key>{t('拆开')}</span><span class="hint"><Key>Y</Key>{t('自转')}</span><span class="hint"><Key>X</Key>{t('复位')}</span>
      <span class="hint"><Key>B</Key>{t('返回')}<kbd>Esc</kbd></span>
      <span class="foot-note">{t('拖动旋转 · 滚轮缩放 · 指向部件看名称')}</span>
    </footer>
  </div>;
}
