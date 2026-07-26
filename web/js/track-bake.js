/* track-bake.js — THREE-free baking of a normalized track object into the
 * world-space physics data a Simulation (js/track-physics.js) consumes: the
 * per-path centerline of Vec3 frames, plus `connectedEndpointIds` and the
 * `trackFloorY` respawn threshold.
 *
 * This is a faithful extraction of the physics-relevant half of track-game.js's
 * buildTrack()/buildPath() — the same TrackCore calls in the same order, so a
 * track baked here produces byte-identical centerline frames to the ones the
 * game builds inline (the game additionally builds THREE meshes on top). The
 * headless physics tests and the C++ parity trace generator bake through here so
 * they drive the exact same corridor the shipping game does.
 *
 * Mesh placements are compiled through track-mesh.js as well, including their
 * finite rails, mesh-hosted zones/triggers, and contribution to trackFloorY.
 * The game and headless tests therefore consume the same complete physics world.
 */

import { Vec3 } from './vec3.js';
import { RESPAWN_FALL_DEPTH } from './track-physics.js';
import * as TrackMesh from './track-mesh.js';

const TC = () => /** @type {any} */ (globalThis).TrackCore;
const toVec = o => new Vec3(o.x, o.y, o.z);

// Mirror of track-game.js: how far a path zone's detection strip hovers above the
// surface. Only affects the rendered/preview strip; detection uses the g-window.
const ZONE_HOVER = 0.15;

// --- shared-endpoint / branch bookkeeping (ported verbatim, all pure) --------
function sharedEndpointGroups(bakedPaths) {
  const groups = new Map();
  const add = (id, pathIndex, end) => {
    if (!id) return;
    if (!groups.has(id)) groups.set(id, []);
    groups.get(id).push({ pathIndex, end });
  };
  bakedPaths.forEach((bp, pathIndex) => {
    if (bp.closed || !bp.controlPoints.length) return;
    const first = bp.controlPoints[0], last = bp.controlPoints[bp.controlPoints.length - 1];
    if (first) add(first.id, pathIndex, 'start');
    if (last) add(last.id, pathIndex, 'end');
  });
  return groups;
}
function endpointIncidentCounts(bakedPaths) {
  const counts = new Map();
  for (const [id, list] of sharedEndpointGroups(bakedPaths)) counts.set(id, list.length);
  return counts;
}
function inferBranchPointIds(trackPaths, junctions) {
  const ids = new Set((junctions || []).map(j => j.pointId).filter(Boolean));
  const stats = new Map();
  const stat = id => {
    if (!stats.has(id)) stats.set(id, { endpoints: 0, interior: 0, closed: 0 });
    return stats.get(id);
  };
  for (const path of trackPaths || []) {
    const cps = TC().splitPoints(path.points || []).controlPoints;
    const closed = path.closed !== false;
    for (let i = 0; i < cps.length; i++) {
      const p = cps[i];
      if (!p || !p.id) continue;
      const s = stat(p.id);
      if (closed) s.closed++;
      else if (i === 0 || i === cps.length - 1) s.endpoints++;
      else s.interior++;
    }
  }
  for (const [id, s] of stats) {
    if (s.endpoints >= 3) ids.add(id);
    else if (s.endpoints >= 1 && (s.closed > 0 || s.interior > 0)) ids.add(id);
  }
  return ids;
}
function computeDisjointEndpointNormals(bakedPaths, disjointSeams) {
  const out = bakedPaths.map(() => ({}));
  const norm = v => { const l = Math.hypot(v.x, v.y, v.z) || 1; return { x: v.x / l, y: v.y / l, z: v.z / l }; };
  for (const seam of disjointSeams || []) {
    const incs = [];
    bakedPaths.forEach((bp, pathIndex) => {
      if (bp.closed || !bp.controlPoints.length || !bp.frames.length) return;
      const lastCp = bp.controlPoints.length - 1;
      if (bp.controlPoints[0] && bp.controlPoints[0].id === seam.pointId) {
        incs.push({ pathIndex, end: 'start', normal: bp.frames[0].normal });
      }
      if (bp.controlPoints[lastCp] && bp.controlPoints[lastCp].id === seam.pointId) {
        incs.push({ pathIndex, end: 'end', normal: bp.frames[bp.frames.length - 1].normal });
      }
    });
    if (incs.length < 2) continue;
    const avg = norm(incs.reduce((s, inc) => ({
      x: s.x + inc.normal.x, y: s.y + inc.normal.y, z: s.z + inc.normal.z
    }), { x: 0, y: 0, z: 0 }));
    for (const inc of incs) out[inc.pathIndex][inc.end] = avg;
  }
  return out;
}

