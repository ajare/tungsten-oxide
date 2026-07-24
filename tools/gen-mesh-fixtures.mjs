// Generate the small current-schema mesh tracks shared by the JS and C++
// runtime tests. Run deliberately when a fixture changes; tests consume the
// committed JSON and never regenerate it implicitly.
import { mkdir, writeFile } from 'node:fs/promises';
import { readFileSync } from 'node:fs';
import { Mesh, Vector2 } from '@willpower/geometry';
import * as TrackMesh from '../js/track-mesh.js';

const src = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', src)(fakeWindow);
const TrackCore = fakeWindow.TrackCore;

const outputDir = new URL('../test/fixtures/mesh/', import.meta.url);
await mkdir(outputDir, { recursive: true });

function baseTrack(name, meshAssets, meshes, extra = {}) {
  return {
    version: TrackCore.TRACK_SCHEMA_VERSION,
    name,
    paths: [{
      id: 'path-main', closed: false, points: [
        { type: 'position', id: 'p0', pos: [0, 0, -120], weight: 1 },
        { type: 'position', id: 'p1', pos: [0, 0, -40], weight: 1 },
        { type: 'position', id: 'p2', pos: [0, 0, 40], weight: 1 },
        { type: 'position', id: 'p3', pos: [0, 0, 120], weight: 1 },
        { type: 'width', t: 0, width: 24 },
        { type: 'width', t: 1, width: 24 }
      ]
    }],
    start: { path: 0, point: 0, reverse: false },
    meshAssets,
    meshes,
    ...extra
  };
}

function asset(mesh, railHeight = 6) {
  return { name: 'Fixture mesh', railHeight, mesh: TrackMesh.meshToJSON(mesh) };
}

function polygonMesh(points, railAll = false) {
  const mesh = new Mesh();
  const polygon = mesh.addPolygon(points.map(([x, y]) => new Vector2(x, y)));
  if (railAll) for (const edge of mesh.getPolygon(polygon).edges) TrackMesh.setRailEdge(mesh, edge.edge, true);
  return mesh;
}

function square(size = 40, railAll = false) {
  return polygonMesh([[0, 0], [size, 0], [size, size], [0, size]], railAll);
}

const fixtures = [];

{
  const mesh = square();
  TrackMesh.setRailEdge(mesh, mesh.findEdge(0, 1), true);
  fixtures.push(['transformed-square.json', baseTrack(
    'Fixture - transformed square',
    { pad: asset(mesh, 5) },
    [{ id: 'square-placed', asset: 'pad', x: 100, z: 50, rotation: 37, elevation: 8 }]
  )]);
}

{
  const mesh = new Mesh();
  const outer = mesh.addPolygon([[0, 0], [60, 0], [60, 60], [0, 60]].map(([x, y]) => new Vector2(x, y)));
  const hole = mesh.addPolygon([[20, 20], [40, 20], [40, 40], [20, 40]].map(([x, y]) => new Vector2(x, y)));
  mesh.addHoleToPolygon(outer, hole);
  TrackMesh.railBoundaryEdges(mesh);
  fixtures.push(['pad-with-hole.json', baseTrack(
    'Fixture - pad with hole',
    { pad: asset(mesh) },
    [{ id: 'hole-pad', asset: 'pad', x: -30, z: -30, rotation: 0, elevation: 4 }]
  )]);
}

{
  const mesh = new Mesh();
  const a = mesh.addVertex(new Vector2(0, 0));
  const b = mesh.addVertex(new Vector2(30, 0));
  const c = mesh.addVertex(new Vector2(30, 30));
  const d = mesh.addVertex(new Vector2(0, 30));
  const e = mesh.addVertex(new Vector2(60, 0));
  const f = mesh.addVertex(new Vector2(60, 30));
  mesh.addPolygon([a, b, c, d]);
  mesh.addPolygon([b, e, f, c]);
  TrackMesh.railBoundaryEdges(mesh);
  fixtures.push(['shared-seam.json', baseTrack(
    'Fixture - shared interior seam',
    { pad: asset(mesh) },
    [{ id: 'seamed-pad', asset: 'pad', x: -30, z: 0, rotation: 0, elevation: 2 }]
  )]);
}

{
  const mesh = polygonMesh([[0, 0], [60, 0], [60, 20], [20, 20], [20, 60], [0, 60]], true);
  fixtures.push(['concave-railed-pad.json', baseTrack(
    'Fixture - concave railed pad',
    { pad: asset(mesh, 8) },
    [{ id: 'concave-pad', asset: 'pad', x: -30, z: 0, rotation: 0, elevation: 3 }]
  )]);
}

