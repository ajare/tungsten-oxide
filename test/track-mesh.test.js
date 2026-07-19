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

test('railing on import walls the rim, and the inside of a hole with it', () => {
  const { mesh } = padWithHole();
  const railed = TM.railBoundaryEdges(mesh);

  assert.equal(railed, 8, '4 outer rim edges + 4 hole rim edges');
  const c = TM.compile(mesh, flatPlacement());
  assert.equal(c.rails.length, 8);
  // Every wall must face away from the drivable surface, hole rims included --
  // otherwise a rail pushes the ship into the void it is meant to fence off.
  for (const rail of c.rails) {
    const mx = (rail.a.x + rail.b.x) / 2, mz = (rail.a.z + rail.b.z) / 2;
    assert.equal(TM.containsWorldPoint(c, mx + rail.nx * 0.5, mz + rail.nz * 0.5), false);
    assert.equal(TM.containsWorldPoint(c, mx - rail.nx * 0.5, mz - rail.nz * 0.5), true);
  }
});

test('railing on import leaves interior seams between polygons bare', () => {
  // Two squares meeting along x = 30: that shared edge is a seam you drive
  // across, not a rim. Railing it would wall the region down the middle.
  const mesh = new Mesh();
  // Share the two middle vertices explicitly -- passing coincident points
  // would mint separate vertices and leave the squares merely abutting.
  const a = mesh.addVertex(new Vector2(0, 0));
  const b = mesh.addVertex(new Vector2(30, 0));
  const c0 = mesh.addVertex(new Vector2(30, 30));
  const d = mesh.addVertex(new Vector2(0, 30));
  const e = mesh.addVertex(new Vector2(60, 0));
  const f = mesh.addVertex(new Vector2(60, 30));
  mesh.addPolygon([a, b, c0, d]);
  mesh.addPolygon([b, e, f, c0]);
  const seam = mesh.findEdge(b, c0);
  assert.equal(mesh.getEdge(seam).polygons.size, 2, 'test setup: the two squares must actually share an edge');

  TM.railBoundaryEdges(mesh);

  assert.equal(TM.isRailEdge(mesh, seam), false, 'the shared seam stays drivable');
  const c = TM.compile(mesh, flatPlacement());
  assert.equal(c.rails.length, 6, 'the 8 authored edges minus the 2 that coincide on the seam');
});

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

