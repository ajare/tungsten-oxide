import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', source)(fakeWindow);
const TrackCore = fakeWindow.TrackCore;
const expected = JSON.parse(readFileSync(new URL('./fixtures/mesh/expected/normalized-summary.json', import.meta.url), 'utf8'));

function summary(track) {
  const assets = Object.values(track.meshAssets);
  return {
    name: track.name,
    samples: track.samples,
    paths: track.paths.length,
    points: track.paths.reduce((n, path) => n + path.points.length, 0),
    meshAssets: assets.length,
    meshes: track.meshes.length,
    vertices: assets.reduce((n, asset) => n + asset.mesh.vertices.length, 0),
    edges: assets.reduce((n, asset) => n + asset.mesh.edges.length, 0),
    railEdges: assets.reduce((n, asset) => n + asset.mesh.edges.filter(edge => edge.attributes?.rail).length, 0),
    polygons: assets.reduce((n, asset) => n + asset.mesh.polygons.length, 0),
    zones: track.zones.length,
    triggers: track.triggers.length,
    start: track.start,
    handling: track.handling,
    placements: track.meshes.map(mesh => ({
      id: mesh.id, asset: mesh.asset, x: mesh.x, z: mesh.z,
      rotation: mesh.rotation, elevation: mesh.elevation
    })),
    effects: track.zones.map(zone => ({ id: zone.id, effect: zone.effect, kind: zone.host.kind })),
    gates: track.triggers.map(trigger => ({
      id: trigger.id, type: trigger.type, role: trigger.role || '',
      direction: trigger.direction, kind: trigger.host.kind
    }))
  };
}

for (const [filename, wanted] of Object.entries(expected)) {
  test(`shared loader fixture summary remains current: ${filename}`, () => {
    const text = readFileSync(new URL(`./fixtures/mesh/${filename}`, import.meta.url), 'utf8');
    assert.deepEqual(summary(TrackCore.parseTrack(text)), wanted);
  });
}
