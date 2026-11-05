import { render } from 'preact';
import { App, editable } from './app';
import { initialLanguage, setLanguage } from './shared/i18n';
import { recall } from './shared/prefs';

if (navigator.platform.startsWith('Win')) document.documentElement.dataset.platform = 'windows';
setLanguage(initialLanguage(recall('ogplay.language'), navigator.language));
// The launcher is an app, not a page: the WebView's own menu (Reload, Back, Inspect…) stays off except in text fields
// and on selected text, where it carries Cut/Copy/Paste.
window.addEventListener('contextmenu', event => {
  if (!editable(event.target) && !window.getSelection()?.toString()) event.preventDefault();
});
render(<App />, document.getElementById('app')!);
