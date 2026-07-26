// Deterministic random current-schema tracks used by the JS/C++ render-geometry
// parity suite. These are intentionally smaller than the editor's game-sized
// generator, but vary spline shape, banking, width/profile, mesh topology,
// placement transforms, rails, textures, and path/mesh zones.
import { buildTrackRenderGeometry } from '../../js/track-render-geometry.js';

export const RANDOM_TRACK_MESH_CASES = Object.freeze([
  { seed: 0x13579bdf, complexity: 3 },
  { seed: 0x2468ace0, complexity: 5 },
  { seed: 0x0badf00d, complexity: 7 },
  { seed: 0xc001d00d, complexity: 9 },
  { seed: 0x5eed1234, complexity: 10 }
]);

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function polygonRecord(points, firstVertexId, firstEdgeId, polygonId, rail, hole = false) {
  const vertices = points.map((position, i) => ({
    id: firstVertexId + i,
    position: { x: position[0], y: position[1] },
    attributes: {}
  }));
  const edges = points.map((_, i) => {
    const v0 = firstVertexId + i;
    const v1 = firstVertexId + (i + 1) % points.length;
    return {
      id: firstEdgeId + i,
      vertices: [v0, v1],
      attributes: rail(i) ? { rail: true } : {}
    };
  });
  return {
    vertices,
    edges,
    polygon: {
      id: polygonId,
      edges: edges.map((edge, i) => ({
        edge: edge.id,
        v0: firstVertexId + i,
        v1: firstVertexId + (i + 1) % points.length
      })),
      holes: [],
      hole,
      attributes: {}
    }
  };
}

function randomMesh(rnd, complexity, withHole) {
  const outerCount = 5 + (complexity % 4);
  const outer = [];
  for (let i = 0; i < outerCount; i++) {
    const angle = i * Math.PI * 2 / outerCount;
    const radius = 35 + complexity * 2 + rnd() * 14;
    outer.push([Math.cos(angle) * radius, Math.sin(angle) * radius]);
  }
  const outerRecord = polygonRecord(outer, 0, 0, 0, i => ((i + complexity) % 3) !== 1);
  const vertices = [...outerRecord.vertices];
  const edges = [...outerRecord.edges];
  const polygons = [outerRecord.polygon];

  if (withHole) {
    const holeCount = 3 + complexity % 3;
    const hole = [];
    const phase = rnd() * 0.4;
    for (let i = 0; i < holeCount; i++) {
      const angle = phase + i * Math.PI * 2 / holeCount;
      const radius = 9 + rnd() * 4;
      hole.push([Math.cos(angle) * radius, Math.sin(angle) * radius]);
    }
    const holeRecord = polygonRecord(hole, outerCount, outerCount, 1, i => i % 2 === 0, true);
    vertices.push(...holeRecord.vertices);
    edges.push(...holeRecord.edges);
    polygons[0].holes.push(1);
    polygons.push(holeRecord.polygon);
  }
  return { vertices, edges, polygons };
}

function randomPath(rnd, seed, complexity) {
  const count = 6 + complexity % 6;
  const points = [];
  for (let i = 0; i < count; i++) {
    const angle = i * Math.PI * 2 / count + (rnd() - 0.5) * 0.16;
    const radius = 180 + complexity * 13 + (rnd() - 0.5) * 75;
    const y = Math.sin(angle * (2 + seed % 3) + rnd() * 0.2) * (8 + complexity * 2.5);
    points.push({
      type: 'position', id: `seed-${seed}-p${i}`,
      pos: [Math.cos(angle) * radius, y, Math.sin(angle) * radius],
      weight: 0.8 + rnd() * 0.7
    });
  }
  for (const t of [0, 0.27, 0.59, 0.83]) {
    points.push({ type: 'roll', t, roll: (rnd() * 2 - 1) * (8 + complexity * 2) });
    points.push({ type: 'width', t, width: 22 + rnd() * (14 + complexity * 1.5) });
    points.push({
      type: 'crossSection', t,
      curvature: (rnd() * 2 - 1) * Math.min(0.8, 0.2 + complexity * 0.055),
      tightness: 0.55 + rnd() * 2.4,
      thickness: (complexity + Math.round(t * 10)) % 3 === 0 ? 0 : 1.5 + rnd() * 5
    });
  }
  return points;
}