{
  const mesh = square(80);
  fixtures.push(['corridor-mesh-bridge.json', baseTrack(
    'Fixture - corridor mesh bridge',
    { bridge: asset(mesh) },
    [{ id: 'bridge-pad', asset: 'bridge', x: -40, z: -40, rotation: 0, elevation: 0 }]
  )]);
}

{
  const mesh = square(70);
  fixtures.push(['overlapping-elevations.json', baseTrack(
    'Fixture - overlapping elevations',
    { deck: asset(mesh) },
    [
      { id: 'lower-deck', asset: 'deck', x: -35, z: -35, rotation: 0, elevation: 0 },
      { id: 'upper-deck', asset: 'deck', x: -35, z: -35, rotation: 15, elevation: 12 }
    ]
  )]);
}

{
  const mesh = square(100, true);
  fixtures.push(['mesh-effects.json', baseTrack(
    'Fixture - mesh effects',
    { arena: asset(mesh, 7) },
    [{ id: 'arena-placed', asset: 'arena', x: -50, z: -50, rotation: 0, elevation: 5 }],
    {
      zones: [{
        id: 'mesh-boost', effect: 'velocityChange', factor: 1.4, duration: 1.5,
        width: 20, length: 30,
        host: { kind: 'mesh', meshId: 'arena-placed', x: 0, z: 0, rotation: 0 }
      }],
      triggers: [{
        id: 'mesh-finish', type: 'checkpoint', role: 'finish', direction: 'both',
        width: 60, height: 15, rotation: 0,
        host: { kind: 'mesh', meshId: 'arena-placed', x: 0, z: 20 }
      }]
    }
  )]);
}

