import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', source)(fakeWindow);
globalThis.TrackCore = fakeWindow.TrackCore;

const { generateRandomTrackMeshCorpus } = await import('./parity/random-track-mesh.js');
const fixtureDir = new URL('./fixtures/random-track-mesh/', import.meta.url);

const corpus = generateRandomTrackMeshCorpus();
const expectedSummaries = JSON.parse(readFileSync(new URL('expected/geometry-summary.json', fixtureDir), 'utf8'));
const manifest = JSON.parse(readFileSync(new URL('manifest.json', fixtureDir), 'utf8'));

test('seeded random track JSON fixtures and JS geometry oracle are reproducible', () => {
  assert.deepEqual(manifest, corpus.map(({ seed, complexity, file }) => ({ seed, complexity, file })));
  for (const entry of corpus) {
    assert.equal(readFileSync(new URL(entry.file, fixtureDir), 'utf8'), entry.json,
      `${entry.file}: committed randomly-generated source is current`);
    assert.deepEqual(expectedSummaries[entry.file], entry.summary,
      `${entry.file}: committed JS geometry summary is current`);
  }
});

test('seeded random tracks exercise varied path and mesh render geometry', () => {
  const kinds = new Set();
  let textured = 0, holed = 0, multiplePlacements = 0;
  for (const entry of corpus) {
    const parsed = TrackCore.parseTrack(entry.json);
    for (const batch of entry.summary.batches) kinds.add(batch.kind);
    if (parsed.paths.some(path => path.texture)) textured++;
    if (Object.values(parsed.meshAssets).some(asset => asset.mesh.polygons.some(polygon => polygon.hole))) holed++;
    if (parsed.meshes.length > 1) multiplePlacements++;
    assert.equal(entry.summary.warningCount, 0, `${entry.file}: JS bake has no warnings`);
  }
  for (const kind of ['PathSurface', 'PathShell', 'PathRail', 'MeshSurface', 'MeshRail', 'ZoneSurface'])
    assert.ok(kinds.has(kind), `corpus emits ${kind}`);
  assert.ok(textured > 0, 'corpus includes textured paths');
  assert.ok(holed > 0, 'corpus includes polygon holes');
  assert.ok(multiplePlacements > 0, 'corpus includes repeated transformed placements');
});