export function generateRandomTrackMeshCase({ seed, complexity }) {
  const TrackCore = globalThis.TrackCore;
  if (!TrackCore) throw new Error('globalThis.TrackCore must be installed first');
  const rnd = mulberry32(seed);
  const assetId = `random-asset-${seed}`;
  const pathId = `random-path-${seed}`;
  const mesh = randomMesh(rnd, complexity, seed % 2 === 0);
  const placements = [{
    id: `random-placement-${seed}-a`, asset: assetId,
    x: (rnd() * 2 - 1) * 130, z: (rnd() * 2 - 1) * 130,
    rotation: (rnd() * 2 - 1) * 175, elevation: (rnd() * 2 - 1) * 18
  }];
  if (complexity >= 7) placements.push({
    id: `random-placement-${seed}-b`, asset: assetId,
    x: (rnd() * 2 - 1) * 170, z: (rnd() * 2 - 1) * 170,
    rotation: (rnd() * 2 - 1) * 175, elevation: 15 + rnd() * 25
  });

  const path = {
    id: pathId,
    closed: true,
    points: randomPath(rnd, seed, complexity)
  };
  const textureAssets = {};
  if (seed & 1) {
    textureAssets[`atlas-${seed}`] = {
      name: `atlas-${seed}.png`, path: `textures/atlas-${seed}.png`,
      width: 128, height: 64, tileWidth: 32, tileHeight: 32
    };
    path.texture = { asset: `atlas-${seed}`, tile: complexity % 8 };
  }

  const source = {
    version: TrackCore.TRACK_SCHEMA_VERSION,
    name: `Random geometry parity ${seed}`,
    samples: 120 + complexity * 17,
    start: { path: 0, point: complexity % (6 + complexity % 6), reverse: !!(seed & 2) },
    handling: {
      maxSpeed: 110 + complexity * 5,
      accel: 55 + complexity * 3,
      turnSpeed: 100 + complexity * 7,
      weight: 700 + complexity * 90
    },
    disjointSeams: [], junctions: [], selfIntersectionOverrides: [],
    textureAssets,
    meshAssets: {
      [assetId]: {
        name: `Seeded polygon ${seed}`,
        railHeight: 3 + rnd() * 7,
        mesh
      }
    },
    meshes: placements,
    paths: [path],
    zones: [
      {
        id: `random-path-zone-${seed}`, effect: 'velocityChange',
        width: 8 + rnd() * 16, length: 18 + rnd() * 35,
        factor: 1.2 + rnd() * 0.6, duration: 0.5 + rnd() * 2,
        host: { kind: 'path', pathId, t: 0.1 + rnd() * 0.8, lateral: (rnd() * 2 - 1) * 4 }
      },
      {
        id: `random-mesh-zone-${seed}`, effect: 'startGrid',
        width: 8 + rnd() * 15, length: 12 + rnd() * 25,
        host: {
          kind: 'mesh', meshId: placements[0].id,
          x: placements[0].x, z: placements[0].z, rotation: placements[0].rotation + 13
        }
      }
    ],
    triggers: [{
      id: `random-finish-${seed}`, type: 'checkpoint', role: 'finish',
      width: 24 + rnd() * 15, height: 8 + rnd() * 9,
      rotation: (rnd() * 2 - 1) * 25, direction: 'forward',
      host: { kind: 'path', pathId, t: 0.05 + rnd() * 0.2 }
    }]
  };
  return TrackCore.parseTrack(JSON.stringify(source));
}

function vectorStats(batch) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  const uvMin = [Infinity, Infinity];
  const uvMax = [-Infinity, -Infinity];
  for (const vertex of batch.vertices) {
    for (let k = 0; k < 3; k++) {
      min[k] = Math.min(min[k], vertex.position[k]);
      max[k] = Math.max(max[k], vertex.position[k]);
    }
    for (let k = 0; k < 2; k++) {
      uvMin[k] = Math.min(uvMin[k], vertex.uv[k]);
      uvMax[k] = Math.max(uvMax[k], vertex.uv[k]);
    }
  }

  let area = 0;
  const firstMoment = [0, 0, 0];
  const orientedArea = [0, 0, 0];
  for (let i = 0; i < batch.indices.length; i += 3) {
    const a = batch.vertices[batch.indices[i]].position;
    const b = batch.vertices[batch.indices[i + 1]].position;
    const c = batch.vertices[batch.indices[i + 2]].position;
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const cross = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
    const triangleArea = Math.hypot(...cross) / 2;
    area += triangleArea;
    for (let k = 0; k < 3; k++) {
      firstMoment[k] += triangleArea * (a[k] + b[k] + c[k]) / 3;
      orientedArea[k] += cross[k] / 2;
    }
  }
  const centroid = area > 0 ? firstMoment.map(value => value / area) : [0, 0, 0];
  return { min, max, area, centroid, orientedArea, uvMin, uvMax };
}

export function summarizeRandomTrackMesh(track) {
  const built = buildTrackRenderGeometry(track);
  return {
    pathCount: track.paths.length,
    placementCount: track.meshes.length,
    batchCount: built.batches.length,
    warningCount: built.warnings.length,
    batches: built.batches.map(batch => ({
      id: batch.id,
      kind: batch.kind,
      materialKey: batch.materialKey,
      vertexCount: batch.vertices.length,
      indexCount: batch.indices.length,
      hasUv: batch.hasUv,
      texture: batch.texture,
      ...vectorStats(batch)
    }))
  };
}

export function generateRandomTrackMeshCorpus() {
  return RANDOM_TRACK_MESH_CASES.map(options => {
    const track = generateRandomTrackMeshCase(options);
    return {
      ...options,
      file: `random-track-mesh-${options.seed}.json`,
      json: globalThis.TrackCore.serializeTrack(track) + '\n',
      summary: summarizeRandomTrackMesh(track)
    };
  });
}
