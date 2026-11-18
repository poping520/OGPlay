import { useEffect, useState } from 'preact/hooks';
import type { LibraryItem } from '../shared/rpc';

// The game's own launcher icon, or its first letter on a plain tile when it ships none (or it fails to load). Both
// layouts' icon views use it; each sizes `.app-icon` for its own place (library.css holds the shared look).
export function GameIcon({ game }: { game: LibraryItem }) {
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [game.icon]);
  return <span class="app-icon">{game.icon && !failed ? <img src={game.icon} alt="" draggable={false} onError={() => setFailed(true)} />
    : <b aria-hidden="true">{[...game.display_name][0] ?? '?'}</b>}</span>;
}
