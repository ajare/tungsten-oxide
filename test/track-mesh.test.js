import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { Mesh, Vector2 } from '@willpower/geometry';
import * as TM from '../js/track-mesh.js';

// track-core.js is a classic browser script (an IIFE that assigns to `window`),
// not a module, so it is evaluated here with a stand-in global rather than
// imported. This is deliberate: it stays dependency-free and script-loadable.
function loadTrackCore() {
  const src = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
  const fakeWindow = {};
  new Function('window', src)(fakeWindow);
  return fakeWindow.TrackCore;
}
const TrackCore = loadTrackCore();

const SHIP_MARGIN = 0.9;
const flatPlacement = (over = {}) => ({ id: 'm1', asset: 'pad', x: 0, z: 0, rotation: 0, elevation: 0, ...over });

// A 30x30 pad with a 10x10 hole punched in the middle.
function padWithHole() {
  const mesh = new Mesh();
  const outer = mesh.addPolygon([new Vector2(0, 0), new Vector2(30, 0), new Vector2(30, 30), new Vector2(0, 30)]);
  const hole = mesh.addPolygon([new Vector2(10, 10), new Vector2(20, 10), new Vector2(20, 20), new Vector2(10, 20)]);
  mesh.addHoleToPolygon(outer, hole);
  return { mesh, outer, hole };
}

function railedPad(railAll = false) {
  const mesh = new Mesh();
  const pid = mesh.addPolygon([new Vector2(0, 0), new Vector2(30, 0), new Vector2(30, 30), new Vector2(0, 30)]);
  if (railAll) for (const de of mesh.getPolygon(pid).edges) TM.setRailEdge(mesh, de.edge, true);
  else TM.setRailEdge(mesh, mesh.findEdge(0, 1), true);   // south edge only, at z = 0
  return mesh;
}

test('compile bakes triangles for the polygon minus its hole', () => {
  const { mesh } = padWithHole();
  const c = TM.compile(mesh, flatPlacement());
  assert.ok(c.triangles.length >= 8, `expected >= 8 triangles, got ${c.triangles.length}`);
  assert.equal(c.polygons.length, 1, 'a hole is never a surface in its own right');
  assert.equal(c.polygons[0].holes.length, 1);
});

test('containment treats a hole as a void', () => {
  const { mesh } = padWithHole();
  const c = TM.compile(mesh, flatPlacement());
  assert.equal(TM.containsWorldPoint(c, 5, 5), true, 'inside the pad');
  assert.equal(TM.containsWorldPoint(c, 15, 15), false, 'inside the hole');
  assert.equal(TM.containsWorldPoint(c, -5, 5), false, 'outside the pad');
});

test('rail normals point out of the drivable area, including around holes', () => {
  const { mesh, outer, hole } = padWithHole();
  TM.setRailEdge(mesh, mesh.getPolygon(outer).edges[0].edge, true);
  for (const de of mesh.getPolygon(hole).edges) TM.setRailEdge(mesh, de.edge, true);
  const c = TM.compile(mesh, flatPlacement());

  assert.equal(c.rails.length, 5, '1 outer edge + 4 hole edges');
  for (const rail of c.rails) {
    const mx = (rail.a.x + rail.b.x) / 2, mz = (rail.a.z + rail.b.z) / 2;
    assert.equal(TM.containsWorldPoint(c, mx + rail.nx * 0.5, mz + rail.nz * 0.5), false, 'outward leaves');
    assert.equal(TM.containsWorldPoint(c, mx - rail.nx * 0.5, mz - rail.nz * 0.5), true, 'inward stays');
  }
});

test('yaw and translation round trip, and move containment with them', () => {
  const { mesh } = padWithHole();
  const placement = flatPlacement({ x: 100, z: -40, rotation: 37, elevation: 12.5 });
  const w = TM.localToWorld(placement, 30, 30);
  const back = TM.worldToLocal(placement, w.x, w.z);
  assert.ok(Math.abs(back.x - 30) < 1e-9 && Math.abs(back.y - 30) < 1e-9);

  const rotated = TM.compile(mesh, flatPlacement({ x: 100, z: -40, rotation: 90 }));
  assert.equal(TM.containsWorldPoint(rotated, 5, 5), false, 'local-space point is not world-space');
  const moved = TM.localToWorld({ x: 100, z: -40, rotation: 90 }, 5, 5);
  assert.equal(TM.containsWorldPoint(rotated, moved.x, moved.z), true);
});

test('triangulation is invariant under a rigid transform', () => {
  const { mesh } = padWithHole();
  const a = TM.compile(mesh, flatPlacement());
  const b = TM.compile(mesh, flatPlacement({ x: 77, z: 13, rotation: 41, elevation: 9 }));
  assert.equal(a.triangles.length, b.triangles.length);
});

