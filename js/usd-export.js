/* usd-export.js — ASCII USD (.usda) export for authored tracks.
 * Pure browser/Node module: no three.js. TrackCore is passed in because
 * track-core.js is deliberately a classic script, while mesh regions use the
 * shared TrackMesh module directly.
 */
import * as TrackMesh from './track-mesh.js';

const DEFAULT_CROSS_SECTION_SEGMENTS = 24;
const ROAD_MATERIAL = 'RoadSurface';
const MESH_MATERIAL = 'MeshRegionSurface';

const clampSignedUnit = n => (typeof n === 'number' && isFinite(n) ? Math.max(-1, Math.min(1, n)) : 0);
const clampTightness = n => (typeof n === 'number' && isFinite(n) ? Math.max(0.2, Math.min(4, n)) : 1);

function crossSectionHeight(curvature, tightness, v, chordWidth) {
  const c = clampSignedUnit(curvature);
  if (Math.abs(c) < 1e-6) return 0;
  const q = 1 - Math.pow(Math.abs(v * 2 - 1), clampTightness(tightness));
  return c * chordWidth * 0.5 * q;
}

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
  const ringSize = crossSectionSegments + 1;
  for (let i = 0; i < raw.length; i++) {
    const left = edges.left[i], right = edges.right[i], f = raw[i];
    const chord = { x: right.x - left.x, y: right.y - left.y, z: right.z - left.z };
    const chordWidth = Math.hypot(chord.x, chord.y, chord.z) || 1;
    for (let j = 0; j <= crossSectionSegments; j++) {
      const v = j / crossSectionSegments;
      const h = crossSectionHeight(f.crossSectionCurvature, f.crossSectionTightness, v, chordWidth);
      points.push([
        left.x + chord.x * v + f.normal.x * h,
        left.y + chord.y * v + f.normal.y * h,
        left.z + chord.z * v + f.normal.z * h
      ]);
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
  return { name: `Path_${pathIndex}`, material: ROAD_MATERIAL, points, faces };
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
  lines.push('    }');
}

export function buildUsdScene(track, options = {}) {
  const TrackCore = options.TrackCore || globalThis.TrackCore;
  if (!TrackCore) throw new Error('TrackCore is required for USD export');
  const crossSectionSegments = Math.max(1, Math.floor(options.crossSectionSegments || DEFAULT_CROSS_SECTION_SEGMENTS));
  const warnings = [];
  const meshes = [];

  (track.paths || []).forEach((path, i) => {
    const mesh = buildCurveMesh(TrackCore, track, path, i, crossSectionSegments, warnings);
    if (mesh) meshes.push(mesh);
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
