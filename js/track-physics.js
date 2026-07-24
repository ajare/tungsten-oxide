/* track-physics.js — the THREE-free physics core, extracted verbatim out of
 * js/track-game.js so it can run headless (Node) and serve as the single
 * reference oracle for the C++ port (see CPP_PORT_PLAN.md, milestone 0).
 *
 * This is a *literal transliteration* of the physics that used to live inline
 * in track-game.js: every `THREE.Vector3` became a `Vec3` (js/vec3.js, which
 * mirrors THREE r128's op order + edge cases exactly), and `THREE.MathUtils`
 * .clamp/.lerp became the local `clamp`/`lerp` below (same formulas r128 uses).
 * Nothing else about the math changed — the shipping game must drive identically
 * across this refactor, which the browser smoke test guards.
 *
 * Split of responsibilities:
 *   - PURE functions + constants are exported directly (no world state): the
 *     spline/kinematics helpers a C++ transliteration mirrors 1:1.
 *   - The STATEFUL step lives on a `Simulation` that owns the baked track data
 *     (paths, meshRegions, zones, triggers, ...) and the ship roster. Game-only
 *     side effects (console logging, the checkpoint-flash, wall-clock) are
 *     injected as hooks so this module stays deterministic and portable.
 *
 * Dependencies: `Vec3` (imported) and the shared `TrackCore` math (read from the
 * global, exactly as track-game.js does — track-core.js is a classic script that
 * sets window.TrackCore before any module runs; Node tests set globalThis
 * .TrackCore before importing this file). Mesh-region collision is delegated to
 * an injected `TrackMesh` (geometry-js); it is OUT of scope for the C++ port and
 * only exercised when a track actually carries mesh regions.
 */

import { Vec3 } from './vec3.js';

// track-core.js publishes window.TrackCore (a classic browser script) before any
// ES module evaluates; Node harnesses assign globalThis.TrackCore the same way.
// Referenced lazily (inside functions), never captured at import time, so the
// binding is whatever is current when physics actually runs.
const TC = () => /** @type {any} */ (globalThis).TrackCore;

// --- centralized physics constants (single source of truth) ------------------
// Scattered across track-core.js and track-game.js before the port; gathered
// here so the C++ header transliterates from one place and a drifted constant
// can't become an invisible parity bug (see CPP_PORT_PLAN.md §8). The geometry
// constants TrackCore owns (COLLISION_WALL_MARGIN, DEFAULT_RAIL_HEIGHT, boost
// defaults) still live in TrackCore, which uses them itself; read from there.
export const UP = Object.freeze(new Vec3(0, 1, 0));
export const ZONE_RELEASE = 1;             // seconds for the boost's smooth release back to max
export const CHECKPOINT_FLASH_MS = 500;
export const TRIGGER_REARM_MARGIN = 3;     // units past the plane that counts as "clear"
export const SURFACE_SNAP_UP = 3;          // a surface farther above the ship is overhead geometry
export const RESPAWN_FALL_DEPTH = 100;     // how far below the lowest surface counts as fallen off
export const CORRIDOR_ALONG_TOL = 8;       // along-tangent slack for corridor containment
export const SEGMENT_ALONG_TOL = 0.5;      // along-segment slack for nearest-segment membership
export const MAX_PHYSICS_STEP = 1 / 120;   // largest integration sub-step (substepping is the caller's job)
export const HANDLING_BASE_WEIGHT = 1000;  // kg; the weight the collision reaction is tuned around

// THREE.MathUtils.clamp / lerp, r128 formulas verbatim.
export const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
export const lerp = (x, y, t) => (1 - t) * x + t * y;

// --- scratch vectors (module-level, reused every step — allocates nothing) ---
const _vel = new Vec3();
const _newPos = new Vec3();
const _wallN = new Vec3();
const _launchVel = new Vec3();
const _meshSurfacePos = new Vec3();
const _tanTmp = new Vec3();
const _saCross = new Vec3();

// ---------------------------------------------------------------------------
// Pure helpers (no world state)
// ---------------------------------------------------------------------------

// The ship's evaluator parameter g on the path a sample landed on, recovered
// from the segment (a->b, segT) sampleTrack recorded.
export function shipParamG(sample) {
  const p = sample.pathObj;
  if (!p) return 0;
  const M = p.centerline.length, CP_N = p.anchors.length;
  const gAt = i => p.closed ? (i / M) * CP_N : (M > 1 ? (i / (M - 1)) * (CP_N - 1) : 0);
  const ga = gAt(sample.a);
  let gb = gAt(sample.b);
  if (p.closed && sample.b < sample.a) gb += CP_N;   // the wrap segment M-1 -> 0
  return ga + (gb - ga) * sample.segT;
}