test('driving into a rail is stopped short and loses only the into-wall speed', () => {
  const c = TM.compile(railedPad(), flatPlacement());
  const vel = { x: 0, z: -20 };
  const r = TM.slideAlongRails(c, { x: 15, z: 5 }, { x: 15, z: -5 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, true);
  assert.ok(r.z >= SHIP_MARGIN - 1e-6, `held inside, got z=${r.z}`);
  assert.ok(Math.abs(vel.z) < 1e-9, 'normal component cancelled');
});

test('glancing a rail slides along it and preserves tangential speed', () => {
  const c = TM.compile(railedPad(), flatPlacement());
  const vel = { x: 20, z: -5 };
  const r = TM.slideAlongRails(c, { x: 5, z: 2 }, { x: 12, z: -1 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, true);
  assert.ok(r.z >= SHIP_MARGIN - 1e-6);
  assert.ok(r.x > 5, 'still travelling along the wall');
  assert.ok(Math.abs(vel.x - 20) < 1e-9, 'tangential speed untouched');
});

test('a very fast move cannot tunnel through a rail', () => {
  const c = TM.compile(railedPad(), flatPlacement());
  const vel = { x: 0, z: -500 };
  const r = TM.slideAlongRails(c, { x: 15, z: 10 }, { x: 15, z: -400 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, true);
  assert.ok(r.z >= SHIP_MARGIN - 1e-6, `swept test must catch it, got z=${r.z}`);
});

test('a move that crosses no rail is left untouched', () => {
  const c = TM.compile(railedPad(), flatPlacement());
  const vel = { x: 5, z: 5 };
  const r = TM.slideAlongRails(c, { x: 5, z: 5 }, { x: 10, z: 10 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, false);
  assert.deepEqual({ x: r.x, z: r.z }, { x: 10, z: 10 });
  assert.deepEqual(vel, { x: 5, z: 5 });
});

test('crossing a bare edge is a ledge, not a wall', () => {
  const c = TM.compile(railedPad(), flatPlacement());
  const vel = { x: 0, z: 20 };
  const r = TM.slideAlongRails(c, { x: 15, z: 25 }, { x: 15, z: 35 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, false, 'the north edge carries no rail flag');
  assert.equal(TM.containsWorldPoint(c, r.x, r.z), false, 'ship has left the region and should fall');
});

test('a concave corner resolves against both walls', () => {
  const c = TM.compile(railedPad(true), flatPlacement());
  const vel = { x: -20, z: -20 };
  const r = TM.slideAlongRails(c, { x: 4, z: 4 }, { x: -6, z: -6 }, vel, SHIP_MARGIN);
  assert.equal(r.hit, true);
  assert.ok(r.x >= SHIP_MARGIN - 1e-6 && r.z >= SHIP_MARGIN - 1e-6, `held in corner, got ${r.x},${r.z}`);
});

test('asset ids stay unique so a re-import never disturbs existing placements', () => {
  const taken = new Set();
  const a = TM.uniqueAssetId('Pit Complex.json', taken); taken.add(a);
  const b = TM.uniqueAssetId('Pit Complex.json', taken); taken.add(b);
  assert.equal(a, 'pit-complex');
  assert.equal(b, 'pit-complex-2');
  assert.notEqual(a, b);
});

// --- track-core schema 4 -----------------------------------------------------

function trackFixture() {
  const { mesh } = padWithHole();
  TM.setRailEdge(mesh, mesh.findEdge(0, 1), true);
  return JSON.stringify({
    version: 4,
    name: 'Mesh Test',
    paths: [{ id: 'path1', closed: true, points: [
      { type: 'position', id: 'p1', pos: [50, 0, 0], weight: 1 },
      { type: 'position', id: 'p2', pos: [0, 0, 50], weight: 1 },
      { type: 'position', id: 'p3', pos: [-50, 0, 0], weight: 1 },
      { type: 'position', id: 'p4', pos: [0, 0, -50], weight: 1 }
    ] }],
    meshAssets: {
      pad: { name: 'pad', railHeight: 2.5, mesh: TM.meshToJSON(mesh) },
      orphan: { mesh: TM.meshToJSON(mesh) }
    },
    meshes: [
      { id: 'm1', asset: 'pad', x: 10, z: 20, rotation: 37, elevation: 12.5 },
      { id: 'm2', asset: 'pad', x: -60, z: 0, rotation: 0, elevation: 4 },
      { id: 'm3', asset: 'ghost', x: 0, z: 0, rotation: 0, elevation: 0 }
    ]
  });
}

test('parseTrack keeps assets and placements but drops dangling references', () => {
  const t = TrackCore.parseTrack(trackFixture());
  assert.deepEqual(Object.keys(t.meshAssets).sort(), ['orphan', 'pad']);
  assert.deepEqual(t.meshes.map(m => m.id), ['m1', 'm2'], 'm3 points at a missing asset');
  assert.equal(t.meshAssets.pad.railHeight, 2.5);
  assert.equal(t.meshAssets.orphan.railHeight, TrackCore.DEFAULT_RAIL_HEIGHT);
});

test('serializeTrack round trips and garbage-collects unreferenced assets', () => {
  const t = TrackCore.parseTrack(trackFixture());
  const again = TrackCore.parseTrack(TrackCore.serializeTrack(t));
  assert.deepEqual(Object.keys(again.meshAssets), ['pad'], 'orphan dropped on export');
  assert.deepEqual(again.meshes.map(m => m.id), ['m1', 'm2']);
  assert.equal(again.meshes[0].elevation, 12.5);
  assert.equal(again.meshes[0].rotation, 37);
});

test('rail flags survive a full track round trip', () => {
  const t = TrackCore.parseTrack(TrackCore.serializeTrack(TrackCore.parseTrack(trackFixture())));
  const restored = TM.meshFromJSON(t.meshAssets.pad.mesh);
  assert.equal([...restored.edges.values()].filter(e => e.attributes.rail).length, 1);
  assert.equal(TM.compile(restored, t.meshes[0]).rails.length, 1);
});

test('tracks written before schema 4 load with no mesh data', () => {
  const legacy = TrackCore.parseTrack(JSON.stringify({
    version: 3, name: 'Old',
    paths: [{ id: 'p', closed: true, points: [
      { type: 'position', id: 'a', pos: [10, 0, 0], weight: 1 },
      { type: 'position', id: 'b', pos: [0, 0, 10], weight: 1 },
      { type: 'position', id: 'c', pos: [-10, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [0, 0, -10], weight: 1 }
    ] }]
  }));
  assert.deepEqual(legacy.meshAssets, {});
  assert.deepEqual(legacy.meshes, []);
  assert.ok(TrackCore.serializeTrack(legacy).includes('"meshes": []'));
});
