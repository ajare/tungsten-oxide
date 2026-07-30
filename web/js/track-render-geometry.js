/* Graphics-API-agnostic track geometry shared by headless tests and the C++
 * port oracle. Every batch is triangle-indexed and carries position, normal,
 * UV (when meaningful), and opaque-white RGBA. Renderer material policy is a
 * semantic key rather than baked display colour.
 */
import { bakeTrackPhysics } from './track-bake.js';
import { curvedSurfaceFrame } from './track-physics.js';

const TC = () => globalThis.TrackCore;
const WHITE = Object.freeze([1, 1, 1, 1]);
const ZONE_HOVER = 0.15;
const ZONE_CHECKER = 3;
const PATH_RAIL_HEIGHT = 1.8;
const PATH_RAIL_LIFT = 0.04;

function unionBreakpoints(a, b) {
  const set = new Set(a);
  for (const v of b) set.add(v);
  return [...set].sort((x, y) => x - y);
}

function vec(p) {
  return Array.isArray(p) ? [p[0], p[1], p[2]] : [p.x, p.y, p.z];
}

function triangleNormal(a, b, c) {
  const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
  // cross(v, u), not cross(u, v) -- mirrors cpp/core/src/TrackMesh.cpp's normalOf() (see its
  // comment): for the (a,b,c) vertex order this is always called with, cross(u,v) works out to
  // cross(edgeRight, tangent), which is provably always -normal (into the ground), not +normal.
  let x = vy * uz - vz * uy, y = vz * ux - vx * uz, z = vx * uy - vy * ux;
  const len = Math.hypot(x, y, z);
  if (len < 1e-12) return [0, 1, 0];
  x /= len; y /= len; z /= len;
  return [x, y, z];
}

function batch(id, kind, materialKey, triangles, { hasUv = false, texture = null } = {}) {
  const vertices = [], indices = [];
  for (const tri of triangles) {
    const points = tri.map(v => vec(v.position));
    const normal = triangleNormal(points[0], points[1], points[2]);
    for (let i = 0; i < 3; i++) {
      vertices.push({
        position: points[i], normal: [...normal],
        uv: hasUv ? [...(tri[i].uv || [0, 0])] : [0, 0], rgba: [...WHITE]
      });
      indices.push(indices.length);
    }
  }
  return { id, kind, materialKey, vertices, indices, hasUv, texture };
}

const pv = (position, uv = null) => ({ position, uv });
const addTri = (out, a, b, c, auv = null, buv = null, cuv = null) => out.push([pv(a, auv), pv(b, buv), pv(c, cuv)]);
const addQuad = (out, a, b, c, d) => { addTri(out, a, b, c); addTri(out, b, d, c); };