// The physics-relevant portion of track-game.js buildPath(): apply any disjoint
// seam overrides, trim edge self-intersections, then wrap the frames into Vec3.
function bakePhysicsPath(controlPoints, closed, prebuiltRaw, prebuiltEdges, endpointCuts, endpointNormals, deciders, skipSelfIntersectionCleanup) {
  const TrackCore = TC();
  const raw = prebuiltRaw;
  let edges = prebuiltEdges;

  if (endpointNormals) {
    if (endpointNormals.start) raw[0].normal = endpointNormals.start;
    if (endpointNormals.end) raw[raw.length - 1].normal = endpointNormals.end;
  }
  if (endpointCuts) {
    const applyCut = (end, i) => {
      if (!endpointCuts[end]) return;
      if (endpointCuts[end].left) edges.left[i] = endpointCuts[end].left;
      if (endpointCuts[end].right) edges.right[i] = endpointCuts[end].right;
    };
    applyCut('start', 0);
    applyCut('end', raw.length - 1);
  }

  const wrapsAtDisjointSeam = !closed && !!endpointCuts && !!endpointCuts.start && !!endpointCuts.end &&
    controlPoints[0] && controlPoints[controlPoints.length - 1] && controlPoints[0].id === controlPoints[controlPoints.length - 1].id;
  if (!skipSelfIntersectionCleanup) {
    edges = TrackCore.removeLocalEdgeSelfIntersections(
      edges, closed, wrapsAtDisjointSeam,
      deciders && deciders.decideLeft, deciders && deciders.decideRight, deciders && deciders.scanSpan
    );
  }

  const wallOffsets = TrackCore.computePhysicalWallOffsets(raw, edges);
  const centerline = raw.map((f, i) => ({
    pos: toVec(f.pos), tangent: toVec(f.tangent), h: toVec(f.h),
    edgeRight: toVec(f.edgeRight), normal: toVec(f.normal),
    roll: f.roll, width: f.width, halfW: f.halfW,
    crossSectionCurvature: f.crossSectionCurvature, crossSectionTightness: f.crossSectionTightness,
    sLeft: wallOffsets[i].sLeft, sRight: wallOffsets[i].sRight
  }));
  const anchors = controlPoints.map(c => new Vec3(c.pos[0], c.pos[1], c.pos[2]));
  const endpointIds = {
    start: controlPoints[0] && controlPoints[0].id,
    end: controlPoints[controlPoints.length - 1] && controlPoints[controlPoints.length - 1].id
  };
  return { closed, centerline, anchors, endpointIds, _renderRaw: raw, _renderEdges: edges };
}

// Compile every valid mesh placement once. Invalid assets are recoverable, as in
// track-game.js: the rest of the track remains usable and callers receive a
// structured warning suitable for tests or a future native loading UI.
function bakeMeshRegions(track, warnings) {
  const regions = [];
  const assets = track.meshAssets || {};
  const cache = new Map();
  for (const placement of track.meshes || []) {
    const asset = assets[placement.asset];
    if (!asset) continue;
    try {
      let mesh = cache.get(placement.asset);
      if (!mesh) { mesh = TrackMesh.meshFromJSON(asset.mesh); cache.set(placement.asset, mesh); }
      const compiled = TrackMesh.compile(mesh, placement);
      const railHeight = asset.railHeight == null ? TC().DEFAULT_RAIL_HEIGHT : asset.railHeight;
      regions.push({ compiled, elevation: compiled.elevation, railHeight });
    } catch (error) {
      warnings.push({ code: 'mesh-load-failed', objectId: placement.id, message: String(error && error.message || error) });
    }
  }
  return regions;
}

// Bake zones into the records detectZoneTriggers reads. Path records retain the
// sampled strip rows for graphics-agnostic rendering; mesh records retain their
// host-region identity and flat rectangle transform.
function bakeZones(track, bakedPaths, paths, meshRegions) {
  const TrackCore = TC();
  const out = [];
  for (const zone of track.zones || []) {
    const host = zone.host || {};
    if (host.kind === 'mesh') {
      const hostRegionIndex = meshRegions.findIndex(r => r.compiled && r.compiled.id === host.meshId);
      if (hostRegionIndex < 0) continue;
      const hl = Math.max(0.25, (zone.length || 0) / 2), hw = Math.max(0.25, (zone.width || 0) / 2);
      out.push({
        id: zone.id, kind: 'mesh', effect: zone.effect, factor: zone.factor, duration: zone.duration,
        hostRegion: meshRegions[hostRegionIndex], hostRegionIndex,
        x: host.x, z: host.z, rot: (host.rotation || 0) * Math.PI / 180,
        halfLen: hl, halfWidth: hw
      });
      continue;
    }
    const idx = bakedPaths.findIndex(bp => bp.id === host.pathId);
    if (idx < 0) continue;
    const bp = bakedPaths[idx];
    const strip = TrackCore.zonePathStrip(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, zone, ZONE_HOVER);
    out.push({
      id: zone.id, kind: 'path', effect: zone.effect, factor: zone.factor, duration: zone.duration,
      hostPath: paths[idx], hostPathIndex: idx,
      gLo: strip.gLo, gHi: strip.gHi, gMax: strip.gMax, closed: strip.closed,
      lateral: host.lateral || 0, halfWidth: Math.max(0.25, (zone.width || 0) / 2),
      renderRows: strip.rows
    });
  }
  return out;
}