// Effective speed cap this frame: the raised boost cap while a boost is running,
// otherwise the normal per-track max.
export function effectiveMaxSpeed(physics) {
  return physics.boostActive ? Math.max(physics.maxSpeed, physics.boostEffCap) : physics.maxSpeed;
}
export function clearBoost(ship) {
  const physics = ship.physics;
  physics.boostActive = false; physics.boostReleasing = false;
  physics.boostHold = 0; physics.boostReleaseT = 0; physics.boostCap = 0; physics.boostEffCap = 0;
  ship.zoneInside.clear();
}
// Start a boost for one ship. Each ship owns its lock and cap state.
export function triggerBoost(ship, zone) {
  const physics = ship.physics;
  if (physics.boostActive) return;
  physics.boostActive = true;
  physics.boostReleasing = false;
  physics.boostHold = zone.duration || TC().DEFAULT_BOOST_DURATION;
  physics.boostReleaseT = ZONE_RELEASE;
  physics.boostCap = (zone.factor || TC().DEFAULT_BOOST_FACTOR) * physics.maxSpeed;
  physics.boostEffCap = physics.boostCap;
  if (physics.speed > 0) physics.speed = Math.max(physics.speed, physics.boostCap);
}
export function tickBoost(ship, dt) {
  const physics = ship.physics;
  if (!physics.boostActive) return;
  if (!physics.boostReleasing) {
    physics.boostHold -= dt;
    physics.boostEffCap = physics.boostCap;
    if (physics.boostHold <= 0) { physics.boostReleasing = true; physics.boostReleaseT = ZONE_RELEASE; }
  } else {
    physics.boostReleaseT -= dt;
    const frac = Math.max(0, Math.min(1, physics.boostReleaseT / ZONE_RELEASE));
    physics.boostEffCap = physics.maxSpeed + (physics.boostCap - physics.maxSpeed) * frac;
    if (physics.boostReleaseT <= 0) { physics.boostActive = false; physics.boostEffCap = physics.maxSpeed; }
  }
}

// Project a FULL 3D position onto a corridor sample's cross-section, returning
// the lateral offset `s` and the drivable range bounded by the rendered
// physical walls (the trimmed edge offsets, inset by the ship margin).
export function projectToSurface(sample, px, py, pz) {
  const er = sample.edgeRight;
  const s = (px - sample.pos.x) * er.x + (py - sample.pos.y) * er.y + (pz - sample.pos.z) * er.z;
  let loS = sample.sLeft + TC().COLLISION_WALL_MARGIN;    // inner limit of the left edge (negative side)
  let hiS = sample.sRight - TC().COLLISION_WALL_MARGIN;   // inner limit of the right edge (positive side)
  if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; } // corridor pinched to a point
  return { er, s, loS, hiS };
}

// Is X/Z genuinely over a corridor sample's drivable surface?
export function corridorContains(sample, x, y, z, proj) {
  if (sample.offEnd || proj.s < proj.loS || proj.s > proj.hiS) return false;
  const along = (x - sample.pos.x) * sample.tangent.x + (y - sample.pos.y) * sample.tangent.y + (z - sample.pos.z) * sample.tangent.z;
  return Math.abs(along) <= CORRIDOR_ALONG_TOL;
}

export function curvedSurfaceHeight(sample, s) {
  const lo = sample.sLeft, hi = sample.sRight;
  const span = hi - lo;
  if (Math.abs(span) < 1e-6) return 0;
  const v = (s - lo) / span;
  return TC().crossSectionHeight(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
}
export function curvedSurfaceFrame(sample, s) {
  const lo = sample.sLeft, hi = sample.sRight;
  const span = hi - lo;
  const v = Math.abs(span) < 1e-6 ? 0.5 : (s - lo) / span;
  const lift = TC().crossSectionHeight(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
  const pos = sample.pos.clone()
    .addScaledVector(sample.edgeRight, s)
    .addScaledVector(sample.normal, lift);
  const dhdv = TC().crossSectionHeightDerivative(sample.crossSectionCurvature, sample.crossSectionTightness, v, Math.abs(span));
  const crossT = sample.edgeRight.clone().multiplyScalar(span).addScaledVector(sample.normal, dhdv);
  const normal = new Vec3().crossVectors(sample.tangent, crossT).normalize();
  if (normal.dot(sample.normal) < 0) normal.negate();
  return { pos, normal };
}

// ---------- Per-track ship handling ----------
export function applyHandling(track, physics) {
  const h = TC().normalizeHandling(track && track.handling);
  physics.maxSpeed = h.maxSpeed;
  physics.accel = h.accel;
  physics.turnRate = h.turnSpeed * Math.PI / 180;
  physics.weight = h.weight;
}
export function weightRestitution(physics) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  return Math.max(0, Math.min(0.9, physics.wallRestitution / m));
}
export function weightSpeedRetain(physics) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  return Math.max(0.85, Math.min(0.999, 1 - 0.02 / m));
}
export function addImpactJolt(physics, normalImpactSpeed) {
  const m = (physics.weight || HANDLING_BASE_WEIGHT) / HANDLING_BASE_WEIGHT;
  const momentum = m * Math.max(0, normalImpactSpeed);
  physics.landingBounce += Math.min(2.0, momentum * 0.012);
  physics.landingBounceVel += Math.min(10, momentum * 0.05);
}