function pathBatches(track, path, pathIndex) {
  const TrackCore = TC();
  const def = path._renderDefinition;
  const raw = path._renderRaw, edges = path._renderEdges;
  if (!def || !raw || !edges || raw.length < 2) return [];
  const meshBake = TrackCore.buildAdaptiveMeshFrames(
    def.controlPoints, def.closed, def.rollPoints, def.widthPoints, def.crossSectionPoints, raw, edges
  );
  const frames = meshBake.frames, meshEdges = meshBake.edges;
  const n = frames.length, segCount = def.closed ? n : n - 1;
  const surfacePoint = (i, v) => {
    const left = meshEdges.left[i], right = meshEdges.right[i], f = frames[i];
    const chord = { x: right.x - left.x, y: right.y - left.y, z: right.z - left.z };
    const width = Math.hypot(chord.x, chord.y, chord.z) || 1;
    const h = TrackCore.crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, width);
    return [left.x + chord.x * v + f.normal.x * h,
      left.y + chord.y * v + f.normal.y * h,
      left.z + chord.z * v + f.normal.z * h];
  };
  const ringBreaks = frames.map((f, i) => {
    const l = meshEdges.left[i], r = meshEdges.right[i];
    return TrackCore.crossSectionBreakpoints(
      f.crossSectionCurvature, f.crossSectionTightness,
      Math.hypot(r.x - l.x, r.y - l.y, r.z - l.z) || 1
    );
  });
  const ringPoint = (i, v) => TrackCore.crossSectionStitchPoint(ringBreaks[i], v, vv => surfacePoint(i, vv));
  const distances = [0];
  for (let i = 1; i < n; i++) distances[i] = distances[i - 1] + Math.hypot(
    frames[i].pos.x - frames[i - 1].pos.x,
    frames[i].pos.y - frames[i - 1].pos.y,
    frames[i].pos.z - frames[i - 1].pos.z
  );
  const avgWidth = frames.reduce((sum, f) => sum + Math.max(1, f.width || 1), 0) / Math.max(1, n);
  const surface = [];
  for (let i = 0; i < segCount; i++) {
    const ni = def.closed ? (i + 1) % n : i + 1;
    const breaks = unionBreakpoints(ringBreaks[i], ringBreaks[ni]);
    const t0 = distances[i] / avgWidth;
    const t1 = def.closed && ni === 0
      ? (distances[i] + Math.hypot(frames[ni].pos.x - frames[i].pos.x,
        frames[ni].pos.y - frames[i].pos.y, frames[ni].pos.z - frames[i].pos.z)) / avgWidth
      : distances[ni] / avgWidth;
    for (let k = 0; k < breaks.length - 1; k++) {
      const v0 = breaks[k], v1 = breaks[k + 1];
      const a = ringPoint(i, v0), b = ringPoint(i, v1), c = ringPoint(ni, v0), d = ringPoint(ni, v1);
      addTri(surface, a, b, c, [v0, t0], [v1, t0], [v0, t1]);
      addTri(surface, b, d, c, [v1, t0], [v1, t1], [v0, t1]);
    }
  }
  const texture = def.texture ? { assetId: def.texture.asset, tile: def.texture.tile } : null;
  const out = [batch(`path-${pathIndex}-surface`, 'PathSurface', 'road', surface, { hasUv: true, texture })];

  const underPoint = (i, v) => {
    const s = surfacePoint(i, v), f = frames[i], thickness = f.crossSectionThickness || 0;
    return [s[0] - f.normal.x * thickness, s[1] - f.normal.y * thickness, s[2] - f.normal.z * thickness];
  };
  const ringUnderPoint = (i, v) => TrackCore.crossSectionStitchPoint(ringBreaks[i], v, vv => underPoint(i, vv));
  if (frames.some(f => (f.crossSectionThickness || 0) > 1e-6)) {
    const shell = [];
    for (let i = 0; i < segCount; i++) {
      const ni = def.closed ? (i + 1) % n : i + 1;
      const breaks = unionBreakpoints(ringBreaks[i], ringBreaks[ni]);
      for (let k = 0; k < breaks.length - 1; k++) {
        const v0 = breaks[k], v1 = breaks[k + 1];
        const a = ringUnderPoint(i, v0), b = ringUnderPoint(i, v1), c = ringUnderPoint(ni, v0), d = ringUnderPoint(ni, v1);
        addTri(shell, a, c, b); addTri(shell, b, c, d);
      }
      addQuad(shell, surfacePoint(i, 0), underPoint(i, 0), surfacePoint(ni, 0), underPoint(ni, 0));
      addQuad(shell, underPoint(i, 1), surfacePoint(i, 1), underPoint(ni, 1), surfacePoint(ni, 1));
    }
    if (!def.closed) for (const end of [0, n - 1]) {
      const breaks = ringBreaks[end];
      for (let k = 0; k < breaks.length - 1; k++) addQuad(shell,
        surfacePoint(end, breaks[k]), underPoint(end, breaks[k]),
        surfacePoint(end, breaks[k + 1]), underPoint(end, breaks[k + 1]));
    }
    out.push(batch(`path-${pathIndex}-shell`, 'PathShell', 'Tracks/DefaultShellMaterial', shell));
  }

  for (const [side, sideKey] of [['left', 'sLeft'], ['right', 'sRight']]) {
    const pairs = path.centerline.map(frame => {
      const surfaceFrame = curvedSurfaceFrame(frame, frame[sideKey]);
      const base = surfaceFrame.pos.clone().addScaledVector(surfaceFrame.normal, PATH_RAIL_LIFT);
      const top = base.clone().addScaledVector(surfaceFrame.normal, PATH_RAIL_HEIGHT);
      return [vec(base), vec(top)];
    });
    const rails = [];
    const railSegments = path.closed ? pairs.length : pairs.length - 1;
    for (let i = 0; i < railSegments; i++) {
      const j = path.closed ? (i + 1) % pairs.length : i + 1;
      addTri(rails, pairs[i][0], pairs[j][0], pairs[i][1]);
      addTri(rails, pairs[i][1], pairs[j][0], pairs[j][1]);
    }
    out.push(batch(`path-${pathIndex}-rail-${side}`, 'PathRail', 'Tracks/DefaultRailMaterial', rails));
  }
  return out;
}

function meshBatches(region, index) {
  const surface = [];
  for (const tri of region.compiled.triangles) if (tri.length === 3) addTri(surface,
    [tri[0].x, region.elevation, tri[0].z], [tri[1].x, region.elevation, tri[1].z], [tri[2].x, region.elevation, tri[2].z]);
  const rails = [];
  for (const rail of region.compiled.rails) {
    const a = [rail.a.x, region.elevation, rail.a.z], b = [rail.b.x, region.elevation, rail.b.z];
    const at = [rail.a.x, region.elevation + region.railHeight, rail.a.z];
    const bt = [rail.b.x, region.elevation + region.railHeight, rail.b.z];
    addTri(rails, a, b, bt); addTri(rails, a, bt, at);
  }
  const id = region.compiled.id || index;
  return [
    batch(`mesh-${id}-surface`, 'MeshSurface', 'Tracks/DefaultMeshMaterial', surface),
    batch(`mesh-${id}-rails`, 'MeshRail', 'Tracks/DefaultRailMaterial', rails)
  ];
}

