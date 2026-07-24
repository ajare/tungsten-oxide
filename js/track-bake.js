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
 * Mesh regions are intentionally NOT baked — they are out of the C++ port's
 * scope and parity tracks emit none. `trackFloorY` therefore considers only the
 * spline surfaces (the game also lowers it for mesh-region elevations).
 */

import { Vec3 } from './vec3.js';
import { RESPAWN_FALL_DEPTH } from './track-physics.js';

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
  return { closed, centerline, anchors, endpointIds };
}

// Bake the path-hosted zones into the compiled records detectZoneTriggers reads,
// mirroring track-game.js buildZones() (the path branch). Reuses the shared
// TrackCore.zonePathStrip for the g-window, so game and headless never drift.
// Mesh-hosted zones are skipped: mesh regions are out of the C++ port's scope and
// the parity corpus emits none. `hostPathIndex` is carried so the serialized
// world can rehydrate the `hostPath` object identity detectZoneTriggers compares.
function bakeZones(track, bakedPaths, paths) {
  const TrackCore = TC();
  const out = [];
  for (const zone of track.zones || []) {
    const host = zone.host || {};
    if (host.kind === 'mesh') continue;
    const idx = bakedPaths.findIndex(bp => bp.id === host.pathId);
    if (idx < 0) continue;
    const bp = bakedPaths[idx];
    const strip = TrackCore.zonePathStrip(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, zone, ZONE_HOVER);
    out.push({
      id: zone.id, kind: 'path', effect: zone.effect, factor: zone.factor, duration: zone.duration,
      hostPath: paths[idx], hostPathIndex: idx,
      gLo: strip.gLo, gHi: strip.gHi, gMax: strip.gMax, closed: strip.closed,
      lateral: host.lateral || 0, halfWidth: Math.max(0.25, (zone.width || 0) / 2)
    });
  }
  return out;
}

// Bake the path-hosted triggers into the compiled gate records detectTriggers
// reads, mirroring track-game.js buildTriggers() (the path branch) via the shared
// TrackCore.triggerPathFrame. Mesh-hosted triggers are skipped (see bakeZones).
function bakeTriggers(track, bakedPaths) {
  const TrackCore = TC();
  const out = [];
  for (const trig of track.triggers || []) {
    const host = trig.host || {};
    if (host.kind === 'mesh') continue;
    const bp = bakedPaths.find(b => b.id === host.pathId);
    if (!bp) continue;
    const frame = TrackCore.triggerPathFrame(bp.controlPoints, bp.closed, bp.rollPoints, bp.widthPoints, bp.crossSectionPoints, trig);
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
    return { id: p.id, closed, controlPoints, rollPoints, widthPoints, crossSectionPoints, frames, edges, hasBranchConnection, pathN };
  });

  const incidentCounts = endpointIncidentCounts(bakedPaths);
  for (const [id, count] of incidentCounts) if (count >= 2) connectedEndpointIds.add(id);
  const disjointSeams = track.disjointSeams || [];
  const overrides = track.selfIntersectionOverrides || [];
  const edgeCuts = TrackCore.computeDisjointEdgeCuts(bakedPaths, disjointSeams);
  const endpointNormals = computeDisjointEndpointNormals(bakedPaths, disjointSeams);

  const paths = bakedPaths.map((p, i) => bakePhysicsPath(
    p.controlPoints, p.closed, p.frames, p.edges, edgeCuts[i], endpointNormals[i],
    TrackCore.makeSelfIntersectionDeciders(p.controlPoints, p.closed, p.pathN, overrides), p.hasBranchConnection
  ));

  let lowest = Infinity;
  for (const p of paths) for (const f of p.centerline) lowest = Math.min(lowest, f.pos.y);
  const trackFloorY = (isFinite(lowest) ? lowest : 0) - RESPAWN_FALL_DEPTH;

  const zones = bakeZones(track, bakedPaths, paths);
  const triggers = bakeTriggers(track, bakedPaths);

  return { paths, connectedEndpointIds, trackFloorY, zones, triggers };
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