// Bake path- and mesh-hosted trigger gates into one generic world-space record;
// Simulation.detectTriggers needs no host-specific branch after this point.
function bakeTriggers(track, bakedPaths, meshRegions) {
  const TrackCore = TC();
  const out = [];
  for (const trig of track.triggers || []) {
    const host = trig.host || {};
    let frame;
    if (host.kind === 'mesh') {
      const region = meshRegions.find(r => r.compiled && r.compiled.id === host.meshId);
      if (!region) continue;
      const rot = (trig.rotation || 0) * Math.PI / 180, cos = Math.cos(rot), sin = Math.sin(rot);
      frame = {
        center: { x: host.x, y: region.elevation, z: host.z },
        fwd: { x: sin, y: 0, z: cos }, right: { x: cos, y: 0, z: -sin }, up: { x: 0, y: 1, z: 0 }
      };
    } else {
      const bp = bakedPaths.find(b => b.id === host.pathId);
      if (!bp) continue;
      frame = TrackCore.triggerPathFrame(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, trig);
    }
    out.push({
      id: trig.id, type: trig.type, role: trig.role, direction: trig.direction,
      center: toVec(frame.center), right: toVec(frame.right), up: toVec(frame.up), fwd: toVec(frame.fwd),
      halfWidth: Math.max(0.25, (trig.width || 0) / 2), height: Math.max(0.25, trig.height || 0)
    });
  }
  return out;
}

// Bake a normalized track (as produced by TrackCore.parseTrack) into physics
// data. Mirrors buildTrack()'s per-path pipeline exactly.
export function bakeTrackPhysics(track) {
  const TrackCore = TC();
  const trackPaths = track.paths || [];
  const branchPointIds = inferBranchPointIds(trackPaths, track.junctions || []);
  const connectedEndpointIds = new Set((track.disjointSeams || []).concat(track.junctions || []).map(j => j.pointId));

  const bakedPaths = trackPaths.map(p => {
    const { controlPoints, rollPoints, widthPoints, crossSectionPoints } = TrackCore.splitPoints(p.points);
    const closed = p.closed !== false;
    const pathN = TrackCore.adaptiveSampleCount(controlPoints, closed, rollPoints, widthPoints, crossSectionPoints);
    const frames = TrackCore.buildCenterline(controlPoints, pathN, closed, rollPoints, widthPoints, crossSectionPoints);
    const edges = TrackCore.buildEdges(frames, closed);
    const hasBranchConnection = controlPoints.some(cp => cp && branchPointIds.has(cp.id));
    return { id: p.id, closed, controlPoints, rollPoints, widthPoints, crossSectionPoints, frames, edges, hasBranchConnection, pathN, texture: p.texture || null };
  });

  const incidentCounts = endpointIncidentCounts(bakedPaths);
  for (const [id, count] of incidentCounts) if (count >= 2) connectedEndpointIds.add(id);
  const disjointSeams = track.disjointSeams || [];
  const overrides = track.selfIntersectionOverrides || [];
  const edgeCuts = TrackCore.computeDisjointEdgeCuts(bakedPaths, disjointSeams);
  const endpointNormals = computeDisjointEndpointNormals(bakedPaths, disjointSeams);

  const paths = bakedPaths.map((p, i) => {
    const path = bakePhysicsPath(
      p.controlPoints, p.closed, p.frames, p.edges, edgeCuts[i], endpointNormals[i],
      TrackCore.makeSelfIntersectionDeciders(p.controlPoints, p.closed, p.pathN, overrides), p.hasBranchConnection
    );
    path._renderDefinition = p;
    return path;
  });

  const warnings = [];
  const meshRegions = bakeMeshRegions(track, warnings);
  let lowest = Infinity;
  for (const p of paths) for (const f of p.centerline) lowest = Math.min(lowest, f.pos.y);
  for (const region of meshRegions) lowest = Math.min(lowest, region.elevation);
  const trackFloorY = (isFinite(lowest) ? lowest : 0) - RESPAWN_FALL_DEPTH;

  const zones = bakeZones(track, bakedPaths, paths, meshRegions);
  const triggers = bakeTriggers(track, bakedPaths, meshRegions);

  return { paths, meshRegions, connectedEndpointIds, trackFloorY, zones, triggers, warnings, bakedPaths };
}

// Convenience: the start pose (surface position + orientation) at a path's
// chosen control point, mirroring the settling loop in startingGridPoses (minus
// the multi-ship lateral grid offset). Used to place a headless ship on-track.
export function startPose(sim, track, startSpec) {
  const spec = startSpec || track.start || { path: 0, point: 0, reverse: false };
  const path = sim.paths[Math.max(0, Math.min(spec.path || 0, sim.paths.length - 1))];
  const pointIndex = Math.max(0, Math.min(spec.point || 0, path.anchors.length - 1));
  const anchor = path.anchors[pointIndex];
  let startIndex = 0, bestD = Infinity;
  for (let i = 0; i < path.centerline.length; i++) {
    const d = path.centerline[i].pos.distanceToSquared(anchor);
    if (d < bestD) { bestD = d; startIndex = i; }
  }
  const frame = path.centerline[startIndex];
  return { frame, reverse: !!spec.reverse };
}