// ---------- Wipeout-style hover physics: state factory ----------
export function createPhysicsState() { return {
  heading: 0,
  speed: 0,
  maxSpeed: 140,
  maxReverse: -33,
  accel: 71,
  brakeDecel: 115,
  friction: 55,
  turnRate: 2.4,
  grip: 3.2,
  wallRestitution: 0.75,
  weight: 1000,
  bobTime: 0,
  visualBank: 0,
  visualPitch: 0,
  up: new Vec3(0, 1, 0),
  forward: new Vec3(0, 0, 1),
  right: new Vec3(1, 0, 0),
  groundPos: new Vec3(),
  visualGroundPos: new Vec3(),
  visualUp: new Vec3(0, 1, 0),
  moveDir: new Vec3(0, 0, 1),
  airborne: false,
  verticalVel: 0,
  gravity: 60,
  landingBounce: 0,
  landingBounceVel: 0,
  boostActive: false,
  boostReleasing: false,
  boostHold: 0,
  boostReleaseT: 0,
  boostCap: 0,
  boostEffCap: 0
}; }

// --- surface-relative motion helpers ---------------------------------------
export function tangentize(v, n, fallback) {
  _tanTmp.copy(v).addScaledVector(n, -v.dot(n));
  if (_tanTmp.lengthSq() < 1e-9) { return v.copy(fallback); }
  return v.copy(_tanTmp).normalize();
}
export function signedAngleAbout(a, b, axis) {
  const d = clamp(a.dot(b), -1, 1);
  const ang = Math.acos(d);
  _saCross.crossVectors(a, b);
  return _saCross.dot(axis) < 0 ? -ang : ang;
}
export function beginAirborne(ship, vel3D) {
  const physics = ship.physics;
  physics.airborne = true;
  physics.verticalVel = vel3D.y;
  const horiz = Math.hypot(vel3D.x, vel3D.z);
  physics.speed = horiz;
  if (horiz > 1e-6) physics.moveDir.set(vel3D.x / horiz, 0, vel3D.z / horiz);
  else tangentize(physics.moveDir, UP, physics.forward);   // launched straight up: keep a horizontal azimuth
}
export function landOnSurface(ship, normal) {
  const physics = ship.physics;
  physics.airborne = false;
  physics.verticalVel = 0;
  tangentize(physics.moveDir, normal, physics.forward);
  tangentize(physics.forward, normal, physics.moveDir);
}

// ---------- Race / checkpoint state ----------
export function createRaceState(track, now) {
  const checkpoints = (track.triggers || []).filter(tr => tr.type === 'checkpoint');
  return {
    laps: 0, hit: new Set(),
    intermediateIds: checkpoints.filter(tr => tr.role !== 'finish').map(tr => tr.id),
    finishId: (checkpoints.find(tr => tr.role === 'finish') || {}).id || null,
    totalStartedAt: now, lapStartedAt: now, flashUntil: 0
  };
}

// The physics-relevant per-ship sub-state (the game merges group/controller/color
// on top of this). Kept here so the parity harness can build ships without THREE.
export function createShipState(track, now) {
  return {
    physics: createPhysicsState(),
    zoneInside: new Map(),
    triggerStates: new Map(),
    prevTriggerPos: new Vec3(),
    race: createRaceState(track, now),
    lastCheckpoint: { valid: false, triggerId: null, pos: new Vec3(), forward: new Vec3(), up: new Vec3(0, 1, 0) },
    startPose: null
  };
}

