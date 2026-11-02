// Line glyphs for toolbar and key icons, drawn like the settings icons: 24-unit grid, round strokes (theme.css `.glyph`).
// A font symbol would take each system font's size and weight instead.
const GLYPHS = {
  check: '<path d="M5 12.5l4.5 4.5L19 7"/>',
  down: '<path d="M6 9l6 6 6-6"/>',
  search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5 20 20"/>',
  reload: '<path d="M20 12a8 8 0 1 1-2.34-5.66L20 8.5"/><path d="M20 3.5v5h-5"/>',
  settings: '<path d="M10.2 5.4 10.4 3.1 13.6 3.1 13.8 5.4 15.4 6.1 17.2 4.6 19.4 6.8 17.9 8.6 18.6 10.2 20.9 10.4 20.9 13.6 18.6 13.8 17.9 15.4 19.4 17.2 17.2 19.4 15.4 17.9 13.8 18.6 13.6 20.9 10.4 20.9 10.2 18.6 8.6 17.9 6.8 19.4 4.6 17.2 6.1 15.4 5.4 13.8 3.1 13.6 3.1 10.4 5.4 10.2 6.1 8.6 4.6 6.8 6.8 4.6 8.6 6.1z"/><circle cx="12" cy="12" r="3"/>',
};
export type GlyphName = keyof typeof GLYPHS;
export function Glyph({ name }: { name: GlyphName }) {
  return <svg class="glyph" viewBox="0 0 24 24" aria-hidden="true" dangerouslySetInnerHTML={{ __html: GLYPHS[name] }} />;
}
