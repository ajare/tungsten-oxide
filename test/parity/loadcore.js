/* Install the classic-script TrackCore onto globalThis so the physics/bake
 * modules (which read it lazily) resolve it in Node, exactly as the browser does
 * via track-core.js running before any module. */
import { readFileSync } from 'node:fs';

export function installTrackCore() {
  const src = readFileSync(new URL('../../track-core.js', import.meta.url), 'utf8');
  const fakeWindow = {};
  new Function('window', src)(fakeWindow);
  globalThis.TrackCore = fakeWindow.TrackCore;
  return fakeWindow.TrackCore;
}
