import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const coreSource = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', coreSource)(fakeWindow);
globalThis.TrackCore = fakeWindow.TrackCore;
const TrackCore = globalThis.TrackCore;

const { bakeTrackPhysics } = await import('../js/track-bake.js');
const { buildTrackRenderGeometry } = await import('../js/track-render-geometry.js');

function fixture(name) {
  return TrackCore.parseTrack(readFileSync(new URL(`./fixtures/mesh/${name}`, import.meta.url), 'utf8'));
}

function area(batch) {
  let sum = 0;
  for (let i = 0; i < batch.indices.length; i += 3) {
    const a = batch.vertices[batch.indices[i]].position;
    const b = batch.vertices[batch.indices[i + 1]].position;
    const c = batch.vertices[batch.indices[i + 2]].position;
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    sum += Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) / 2;
  }
  return sum;
}

test('renderer-neutral build emits every agreed geometry kind except trigger debug', () => {
  const track = fixture('mesh-effects.json');
  const built = buildTrackRenderGeometry(track, bakeTrackPhysics(track));
  const kinds = new Set(built.batches.map(b => b.kind));
  for (const kind of ['PathSurface', 'PathShell', 'PathRail', 'MeshSurface', 'MeshRail', 'ZoneSurface', 'TriggerSurface']) {
    assert.ok(kinds.has(kind), `contains ${kind}`);
  }
  assert.equal(kinds.has('TriggerDebug'), false);
});

test('all render vertices are finite, indexed safely, and opaque white', () => {
  const track = fixture('mesh-effects.json');
  const { batches } = buildTrackRenderGeometry(track);
  for (const b of batches) {
    assert.equal(b.indices.length % 3, 0, `${b.id}: triangle indices`);
    for (const index of b.indices) assert.ok(index >= 0 && index < b.vertices.length, `${b.id}: valid index`);
    for (const v of b.vertices) {
      assert.ok(v.position.every(Number.isFinite), `${b.id}: finite position`);
      assert.ok(v.normal.every(Number.isFinite), `${b.id}: finite normal`);
      assert.ok(Math.abs(Math.hypot(...v.normal) - 1) < 1e-9, `${b.id}: unit normal`);
      assert.ok(v.uv.every(Number.isFinite), `${b.id}: finite UV`);
      assert.deepEqual(v.rgba, [1, 1, 1, 1], `${b.id}: default white RGBA`);
    }
  }
});

test('mesh triangulation covers the pad but subtracts its hole', () => {
  const track = fixture('pad-with-hole.json');
  const { batches } = buildTrackRenderGeometry(track);
  const surface = batches.find(b => b.kind === 'MeshSurface');
  assert.ok(surface && surface.indices.length > 0);
  assert.ok(Math.abs(area(surface) - 3200) < 1e-7, `60x60 minus 20x20 hole, area=${area(surface)}`);
});

test('path-hosted zones produce conforming renderer-neutral geometry', () => {
  const track = fixture('transformed-square.json');
  track.zones = TrackCore.normalizeZones([{
    id: 'path-grid', effect: 'startGrid', width: 12, length: 30,
    host: { kind: 'path', pathId: 'path-main', t: 0.5, lateral: 0 }
  }], track.paths, track.meshes);
  const zone = buildTrackRenderGeometry(track).batches.find(b => b.id === 'zone-path-grid');
  assert.ok(zone && zone.indices.length > 0);
  assert.equal(zone.hasUv, true);
  assert.equal(zone.materialKey, 'Tracks/DefaultZoneMaterial');
});

test('path texture identity and tile are retained without loading an image', () => {
  const track = fixture('transformed-square.json');
  track.textureAssets = {
    atlas: { name: 'atlas.png', path: 'textures/atlas.png', width: 64, height: 64, tileWidth: 32, tileHeight: 32 }
  };
  track.paths[0].texture = { asset: 'atlas', tile: 2 };
  const surface = buildTrackRenderGeometry(track).batches.find(b => b.kind === 'PathSurface');
  assert.deepEqual(surface.texture, { assetId: 'atlas', tile: 2 });
  assert.equal(surface.hasUv, true);
});

test('mesh rail geometry reaches from placement elevation to authored rail height', () => {
  const track = fixture('transformed-square.json');
  const { batches } = buildTrackRenderGeometry(track);
  const rails = batches.find(b => b.kind === 'MeshRail');
  const ys = rails.vertices.map(v => v.position[1]);
  assert.equal(Math.min(...ys), 8);
  assert.equal(Math.max(...ys), 13);
});
