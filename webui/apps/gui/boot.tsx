import { render, type ComponentType } from 'preact';
import { initialLanguage, setLanguage } from './shared/i18n';
import { recall } from './shared/prefs';
import { hostPlatform, layout, loadFeatures } from './shared/features';
import { editable } from './kit';

// Start-up shared by the two launcher builds: each entry passes its own root, so a build carries only its own layout
// (main.tsx: the console, for desktop windows; phone/main.tsx: the phone, for Android).
export function boot(Root: ComponentType) {
  if (navigator.platform.startsWith('Win')) document.documentElement.dataset.platform = 'windows';
  setLanguage(initialLanguage(recall('ogplay.language'), navigator.language));
  // The launcher is an app, not a page: the WebView's own menu (Reload, Back, Inspect…) stays off except in text
  // fields and on selected text, where it carries Cut/Copy/Paste.
  window.addEventListener('contextmenu', event => {
    if (!editable(event.target) && !window.getSelection()?.toString()) event.preventDefault();
  });
  // The host's features decide which entries exist, so they are known before the first frame.
  void loadFeatures().then(() => {
    if (hostPlatform() === 'Android') document.documentElement.dataset.platform = 'android';
    document.documentElement.dataset.layout = layout();
    render(<Root />, document.getElementById('app')!);
  });
}