// ---------------------------------------------------------------------------
// Simulation — owns the baked track + ship roster, runs the step
// ---------------------------------------------------------------------------
export class Simulation {
  constructor(opts = {}) {
    // Baked, world-space track data the physics reads. Populated by the host
    // (track-game.js buildTrack, or the parity trace generator).
    this.paths = [];
    this.meshRegions = [];
    this.zones = [];
    this.triggers = [];
    this.connectedEndpointIds = new Set();
    this.trackFloorY = -1e9;

    // Injected collaborators / hooks.
    this.TrackMesh = opts.TrackMesh || null;                 // geometry-js mesh collision (optional; mesh is out of C++ scope)
    this.now = opts.now || (() => (typeof performance !== 'undefined' ? performance.now() : Date.now()));
    // Called when a trigger fires, for game-only effects (console log, player
    // checkpoint flash). The portable checkpoint/lap logic runs regardless.
    this.onTriggerFired = opts.onTriggerFired || (() => {});

    // sampleTrack's shared result, reused each call (no per-frame allocation).
    this._sample = {
      pos: new Vec3(), tangent: new Vec3(), edgeRight: new Vec3(), normal: new Vec3(),
      halfW: 0, sLeft: 0, sRight: 0, crossSectionCurvature: 0, crossSectionTightness: 1,
      offEnd: false, pathObj: null, a: 0, b: 1, segT: 0
    };
  }

  // Sample a smooth, interpolated track frame at a 3D position. Searches all
  // path segments for the nearest projected point, then interpolates within it.
  sampleTrack(x, y, z) {
    const paths = this.paths;
    const connectedEndpointIds = this.connectedEndpointIds;
    const _sample = this._sample;
    let fallback = { path: paths[0], a: 0, b: 1, t: 0, d: Infinity };
    let bestUnder = null;
    for (const path of paths) {
      const cl = path.centerline, M = cl.length;
      const segCount = path.closed ? M : M - 1;
      for (let i = 0; i < segCount; i++) {
        const j = path.closed ? (i + 1) % M : i + 1;
        const a = cl[i], b = cl[j];
        const sx = b.pos.x - a.pos.x, sy = b.pos.y - a.pos.y, sz = b.pos.z - a.pos.z;
        const segLen2 = sx * sx + sy * sy + sz * sz;
        const t = segLen2 > 0
          ? clamp(((x - a.pos.x) * sx + (y - a.pos.y) * sy + (z - a.pos.z) * sz) / segLen2, 0, 1)
          : 0;
        const px = a.pos.x + sx * t, py = a.pos.y + sy * t, pz = a.pos.z + sz * t;
        const dx = x - px, dy = y - py, dz = z - pz;
        const d = dx * dx + dy * dy + dz * dz;
        if (d < fallback.d) fallback = { path, a: i, b: j, t, d };

        let erx = a.edgeRight.x + (b.edgeRight.x - a.edgeRight.x) * t;
        let ery = a.edgeRight.y + (b.edgeRight.y - a.edgeRight.y) * t;
        let erz = a.edgeRight.z + (b.edgeRight.z - a.edgeRight.z) * t;
        const erl = Math.hypot(erx, ery, erz) || 1;
        erx /= erl; ery /= erl; erz /= erl;
        const cx = a.pos.x + (b.pos.x - a.pos.x) * t;
        const cy = a.pos.y + (b.pos.y - a.pos.y) * t;
        const cz = a.pos.z + (b.pos.z - a.pos.z) * t;
        const lateral = (x - cx) * erx + (y - cy) * ery + (z - cz) * erz;
        let loS = (a.sLeft + (b.sLeft - a.sLeft) * t) + TC().COLLISION_WALL_MARGIN;
        let hiS = (a.sRight + (b.sRight - a.sRight) * t) - TC().COLLISION_WALL_MARGIN;
        if (loS > hiS) { const m = (loS + hiS) / 2; loS = m; hiS = m; }
        let wouldOffEnd = false;
        if (!path.closed) {
          if (i === 0 && t <= 1e-4) {
            const e = cl[0];
            wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.start) &&
              ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
          } else if (j === M - 1 && t >= 1 - 1e-4) {
            const e = cl[M - 1];
            wouldOffEnd = !connectedEndpointIds.has(path.endpointIds.end) &&
              ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
          }
        }
        const alongSeg = segLen2 > 0 ? ((x - px) * sx + (y - py) * sy + (z - pz) * sz) / Math.sqrt(segLen2) : 0;
        const overSegment = Math.abs(alongSeg) <= SEGMENT_ALONG_TOL;
        if (overSegment && !wouldOffEnd && lateral >= loS && lateral <= hiS && (!bestUnder || d < bestUnder.d)) bestUnder = { path, a: i, b: j, t, d };
      }
    }
    const best = bestUnder || fallback;
    const bestPath = best.path, bestA = best.a, bestB = best.b;
    const cl = bestPath.centerline;
    const a = cl[bestA], b = cl[bestB];
    const t = best.t;
    _sample.pathObj = bestPath; _sample.a = bestA; _sample.b = bestB; _sample.segT = t;

