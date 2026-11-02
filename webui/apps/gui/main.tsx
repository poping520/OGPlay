import { render } from 'preact';
import { App, editable } from './app';
import { setLanguage } from './shared/i18n';
import { recall } from './shared/prefs';

setLanguage(recall('ogplay.language'));
// The launcher is an app, not a page: the WebView's own menu (Reload, Back, Inspect…) stays off except in text fields
// and on selected text, where it carries Cut/Copy/Paste.
window.addEventListener('contextmenu', event => {
  if (!editable(event.target) && !window.getSelection()?.toString()) event.preventDefault();
});
render(<App />, document.getElementById('app')!);