const summaries = {};
const compiledSummaries = {};
for (const [filename, raw] of fixtures) {
  const normalized = TrackCore.parseTrack(JSON.stringify(raw));
  await writeFile(new URL(filename, outputDir), TrackCore.serializeTrack(normalized) + '\n');
  const assets = Object.values(normalized.meshAssets);
  summaries[filename] = {
    name: normalized.name,
    samples: normalized.samples,
    paths: normalized.paths.length,
    points: normalized.paths.reduce((n, p) => n + p.points.length, 0),
    meshAssets: assets.length,
    meshes: normalized.meshes.length,
    vertices: assets.reduce((n, a) => n + a.mesh.vertices.length, 0),
    edges: assets.reduce((n, a) => n + a.mesh.edges.length, 0),
    railEdges: assets.reduce((n, a) => n + a.mesh.edges.filter(e => e.attributes?.rail).length, 0),
    polygons: assets.reduce((n, a) => n + a.mesh.polygons.length, 0),
    zones: normalized.zones.length,
    triggers: normalized.triggers.length,
    start: normalized.start,
    handling: normalized.handling,
    placements: normalized.meshes.map(m => ({ id: m.id, asset: m.asset, x: m.x, z: m.z, rotation: m.rotation, elevation: m.elevation })),
    effects: normalized.zones.map(z => ({ id: z.id, effect: z.effect, kind: z.host.kind })),
    gates: normalized.triggers.map(t => ({ id: t.id, type: t.type, role: t.role || '', direction: t.direction, kind: t.host.kind }))
  };
  const regions = normalized.meshes.map(placement => {
    const record = normalized.meshAssets[placement.asset];
    const compiled = TrackMesh.compile(TrackMesh.meshFromJSON(record.mesh), placement);
    const triangleArea = compiled.triangles.reduce((sum, [a, b, c]) =>
      sum + Math.abs((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) / 2, 0);
    return {
      id: compiled.id, assetId: compiled.assetId, elevation: compiled.elevation, railHeight: record.railHeight,
      bounds: compiled.bounds,
      polygons: compiled.polygons.map(p => ({ polygonId: p.polygonId, outerCount: p.outer.length,
        holeCounts: p.holes.map(h => h.length) })),
      triangleCount: compiled.triangles.length, triangleArea,
      rails: compiled.rails.map(r => ({ edgeId: r.edgeId, a: [r.a.x, r.a.z], b: [r.b.x, r.b.z],
        normal: [r.nx, r.nz], length: r.len }))
    };
  });
  compiledSummaries[filename] = regions;
  console.log(`wrote test/fixtures/mesh/${filename}`);
}
const expectedDir = new URL('expected/', outputDir);
await mkdir(expectedDir, { recursive: true });
await writeFile(new URL('normalized-summary.json', expectedDir), JSON.stringify(summaries, null, 2) + '\n');
await writeFile(new URL('compiled-summary.json', expectedDir), JSON.stringify(compiledSummaries, null, 2) + '\n');
console.log('wrote test/fixtures/mesh/expected loader and compiled summaries');

// M3 curved/banked path oracle: full current-schema source plus selected baked
// frames and renderer-neutral geometric invariants for native comparison.
globalThis.TrackCore = TrackCore;
const { bakeTrackPhysics } = await import('../js/track-bake.js');
const { buildTrackRenderGeometry } = await import('../js/track-render-geometry.js');
const pathDir = new URL('../test/fixtures/path/', import.meta.url);
await mkdir(new URL('expected/', pathDir), { recursive: true });
const curvePoints = [];
for (let i = 0; i < 8; i++) {
  const a = i / 8 * Math.PI * 2;
  curvePoints.push({ type: 'position', id: `c${i}`, pos: [100 * Math.cos(a), 8 * Math.sin(a * 2), 100 * Math.sin(a)], weight: i === 2 ? 1.4 : 1 });
}
curvePoints.push(
  { type: 'roll', t: 0, roll: 18 }, { type: 'roll', t: 0.5, roll: -12 },
  { type: 'width', t: 0, width: 30 }, { type: 'width', t: 0.5, width: 52 },
  { type: 'crossSection', t: 0, curvature: 0.65, tightness: 1.4, thickness: 5 },
  { type: 'crossSection', t: 0.5, curvature: -0.25, tightness: 2.2, thickness: 3 }
);
const curved = TrackCore.parseTrack(JSON.stringify({
  version: TrackCore.TRACK_SCHEMA_VERSION, name: 'Fixture - curved banked path',
  paths: [{ id: 'curve', closed: true, texture: { asset: 'road-atlas', tile: 1 }, points: curvePoints }],
  textureAssets: { 'road-atlas': { name: 'road.png', path: 'textures/road.png', width: 64, height: 32, tileWidth: 32, tileHeight: 32 } },
  start: { path: 0, point: 1, reverse: true },
  zones: [{ id: 'curve-boost', effect: 'velocityChange', width: 16, length: 28, factor: 1.6, duration: 1.25,
    host: { kind: 'path', pathId: 'curve', t: 0.25, lateral: 2 } }],
  triggers: [{ id: 'curve-finish', type: 'checkpoint', role: 'finish', width: 34, height: 14, rotation: 17,
    direction: 'forward', host: { kind: 'path', pathId: 'curve', t: 0.1 } }]
}));
await writeFile(new URL('curved-banked.json', pathDir), TrackCore.serializeTrack(curved) + '\n');
const bakedCurve = bakeTrackPhysics(curved);
const renderCurve = buildTrackRenderGeometry(curved, bakedCurve);
const v = x => [x.x, x.y, x.z];
const pathSummaries = bakedCurve.paths.map(path => {
  const indices = [0, Math.floor(path.centerline.length / 4), Math.floor(path.centerline.length / 2), path.centerline.length - 1];
  return {
    closed: path.closed, frameCount: path.centerline.length, anchors: path.anchors.map(v),
    frames: indices.map(index => { const f = path.centerline[index]; return { index, pos: v(f.pos), tangent: v(f.tangent),
      edgeRight: v(f.edgeRight), normal: v(f.normal), sLeft: f.sLeft, sRight: f.sRight,
      curvature: f.crossSectionCurvature, tightness: f.crossSectionTightness }; })
  };
});
const geometry = renderCurve.batches.filter(b => b.kind.startsWith('Path') || b.kind === 'ZoneSurface').map(b => {
  const positions = b.vertices.map(x => x.position);
  return { id: b.id, kind: b.kind, hasUv: b.hasUv, texture: b.texture,
    vertexCount: b.vertices.length, indexCount: b.indices.length,
    min: [0, 1, 2].map(k => Math.min(...positions.map(p => p[k]))),
    max: [0, 1, 2].map(k => Math.max(...positions.map(p => p[k]))) };
});
await writeFile(new URL('curved-banked-summary.json', new URL('expected/', pathDir)), JSON.stringify({
  paths: pathSummaries, trackFloorY: bakedCurve.trackFloorY,
  zones: bakedCurve.zones.map(z => ({ id: z.id, kind: z.kind, hostPathIndex: z.hostPathIndex, gLo: z.gLo, gHi: z.gHi, gMax: z.gMax })),
  triggers: bakedCurve.triggers.map(t => ({ id: t.id, center: v(t.center), right: v(t.right), up: v(t.up), fwd: v(t.fwd), halfWidth: t.halfWidth, height: t.height })),
  geometry
}, null, 2) + '\n');
console.log('wrote test/fixtures/path/curved-banked.json and expected summary');