function zoneBatch(zone) {
  const triangles = [];
  const uvScale = 1 / (2 * ZONE_CHECKER);
  if (zone.kind === 'mesh') {
    const cos = Math.cos(zone.rot), sin = Math.sin(zone.rot), y = zone.hostRegion.elevation + ZONE_HOVER;
    const corner = (x, z) => [zone.x + x * cos - z * sin, y, zone.z + x * sin + z * cos];
    const a = corner(-zone.halfLen, -zone.halfWidth), b = corner(zone.halfLen, -zone.halfWidth);
    const c = corner(zone.halfLen, zone.halfWidth), d = corner(-zone.halfLen, zone.halfWidth);
    addTri(triangles, a, b, c, [-zone.halfLen * uvScale, -zone.halfWidth * uvScale],
      [zone.halfLen * uvScale, -zone.halfWidth * uvScale], [zone.halfLen * uvScale, zone.halfWidth * uvScale]);
    addTri(triangles, a, c, d, [-zone.halfLen * uvScale, -zone.halfWidth * uvScale],
      [zone.halfLen * uvScale, zone.halfWidth * uvScale], [-zone.halfLen * uvScale, zone.halfWidth * uvScale]);
  } else {
    const rows = zone.renderRows || [], distances = [0];
    for (let i = 1; i < rows.length; i++) {
      const a = rows[i - 1][0], b = rows[i][0];
      distances[i] = distances[i - 1] + Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
    }
    for (let i = 0; i < rows.length - 1; i++) for (let j = 0; j < Math.min(rows[i].length, rows[i + 1].length) - 1; j++) {
      const across = Math.min(rows[i].length, rows[i + 1].length);
      const u0 = distances[i] * uvScale, u1 = distances[i + 1] * uvScale;
      const v0 = (2 * zone.halfWidth) * uvScale * j / (across - 1);
      const v1 = (2 * zone.halfWidth) * uvScale * (j + 1) / (across - 1);
      const a = vec(rows[i][j]), b = vec(rows[i][j + 1]), c = vec(rows[i + 1][j]), d = vec(rows[i + 1][j + 1]);
      addTri(triangles, a, b, c, [u0, v0], [u0, v1], [u1, v0]);
      addTri(triangles, b, d, c, [u0, v1], [u1, v1], [u1, v0]);
    }
  }
  return batch(`zone-${zone.id}`, 'ZoneSurface', 'Tracks/DefaultZoneMaterial', triangles, { hasUv: true });
}

// Gate quad matching track-game.js's buildTriggerDebugMesh corners exactly (c0=(-1,0), c1=(1,0),
// c2=(1,1), c3=(-1,1) in right/up space, same two-triangle split) -- this is the renderer-neutral
// counterpart of that debug-only three.js quad, not a new shape.
function triggerBatch(trigger) {
  const { center: c, right: r, up: u, halfWidth: hw, height: h } = trigger;
  const corner = (sr, su) => [c.x + r.x * sr * hw + u.x * su * h, c.y + r.y * sr * hw + u.y * su * h, c.z + r.z * sr * hw + u.z * su * h];
  const c0 = corner(-1, 0), c1 = corner(1, 0), c2 = corner(1, 1), c3 = corner(-1, 1);
  const triangles = [];
  // In the editor's XZ track convention +right is the driver's left-hand side when looking
  // along fwd. Start U there so trigger textures read left-to-right in the direction of travel.
  addTri(triangles, c0, c1, c2, [1, 0], [0, 0], [0, 1]);
  addTri(triangles, c0, c2, c3, [1, 0], [0, 1], [1, 1]);
  return batch(`trigger-${trigger.id}`, 'TriggerSurface', 'Tracks/DefaultTriggerMaterial', triangles, { hasUv: true });
}

export function buildTrackRenderGeometry(track, bakedWorld = bakeTrackPhysics(track)) {
  const batches = [];
  bakedWorld.paths.forEach((path, i) => batches.push(...pathBatches(track, path, i)));
  bakedWorld.meshRegions.forEach((region, i) => batches.push(...meshBatches(region, i)));
  bakedWorld.zones.forEach(zone => batches.push(zoneBatch(zone)));
  bakedWorld.triggers.forEach(trigger => batches.push(triggerBatch(trigger)));
  return { batches, warnings: [...(bakedWorld.warnings || [])] };
}