    _sample.pos.copy(a.pos).lerp(b.pos, t);
    _sample.tangent.copy(a.tangent).lerp(b.tangent, t).normalize();
    _sample.edgeRight.copy(a.edgeRight).lerp(b.edgeRight, t).normalize();
    _sample.normal.copy(a.normal).lerp(b.normal, t).normalize();
    _sample.halfW = a.halfW + (b.halfW - a.halfW) * t;
    _sample.crossSectionCurvature = a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * t;
    _sample.crossSectionTightness = a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * t;
    _sample.sLeft = a.sLeft + (b.sLeft - a.sLeft) * t;
    _sample.sRight = a.sRight + (b.sRight - a.sRight) * t;
    _sample.offEnd = false;
    if (!bestPath.closed) {
      const M = bestPath.centerline.length;
      if (bestA === 0 && t <= 1e-4) {
        const e = bestPath.centerline[0];
        _sample.offEnd = !connectedEndpointIds.has(bestPath.endpointIds.start) &&
          ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
      } else if (bestB === M - 1 && t >= 1 - 1e-4) {
        const e = bestPath.centerline[M - 1];
        _sample.offEnd = !connectedEndpointIds.has(bestPath.endpointIds.end) &&
          ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
      }
    }
    return _sample;
  }

  // The mesh region under X/Z whose surface sits nearest the ship's current Y,
  // or null. Surfaces above the ship are strongly penalised.
  meshRegionAt(x, z, shipY) {
    const TrackMesh = this.TrackMesh;
    let best = null;
    for (const region of this.meshRegions) {
      if (!TrackMesh.withinBounds(region.compiled, x, z)) continue;
      if (!TrackMesh.containsWorldPoint(region.compiled, x, z)) continue;
      const above = region.elevation - shipY;
      const score = Math.abs(above) + (above > SURFACE_SNAP_UP ? 1e6 : 0);
      if (!best || score < best.score) best = { region, score };
    }
    return best;
  }

  // Decide which surface owns a horizontal position: the mesh region under it, or
  // the spline corridor. Returns the winning region, or null (corridor owns it).
  surfaceOwnerAt(x, z, shipY, corridorSample) {
    const meshHit = this.meshRegionAt(x, z, shipY);
    if (!meshHit) return null;
    const proj = projectToSurface(corridorSample, x, shipY, z);
    if (!corridorContains(corridorSample, x, shipY, z, proj)) return meshHit.region;
    const corridorY = curvedSurfaceFrame(corridorSample, proj.s).pos.y;
    return Math.abs(meshHit.region.elevation - shipY) <= Math.abs(corridorY - shipY) ? meshHit.region : null;
  }

  slideAlongRails(physics, region, from, to, velocity) {
    return this.TrackMesh.slideAlongRails(region.compiled, from, to, velocity, TC().COLLISION_WALL_MARGIN, weightRestitution(physics));
  }

  detectZoneTriggers(ship, sample, meshRegion) {
    const physics = ship.physics;
    for (const z of this.zones) {
      let inside = false;
      if (z.kind === 'path') {
        if (!meshRegion && sample && sample.pathObj === z.hostPath) {
          const proj = projectToSurface(sample, physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
          inside = TC().zoneAlongContains(shipParamG(sample), z.gLo, z.gHi, z.gMax, z.closed) &&
            Math.abs(proj.s - z.lateral) <= z.halfWidth;
        }
      } else if (meshRegion === z.hostRegion) {
        const dx = physics.groundPos.x - z.x, dz = physics.groundPos.z - z.z;
        const cos = Math.cos(z.rot), sin = Math.sin(z.rot);
        const lx = dx * cos + dz * sin, lz = -dx * sin + dz * cos;
        inside = Math.abs(lx) <= z.halfLen && Math.abs(lz) <= z.halfWidth;
      }
      const wasInside = ship.zoneInside.get(z.id) || false;
      if (inside && !wasInside && z.effect === 'velocityChange') triggerBoost(ship, z);
      ship.zoneInside.set(z.id, inside);
    }
  }

  // Swept crossing of the ship segment p0->p1 against every trigger gate.
  detectTriggers(ship, p0, p1) {
    for (const tr of this.triggers) {
      const state = ship.triggerStates.get(tr.id);
      const c = tr.center;
      const d0 = (p0.x - c.x) * tr.fwd.x + (p0.y - c.y) * tr.fwd.y + (p0.z - c.z) * tr.fwd.z;
      const d1 = (p1.x - c.x) * tr.fwd.x + (p1.y - c.y) * tr.fwd.y + (p1.z - c.z) * tr.fwd.z;
      const rr = (p1.x - c.x) * tr.right.x + (p1.y - c.y) * tr.right.y + (p1.z - c.z) * tr.right.z;
      const uu = (p1.x - c.x) * tr.up.x + (p1.y - c.y) * tr.up.y + (p1.z - c.z) * tr.up.z;
      if (!state.armed && (Math.abs(rr) > tr.halfWidth || uu < 0 || uu > tr.height || Math.abs(d1) > TRIGGER_REARM_MARGIN)) state.armed = true;
      if (state.armed && d0 !== d1 && ((d0 <= 0 && d1 > 0) || (d0 >= 0 && d1 < 0))) {
        const t = d0 / (d0 - d1);
        const xr = (p0.x + (p1.x - p0.x) * t - c.x), yr = (p0.y + (p1.y - p0.y) * t - c.y), zr = (p0.z + (p1.z - p0.z) * t - c.z);
        const lr = xr * tr.right.x + yr * tr.right.y + zr * tr.right.z;
        const lu = xr * tr.up.x + yr * tr.up.y + zr * tr.up.z;
        if (Math.abs(lr) <= tr.halfWidth && lu >= 0 && lu <= tr.height) {
          const dir = d1 > d0 ? 'forward' : 'backward';
          if (tr.direction === 'both' || tr.direction === dir) { this.fireTrigger(ship, tr, dir); state.armed = false; }
        }
      }
    }
  }

  fireTrigger(ship, rec, dir) {
    const state = ship.triggerStates.get(rec.id);
    // Game-only effects (console log, player checkpoint flash) live in the host.
    this.onTriggerFired(ship, rec, dir, state);
    if (rec.type !== 'checkpoint') return;

    const checkpoint = ship.lastCheckpoint;
    checkpoint.valid = true;
    checkpoint.triggerId = rec.id;
    checkpoint.pos.copy(rec.center);
    checkpoint.up.copy(rec.up);
    checkpoint.forward.copy(rec.fwd).multiplyScalar(dir === 'backward' ? -1 : 1);

    const race = ship.race;
    if (rec.role !== 'finish') {
      race.hit.add(rec.id);
      return;
    }
    if (!race.intermediateIds.every(id => race.hit.has(id))) return;

    const now = this.now();
    race.laps++;
    race.hit.clear();
    race.lapStartedAt = now;
    race.flashUntil = now + CHECKPOINT_FLASH_MS;
  }

  resetTriggers(ship, disarmedId = null) {
    ship.prevTriggerPos.copy(ship.physics.groundPos);
    for (const tr of this.triggers) ship.triggerStates.set(tr.id, { armed: tr.id !== disarmedId, flash: 0 });
  }

  placeShipAtPose(ship, pose, disarmedId = null) {
    const physics = ship.physics;
    physics.groundPos.copy(pose.pos);
    physics.visualGroundPos.copy(pose.pos);
    physics.forward.copy(pose.forward);
    physics.moveDir.copy(pose.forward);
    physics.up.copy(pose.up); physics.visualUp.copy(pose.up);
    physics.right.crossVectors(pose.up, pose.forward).normalize();
    physics.heading = Math.atan2(pose.forward.x, pose.forward.z);
    physics.speed = 0; physics.airborne = false; physics.verticalVel = 0;
    physics.visualBank = 0; physics.visualPitch = 0;
    physics.landingBounce = 0; physics.landingBounceVel = 0;
    // Rendered group placement is the host's concern; guard for headless ships.
    if (ship.group) ship.group.position.copy(pose.pos).addScaledVector(pose.up, 1);
    clearBoost(ship);
    this.resetTriggers(ship, disarmedId);
  }

  respawn(ship) {
    if (!ship) return;
    const checkpoint = ship.lastCheckpoint;
    const pose = checkpoint.valid ? checkpoint : ship.startPose;
    this.placeShipAtPose(ship, pose, checkpoint.valid ? checkpoint.triggerId : null);
  }

  // Advance ONE integration sub-step. Returns the surface normal + render
  // position for the host's visual pass, and whether a respawn fired.
  stepPhysics(ship, dt, throttle, brake, steer) {
    const physics = ship.physics;
    const hasTranslation = !!(throttle || brake || Math.abs(physics.speed) > 0.001);

    // Longitudinal speed control
    if (throttle) {
      physics.speed += physics.accel * dt;
    } else if (brake) {
      physics.speed -= physics.brakeDecel * dt;
    } else {
      const decay = physics.friction * dt;
      if (physics.speed > 0) physics.speed = Math.max(0, physics.speed - decay);
      else physics.speed = Math.min(0, physics.speed + decay);
    }
    physics.speed = clamp(physics.speed, physics.maxReverse, effectiveMaxSpeed(physics));

    const speedRatio = Math.min(1, Math.abs(physics.speed) / physics.maxSpeed);

    let c = this.sampleTrack(physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
    let surfaceNormal = c.normal;
    let surfaceRenderPos = physics.groundPos;

    const meshRegion = this.surfaceOwnerAt(physics.groundPos.x, physics.groundPos.z, physics.groundPos.y, c);

    const steerAxis = (physics.airborne || meshRegion) ? UP : surfaceNormal;

    const effectiveTurn = physics.turnRate * (1 - 0.35 * speedRatio) * Math.sign(physics.speed || 1);
    physics.forward.applyAxisAngle(steerAxis, steer * effectiveTurn * dt);
    tangentize(physics.forward, steerAxis, physics.forward);

    const gripThisFrame = physics.grip * (0.5 + 0.5 * (1 - Math.min(Math.abs(steer) * speedRatio, 1)));
    const toForward = signedAngleAbout(physics.moveDir, physics.forward, steerAxis);
    physics.moveDir.applyAxisAngle(steerAxis, toForward * Math.min(gripThisFrame * dt, 1));
    tangentize(physics.moveDir, steerAxis, physics.forward);

    const vel = _vel.copy(physics.moveDir).multiplyScalar(physics.speed);
    const vx = vel.x, vz = vel.z;

    if (physics.airborne) {
      let ax = vx, az = vz;
      let px = physics.groundPos.x + ax * dt;
      let pz = physics.groundPos.z + az * dt;

      for (const region of this.meshRegions) {
        if (physics.groundPos.y >= region.elevation + region.railHeight) continue;
        if (!this.TrackMesh.withinBounds(region.compiled, px, pz, TC().COLLISION_WALL_MARGIN)) continue;
        const velocity = { x: ax, z: az };
        const before = Math.hypot(ax, az);
        const moved = this.slideAlongRails(physics, region, { x: physics.groundPos.x, z: physics.groundPos.z }, { x: px, z: pz }, velocity);
        if (!moved.hit) continue;
        px = moved.x; pz = moved.z; ax = velocity.x; az = velocity.z;
        physics.speed = Math.hypot(ax, az) * weightSpeedRetain(physics);
        addImpactJolt(physics, before - Math.hypot(ax, az));
        if (physics.speed > 1e-6) physics.moveDir.set(ax, 0, az).normalize();
      }

      physics.verticalVel -= physics.gravity * dt;
      physics.groundPos.set(px, physics.groundPos.y + physics.verticalVel * dt, pz);

      const landing = this.meshRegionAt(px, pz, physics.groundPos.y);
      if (landing && physics.groundPos.y <= landing.region.elevation) {
        const impactSpeed = Math.max(0, -physics.verticalVel);
        landOnSurface(ship, UP);
        physics.landingBounce += Math.min(3.2, impactSpeed * 0.09);
        physics.landingBounceVel += Math.min(16, impactSpeed * 0.35);
        physics.groundPos.set(px, landing.region.elevation, pz);
        surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
        surfaceNormal = UP;
      } else {
        c = this.sampleTrack(px, physics.groundPos.y, pz);
        const proj = projectToSurface(c, px, physics.groundPos.y, pz);
        const { s } = proj;
        const surface = curvedSurfaceFrame(c, s);
        if (corridorContains(c, px, physics.groundPos.y, pz, proj) && physics.groundPos.y <= surface.pos.y) {
          const impactSpeed = Math.max(0, -physics.verticalVel);
          landOnSurface(ship, surface.normal);
          physics.landingBounce += Math.min(3.2, impactSpeed * 0.09);
          physics.landingBounceVel += Math.min(16, impactSpeed * 0.35);
          physics.groundPos.copy(surface.pos);
          surfaceRenderPos = surface.pos;
          surfaceNormal = surface.normal;
        }
      }
    } else if (meshRegion && hasTranslation) {
      const from = { x: physics.groundPos.x, z: physics.groundPos.z };
      const velocity = { x: vx, z: vz };
      const moved = this.slideAlongRails(physics, meshRegion, from, { x: from.x + vx * dt, z: from.z + vz * dt }, velocity);
      if (moved.hit) {
        const before = Math.hypot(vx, vz), after = Math.hypot(velocity.x, velocity.z);
        physics.speed = after * weightSpeedRetain(physics);
        if (physics.speed > 1e-6) physics.moveDir.set(velocity.x, 0, velocity.z).normalize();
        addImpactJolt(physics, before - after);
      }

      const stillOn = this.TrackMesh.containsWorldPoint(meshRegion.compiled, moved.x, moved.z)
        ? meshRegion
        : (this.meshRegionAt(moved.x, moved.z, meshRegion.elevation) || {}).region || null;

      if (stillOn) {
        physics.groundPos.set(moved.x, stillOn.elevation, moved.z);
        surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
        surfaceNormal = UP;
      } else {
        c = this.sampleTrack(moved.x, meshRegion.elevation, moved.z);
        const proj = projectToSurface(c, moved.x, meshRegion.elevation, moved.z);
        const { s } = proj;
        const surface = corridorContains(c, moved.x, meshRegion.elevation, moved.z, proj) ? curvedSurfaceFrame(c, s) : null;
        if (surface && Math.abs(surface.pos.y - meshRegion.elevation) <= SURFACE_SNAP_UP) {
          physics.groundPos.copy(surface.pos);
          tangentize(physics.moveDir, surface.normal, physics.forward);
          tangentize(physics.forward, surface.normal, physics.moveDir);
          surfaceRenderPos = surface.pos;
          surfaceNormal = surface.normal;
        } else {
          beginAirborne(ship, _launchVel.copy(physics.moveDir).multiplyScalar(physics.speed));
          physics.groundPos.set(moved.x, meshRegion.elevation, moved.z);
        }
      }
    } else if (hasTranslation) {
      const newPos = _newPos.copy(physics.groundPos).addScaledVector(vel, dt);

      const current = c;
      let projection = projectToSurface(current, newPos.x, newPos.y, newPos.z);
      let forceCurrentWall = !current.offEnd && (projection.s > projection.hiS || projection.s < projection.loS);

      if (!forceCurrentWall) {
        c = this.sampleTrack(newPos.x, newPos.y, newPos.z);
        projection = projectToSurface(c, newPos.x, newPos.y, newPos.z);
      }

      if (!forceCurrentWall && c.offEnd) {
        beginAirborne(ship, vel);
        physics.groundPos.copy(newPos);
      } else {
        const { er, s, loS, hiS } = projection;

        let hitSign = 0;
        if (s > hiS) hitSign = 1; else if (s < loS) hitSign = -1;
        let finalS = s;
        if (hitSign) {
          finalS = clamp(s, loS, hiS);
          _wallN.copy(er).multiplyScalar(hitSign);
          const into = vel.dot(_wallN);
          if (into > 0) {
            vel.addScaledVector(_wallN, -into * (1 + weightRestitution(physics)));
            addImpactJolt(physics, into);
          }
          physics.speed = vel.length() * weightSpeedRetain(physics);
          if (physics.speed > 1e-6) physics.moveDir.copy(vel).normalize();
        }

        const surface = curvedSurfaceFrame(c, finalS);
        physics.groundPos.copy(surface.pos);
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      }
    }

    if (!physics.airborne && !hasTranslation && meshRegion) {
      physics.groundPos.y = meshRegion.elevation;
      surfaceRenderPos = _meshSurfacePos.copy(physics.groundPos);
      surfaceNormal = UP;
    } else if (!physics.airborne && !hasTranslation) {
      c = this.sampleTrack(physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
      const parkedProjection = projectToSurface(c, physics.groundPos.x, physics.groundPos.y, physics.groundPos.z);
      if (!corridorContains(c, physics.groundPos.x, physics.groundPos.y, physics.groundPos.z, parkedProjection)) {
        beginAirborne(ship, _launchVel.set(0, 0, 0));
        surfaceRenderPos = physics.groundPos;
        surfaceNormal = UP;
      } else {
        surfaceRenderPos = physics.groundPos;
        surfaceNormal = physics.up;
      }
    }

    tickBoost(ship, dt);
    if (!physics.airborne) this.detectZoneTriggers(ship, c, meshRegion);

    this.detectTriggers(ship, ship.prevTriggerPos, physics.groundPos);
    ship.prevTriggerPos.copy(physics.groundPos);

    if (physics.airborne && physics.groundPos.y < this.trackFloorY) {
      this.respawn(ship);
      return { respawned: true };
    }
    return { surfaceNormal, surfaceRenderPos, respawned: false };
  }
}