function trackFixture(version = 5) {
  const { mesh } = padWithHole();
  TM.setRailEdge(mesh, mesh.findEdge(0, 1), true);
  return JSON.stringify({
    version,
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
  const legacy = TrackCore.parseTrack(legacyTrackJson(3));
  assert.deepEqual(legacy.meshAssets, {});
  assert.deepEqual(legacy.meshes, []);
  assert.ok(TrackCore.serializeTrack(legacy).includes('"meshes": []'));
});

// --- schema 5: world unit scale ---------------------------------------------

function legacyTrackJson(version, extra = {}) {
  return JSON.stringify({
    version, name: 'Old',
    paths: [{ id: 'p', closed: true, points: [
      { type: 'position', id: 'a', pos: [10, 1, 0], weight: 1 },
      { type: 'position', id: 'b', pos: [0, 0, 10], weight: 1 },
      { type: 'position', id: 'c', pos: [-10, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [0, 0, -10], weight: 1 },
      { type: 'roll', t: 0, roll: 20 },
      { type: 'roll', t: 0.5, roll: -20 },
      { type: 'width', t: 0, width: 12 },
      { type: 'width', t: 0.5, width: 12 }
    ] }],
    ...extra
  });
}

const positions = track => TrackCore.splitPoints(track.paths[0].points).controlPoints.map(p => p.pos);
const widths = track => TrackCore.splitPoints(track.paths[0].points).widthPoints.map(p => p.width);
const rolls = track => TrackCore.splitPoints(track.paths[0].points).rollPoints.map(p => p.roll);

test('a pre-schema-5 track is scaled up on load', () => {
  const migrated = TrackCore.parseTrack(legacyTrackJson(4));
  assert.equal(migrated.version, 5);
  assert.deepEqual(positions(migrated)[0], [20, 2, 0], 'positions double');
  assert.deepEqual(widths(migrated), [24, 24], 'widths double');
});

test('scale-invariant values are left alone by the migration', () => {
  const migrated = TrackCore.parseTrack(legacyTrackJson(4));
  const parts = TrackCore.splitPoints(migrated.paths[0].points);
  assert.deepEqual(rolls(migrated), [20, -20], 'roll is an angle');
  assert.deepEqual(parts.widthPoints.map(p => p.t), [0, 0.5], 't is a fraction');
  assert.deepEqual(parts.controlPoints.map(p => p.weight), [1, 1, 1, 1], 'NURBS weights');
  for (const p of parts.crossSectionPoints) {
    assert.equal(p.curvature, 0, 'curvature is dimensionless');
    assert.equal(p.tightness, 1, 'tightness is an exponent');
  }
});

test('a schema-5 track is left exactly as authored', () => {
  const already = TrackCore.parseTrack(legacyTrackJson(5));
  assert.deepEqual(positions(already)[0], [10, 1, 0], 'no double-scaling');
  assert.deepEqual(widths(already), [12, 12]);
});

test('migrating is idempotent through a save/load cycle', () => {
  const once = TrackCore.parseTrack(legacyTrackJson(4));
  const twice = TrackCore.parseTrack(TrackCore.serializeTrack(once));
  assert.deepEqual(positions(twice), positions(once), 're-loading must not scale again');
  assert.deepEqual(widths(twice), widths(once));
});

test('defaults injected during normalization are not double-scaled', () => {
  // This track authors no width points, so normalization supplies them. Those
  // defaults are already in current units and must not be scaled again.
  const noWidths = JSON.stringify({
    version: 4, name: 'Old',
    paths: [{ id: 'p', closed: true, points: [
      { type: 'position', id: 'a', pos: [10, 0, 0], weight: 1 },
      { type: 'position', id: 'b', pos: [0, 0, 10], weight: 1 },
      { type: 'position', id: 'c', pos: [-10, 0, 0], weight: 1 },
      { type: 'position', id: 'd', pos: [0, 0, -10], weight: 1 }
    ] }],
    meshAssets: { pad: { mesh: TM.meshToJSON(padWithHole().mesh) } },   // no railHeight
    meshes: [{ id: 'm1', asset: 'pad', x: 5, z: 5, rotation: 10, elevation: 3 }]
  });
  const migrated = TrackCore.parseTrack(noWidths);
  assert.deepEqual(widths(migrated), [24, 24], 'default width, not 48');
  assert.equal(migrated.meshAssets.pad.railHeight, TrackCore.DEFAULT_RAIL_HEIGHT, 'default rail height, not doubled');
  // Values that WERE authored still scale.
  assert.equal(migrated.meshes[0].x, 10);
  assert.equal(migrated.meshes[0].elevation, 6);
  assert.equal(migrated.meshes[0].rotation, 10, 'rotation is an angle');
});

test('mesh asset geometry and rail height scale with the track', () => {
  const migrated = TrackCore.parseTrack(trackFixture(4));
  const mesh = TM.meshFromJSON(migrated.meshAssets.pad.mesh);
  const xs = [...mesh.vertices.values()].map(v => v.position.x);
  assert.equal(Math.max(...xs), 60, 'the 30-wide pad is now 60 wide');
  assert.equal(migrated.meshAssets.pad.railHeight, 5, 'authored 2.5 doubles');
  assert.equal(migrated.meshes[0].elevation, 25, 'authored 12.5 doubles');
  assert.equal(migrated.meshes[0].rotation, 37, 'rotation unchanged');
});

test('built-in tracks are authored in current units', () => {
  for (const name of ['DEFAULT_TRACK', 'STARTER_TRACK']) {
    const t = TrackCore[name];
    assert.equal(t.version, 5, `${name} must not be re-migrated on load`);
    const reloaded = TrackCore.parseTrack(TrackCore.serializeTrack(t));
    assert.deepEqual(positions(reloaded), positions(t), `${name} survives a round trip unscaled`);
  }
});
