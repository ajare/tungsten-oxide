/* usd-export.js — ASCII USD (.usda) export for authored tracks.
 * Pure browser/Node module: no three.js. TrackCore is passed in because
 * track-core.js is deliberately a classic script, while mesh regions use the
 * shared TrackMesh module directly. The exported road surface is built from
 * TrackCore's own centerline, edge and cross-section math, so an exported track
 * is the same geometry the editor previewed and the game drives on.
 */
import * as TrackMesh from './track-mesh.js';

const DEFAULT_CROSS_SECTION_SEGMENTS = 24;
const ROAD_MATERIAL = 'RoadSurface';
const MESH_MATERIAL = 'MeshRegionSurface';
const SHELL_MATERIAL = 'RoadShell';

export function sanitizeUsdIdentifier(value, fallback = 'Prim') {
  let s = String(value || '').replace(/[^A-Za-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
  if (!s) s = fallback;
  if (!/^[A-Za-z_]/.test(s)) s = '_' + s;
  return s;
}

export function sanitizeFileStem(value, fallback = 'track') {
  return String(value || fallback).trim().toLowerCase()
    .replace(/[^a-z0-9._-]+/g, '-')
    .replace(/^-+|-+$/g, '') || fallback;
}

const fmt = n => {
  const v = Math.abs(n) < 1e-12 ? 0 : n;
  return Number.isFinite(v) ? Number(v.toFixed(6)).toString() : '0';
};
const pointText = p => `(${fmt(p[0])}, ${fmt(p[1])}, ${fmt(p[2])})`;
const texCoordText = uv => `(${fmt(uv[0])}, ${fmt(uv[1])})`;

function uniqueName(base, used) {
  const clean = sanitizeUsdIdentifier(base, 'Prim');
  if (!used.has(clean)) { used.add(clean); return clean; }
  for (let i = 2; ; i++) {
    const candidate = `${clean}_${i}`;
    if (!used.has(candidate)) { used.add(candidate); return candidate; }
  }
}

function triangleNormalY(points, ia, ib, ic) {
  const a = points[ia], b = points[ib], c = points[ic];
  const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
  return uz * vx - ux * vz; // y component of u x v
}

function orientFacesUp(points, faces) {
  let sumY = 0;
  for (const f of faces) sumY += triangleNormalY(points, f[0], f[1], f[2]);
  if (sumY < 0) for (const f of faces) [f[1], f[2]] = [f[2], f[1]];
}

function decidersForPath(TrackCore, path, track, samples) {
  if (!TrackCore.makeSelfIntersectionDeciders) return null;
  const overrides = Array.isArray(track.selfIntersectionOverrides) ? track.selfIntersectionOverrides : [];
  return TrackCore.makeSelfIntersectionDeciders(path.controlPoints, path.closed, samples, overrides);
}

function buildCurveMesh(TrackCore, track, path, pathIndex, crossSectionSegments, warnings) {
  const parts = TrackCore.splitPoints(path.points || []);
  const cps = parts.controlPoints;
  if (cps.length < 2 || (path.closed !== false && cps.length < 3)) {
    warnings.push(`Skipped path ${pathIndex}: not enough position points.`);
    return null;
  }
  const samples = Math.max(2, Math.floor(track.samples || TrackCore.N_DEFAULT || 400));
  const closed = path.closed !== false;
  let raw, edges;
  try {
    raw = TrackCore.buildCenterline(cps, samples, closed, parts.rollPoints, parts.widthPoints, parts.crossSectionPoints);
    edges = TrackCore.buildEdges(raw, closed);
    if (TrackCore.removeLocalEdgeSelfIntersections) {
      const deciders = decidersForPath(TrackCore, { controlPoints: cps, closed }, track, samples);
      edges = TrackCore.removeLocalEdgeSelfIntersections(
        edges, closed, false,
        deciders && deciders.decideLeft, deciders && deciders.decideRight, deciders && deciders.scanSpan
      );
    }
  } catch (err) {
    warnings.push(`Skipped path ${pathIndex}: ${err.message || err}.`);
    return null;
  }

  const points = [];
  const uvs = [];
  const ringSize = crossSectionSegments + 1;
  const chordWidths = raw.map((_, i) => {
    const left = edges.left[i], right = edges.right[i];
    return Math.hypot(right.x - left.x, right.y - left.y, right.z - left.z) || 1;
  });
  const representativeWidth = chordWidths.reduce((sum, w) => sum + w, 0) / Math.max(1, chordWidths.length);
  const distances = [0];
  for (let i = 1; i < raw.length; i++) {
    const a = raw[i - 1].pos, b = raw[i].pos;
    distances[i] = distances[i - 1] + Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
  }
  for (let i = 0; i < raw.length; i++) {
    const left = edges.left[i], right = edges.right[i], f = raw[i];
    const chord = { x: right.x - left.x, y: right.y - left.y, z: right.z - left.z };
    const chordWidth = chordWidths[i];
    const texV = distances[i] / representativeWidth;
    for (let j = 0; j <= crossSectionSegments; j++) {
      const v = j / crossSectionSegments;
      const h = TrackCore.crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, chordWidth);
      points.push([
        left.x + chord.x * v + f.normal.x * h,
        left.y + chord.y * v + f.normal.y * h,
        left.z + chord.z * v + f.normal.z * h
      ]);
      uvs.push([v, texV]);
    }
  }

  const faces = [];
  const longSegments = closed ? raw.length : raw.length - 1;
  for (let i = 0; i < longSegments; i++) {
    const ni = closed ? (i + 1) % raw.length : i + 1;
    for (let j = 0; j < crossSectionSegments; j++) {
      const a = i * ringSize + j;
      const b = i * ringSize + j + 1;
      const c = ni * ringSize + j;
      const d = ni * ringSize + j + 1;
      faces.push([a, b, c], [b, d, c]);
    }
  }
  orientFacesUp(points, faces);
  const surface = { name: `Path_${pathIndex}`, material: ROAD_MATERIAL, points, faces, uvs };
  const shell = buildCurveShellMesh(raw, points, ringSize, crossSectionSegments, closed, pathIndex);
  return shell ? [surface, shell] : [surface];
}

/* The underside and side walls of an extruded road, as a prim of their own.
 *
 * Separate from the surface for the same reason the game keeps them in a
 * separate mesh: the road's UV mapping stays untouched, and the substructure
 * gets its own material. `topPoints` is reused rather than recomputed, so the
 * shell is welded to the exact surface that was exported -- including any edge
 * mitring already baked into it.
 *
 * Winding is settled here rather than by orientFacesUp, which cannot work on a
 * closed shell: it flips everything when the summed face normal points down,
 * and on a shell the top and bottom faces cancel, leaving the decision to
 * numerical noise. The underside faces alone decide instead, and the walls
 * follow whatever they choose.
 */
function buildCurveShellMesh(raw, topPoints, ringSize, crossSectionSegments, closed, pathIndex) {
  if (!raw.some(f => (f.crossSectionThickness || 0) > 1e-6)) return null;

  const points = topPoints.map(p => [p[0], p[1], p[2]]);
  const under = topPoints.length;                  // index offset of the under ring
  for (let i = 0; i < raw.length; i++) {
    const f = raw[i];
    const t = f.crossSectionThickness || 0;
    for (let j = 0; j <= crossSectionSegments; j++) {
      const p = topPoints[i * ringSize + j];
      points.push([p[0] - f.normal.x * t, p[1] - f.normal.y * t, p[2] - f.normal.z * t]);
    }
  }

  const bottom = [];
  const sides = [];
  const longSegments = closed ? raw.length : raw.length - 1;
  for (let i = 0; i < longSegments; i++) {
    const ni = closed ? (i + 1) % raw.length : i + 1;
    for (let j = 0; j < crossSectionSegments; j++) {
      const a = under + i * ringSize + j;
      const b = under + i * ringSize + j + 1;
      const c = under + ni * ringSize + j;
      const d = under + ni * ringSize + j + 1;
      bottom.push([a, c, b], [b, c, d]);           // reversed vs the surface
    }
    for (const j of [0, crossSectionSegments]) {   // the two edges become walls
      const t0 = i * ringSize + j, t1 = ni * ringSize + j;
      const u0 = under + t0, u1 = under + t1;
      sides.push([t0, u0, t1], [u0, u1, t1]);
    }
  }
  // An open curve is a cut slab and needs both ends capped; a loop wraps shut.
  if (!closed) {
    for (const end of [0, raw.length - 1]) {
      for (let j = 0; j < crossSectionSegments; j++) {
        const t0 = end * ringSize + j, t1 = end * ringSize + j + 1;
        sides.push([t0, under + t0, t1], [under + t0, under + t1, t1]);
      }
    }
  }

  let sumY = 0;
  for (const f of bottom) sumY += triangleNormalY(points, f[0], f[1], f[2]);
  const faces = bottom.concat(sides);
  if (sumY > 0) for (const f of faces) [f[1], f[2]] = [f[2], f[1]];   // underside must face down
  return { name: `Path_${pathIndex}_Shell`, material: SHELL_MATERIAL, points, faces };
}

function buildMeshRegionMesh(track, placement, placementIndex, warnings) {
  const asset = track.meshAssets && track.meshAssets[placement.asset];
  if (!asset) {
    warnings.push(`Skipped mesh placement ${placement.id || placementIndex}: missing asset ${placement.asset}.`);
    return null;
  }
  try {
    const mesh = TrackMesh.meshFromJSON(asset.mesh || asset);
    const compiled = TrackMesh.compile(mesh, placement);
    const points = [];
    const faces = [];
    for (const tri of compiled.triangles) {
      if (tri.length !== 3) continue;
      const base = points.length;
      for (const p of tri) points.push([p.x, compiled.elevation, p.z]);
      faces.push([base, base + 1, base + 2]);
    }
    if (!faces.length) {
      warnings.push(`Skipped mesh placement ${placement.id || placementIndex}: no triangles.`);
      return null;
    }
    orientFacesUp(points, faces);
    return { name: `MeshRegion_${placement.id || placementIndex}`, material: MESH_MATERIAL, points, faces };
  } catch (err) {
    warnings.push(`Skipped mesh placement ${placement.id || placementIndex}: ${err.message || err}.`);
    return null;
  }
}

function writeMeshPrim(lines, mesh, usedNames) {
  const name = uniqueName(mesh.name, usedNames);
  lines.push(`    def Mesh "${name}" (`);
  lines.push(`        prepend apiSchemas = ["MaterialBindingAPI"]`);
  lines.push(`    )`);
  lines.push('    {');
  lines.push(`        rel material:binding = </Track/Materials/${mesh.material}>`);
  lines.push(`        point3f[] points = [${mesh.points.map(pointText).join(', ')}]`);
  lines.push(`        int[] faceVertexCounts = [${mesh.faces.map(() => '3').join(', ')}]`);
  lines.push(`        int[] faceVertexIndices = [${mesh.faces.flat().join(', ')}]`);
  if (mesh.uvs && mesh.uvs.length === mesh.points.length) {
    lines.push(`        texCoord2f[] primvars:st = [${mesh.uvs.map(texCoordText).join(', ')}]`);
    lines.push('        uniform token primvars:st:interpolation = "vertex"');
  }
  lines.push('        uniform token subdivisionScheme = "none"');
  lines.push('    }');
}

function writeMaterials(lines) {
  lines.push('    def Scope "Materials"');
  lines.push('    {');
  lines.push(`        def Material "${ROAD_MATERIAL}"`);
  lines.push('        {');
  lines.push('            def Shader "PreviewSurface"');
  lines.push('            {');
  lines.push('                uniform token info:id = "UsdPreviewSurface"');
  lines.push('                color3f inputs:diffuseColor = (0.32, 0.55, 0.65)');
  lines.push('                float inputs:roughness = 0.75');
  lines.push('                token outputs:surface');
  lines.push('            }');
  lines.push('            token outputs:surface.connect = </Track/Materials/RoadSurface/PreviewSurface.outputs:surface>');
  lines.push('        }');
  lines.push(`        def Material "${MESH_MATERIAL}"`);
  lines.push('        {');
  lines.push('            def Shader "PreviewSurface"');
  lines.push('            {');
  lines.push('                uniform token info:id = "UsdPreviewSurface"');
  lines.push('                color3f inputs:diffuseColor = (0.42, 0.31, 0.59)');
  lines.push('                float inputs:roughness = 0.8');
  lines.push('                token outputs:surface');
  lines.push('            }');
  lines.push('            token outputs:surface.connect = </Track/Materials/MeshRegionSurface/PreviewSurface.outputs:surface>');
  lines.push('        }');
  lines.push(`        def Material "${SHELL_MATERIAL}"`);
  lines.push('        {');
  lines.push('            def Shader "PreviewSurface"');
  lines.push('            {');
  lines.push('                uniform token info:id = "UsdPreviewSurface"');
  lines.push('                color3f inputs:diffuseColor = (0.23, 0.36, 0.45)');
  lines.push('                float inputs:roughness = 0.9');
  lines.push('                token outputs:surface');
  lines.push('            }');
  lines.push(`            token outputs:surface.connect = </Track/Materials/${SHELL_MATERIAL}/PreviewSurface.outputs:surface>`);
  lines.push('        }');
  lines.push('    }');
}

export function buildUsdScene(track, options = {}) {
  const TrackCore = options.TrackCore || globalThis.TrackCore;
  if (!TrackCore) throw new Error('TrackCore is required for USD export');
  const crossSectionSegments = Math.max(1, Math.floor(options.crossSectionSegments || DEFAULT_CROSS_SECTION_SEGMENTS));
  const warnings = [];
  const meshes = [];

  (track.paths || []).forEach((path, i) => {
    // A curve yields its road surface and, when extruded, a shell prim with it.
    const built = buildCurveMesh(TrackCore, track, path, i, crossSectionSegments, warnings);
    if (built) meshes.push(...built);
  });
  (track.meshes || []).forEach((placement, i) => {
    const mesh = buildMeshRegionMesh(track, placement, i, warnings);
    if (mesh) meshes.push(mesh);
  });

  const lines = [
    '#usda 1.0',
    '(',
    '    defaultPrim = "Track"',
    '    metersPerUnit = 1',
    '    upAxis = "Y"',
    ')',
    ''
  ];
  for (const w of warnings) lines.push(`# WARNING: ${w}`);
  if (warnings.length) lines.push('');
  lines.push('def Xform "Track"');
  lines.push('{');
  lines.push(`    custom string trackName = ${JSON.stringify(track.name || 'Untitled Track')}`);
  writeMaterials(lines);
  const usedNames = new Set(['Materials']);
  for (const mesh of meshes) writeMeshPrim(lines, mesh, usedNames);
  lines.push('}');
  lines.push('');
  return { text: lines.join('\n'), warnings, meshCount: meshes.length, meshes };
}

export function exportTrackToUSDA(track, options = {}) {
  return buildUsdScene(track, options).text;
}

export const DEFAULT_USD_CROSS_SECTION_SEGMENTS = DEFAULT_CROSS_SECTION_SEGMENTS;
