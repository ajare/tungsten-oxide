// Deliberately regenerate deterministic random JSON tracks and the JavaScript
// geometry oracle consumed by the native random_geometry_parity test.
import { mkdir, writeFile } from 'node:fs/promises';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', source)(fakeWindow);
globalThis.TrackCore = fakeWindow.TrackCore;

const { generateRandomTrackMeshCorpus } = await import('../test/parity/random-track-mesh.js');
const output = new URL('../test/fixtures/random-track-mesh/', import.meta.url);
const expected = new URL('expected/', output);
await mkdir(expected, { recursive: true });

const corpus = generateRandomTrackMeshCorpus();
for (const entry of corpus) {
  await writeFile(new URL(entry.file, output), entry.json);
  console.log(`wrote test/fixtures/random-track-mesh/${entry.file}`);
}
await writeFile(new URL('manifest.json', output), JSON.stringify(
  corpus.map(({ seed, complexity, file }) => ({ seed, complexity, file })), null, 2
) + '\n');
await writeFile(new URL('geometry-summary.json', expected), JSON.stringify(
  Object.fromEntries(corpus.map(entry => [entry.file, entry.summary])), null, 2
) + '\n');
console.log(`wrote random geometry oracle for ${corpus.length} tracks`);
