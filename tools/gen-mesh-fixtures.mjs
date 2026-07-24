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
  console.log(`wrote test/fixtures/mesh/${filename}`);
}
const expectedDir = new URL('expected/', outputDir);
await mkdir(expectedDir, { recursive: true });
await writeFile(new URL('normalized-summary.json', expectedDir), JSON.stringify(summaries, null, 2) + '\n');
console.log('wrote test/fixtures/mesh/expected/normalized-summary.json');
