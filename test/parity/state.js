/* test/parity/state.js — lossless (de)serialization of the pieces a golden trace
 * carries: the baked corridor, and the full per-ship physics + detection state.
 *
 * Doubles go to JSON as JS shortest-round-trip decimals; C++ parses them with
 * correctly-rounded strtod (nlohmann/json) back to the identical double. So a
 * round trip here is bit-exact, and the JS<->JS self-check (test/parity.test.js)
 * proves it before any C++ exists.
 *
 * The trace serializes the BAKED corridor rather than the raw track JSON, so both
 * engines replay against byte-identical centerline frames — baking is removed as
 * a parity variable, and the C++ TrackCore port shrinks to the runtime math the
 * step actually calls (crossSectionHeight/Derivative, zoneAlongContains).
 */

import { Vec3 } from '../../js/vec3.js';
import { createPhysicsState } from '../../js/track-physics.js';

const v3 = v => [v.x, v.y, v.z];

// Only the centerline fields the physics reads at runtime (h/roll/width were
// baking-time only and sampleTrack never touches them).
function serializePath(path) {
  return {
    closed: path.closed,
    endpointIds: path.endpointIds,
    anchors: path.anchors.map(v3),
    centerline: path.centerline.map(f => ({
      pos: v3(f.pos), tangent: v3(f.tangent), edgeRight: v3(f.edgeRight), normal: v3(f.normal),
      halfW: f.halfW, sLeft: f.sLeft, sRight: f.sRight,
      crossSectionCurvature: f.crossSectionCurvature, crossSectionTightness: f.crossSectionTightness
    }))
  };
}
function deserializePath(p) {
  return {
    closed: p.closed,
    endpointIds: p.endpointIds,
    anchors: p.anchors.map(a => new Vec3(a[0], a[1], a[2])),
    centerline: p.centerline.map(f => ({
      pos: new Vec3(...f.pos), tangent: new Vec3(...f.tangent),
      edgeRight: new Vec3(...f.edgeRight), normal: new Vec3(...f.normal),
      halfW: f.halfW, sLeft: f.sLeft, sRight: f.sRight,
      crossSectionCurvature: f.crossSectionCurvature, crossSectionTightness: f.crossSectionTightness
    }))
  };
}

// A compiled path zone. `hostPath` is an object reference (detectZoneTriggers
// compares it by identity against the sampled path), so it is serialized as the
// path INDEX and rehydrated to the live path object on load.
function serializeZone(z) {
  return {
    id: z.id, kind: z.kind, effect: z.effect, factor: z.factor, duration: z.duration,
    hostPathIndex: z.hostPathIndex,
    gLo: z.gLo, gHi: z.gHi, gMax: z.gMax, closed: z.closed,
    lateral: z.lateral, halfWidth: z.halfWidth
  };
}
function deserializeZone(z, paths) {
  return {
    id: z.id, kind: z.kind, effect: z.effect, factor: z.factor, duration: z.duration,
    hostPath: paths[z.hostPathIndex], hostPathIndex: z.hostPathIndex,
    gLo: z.gLo, gHi: z.gHi, gMax: z.gMax, closed: z.closed,
    lateral: z.lateral, halfWidth: z.halfWidth
  };
}
// A compiled trigger gate: baked world-space frame (center/right/up/fwd) + extent.
function serializeTrigger(t) {
  return {
    id: t.id, type: t.type, role: t.role, direction: t.direction,
    center: v3(t.center), right: v3(t.right), up: v3(t.up), fwd: v3(t.fwd),
    halfWidth: t.halfWidth, height: t.height
  };
}
function deserializeTrigger(t) {
  return {
    id: t.id, type: t.type, role: t.role, direction: t.direction,
    center: new Vec3(...t.center), right: new Vec3(...t.right), up: new Vec3(...t.up), fwd: new Vec3(...t.fwd),
    halfWidth: t.halfWidth, height: t.height
  };
}

export function serializeWorld(sim) {
  return {
    paths: sim.paths.map(serializePath),
    connectedEndpointIds: [...sim.connectedEndpointIds],
    trackFloorY: sim.trackFloorY,
    zones: sim.zones.map(serializeZone),
    triggers: sim.triggers.map(serializeTrigger)
  };
}
export function loadWorldIntoSim(sim, world) {
  sim.paths = world.paths.map(deserializePath);
  sim.connectedEndpointIds = new Set(world.connectedEndpointIds);
  sim.trackFloorY = world.trackFloorY;
  sim.zones = (world.zones || []).map(z => deserializeZone(z, sim.paths));
  sim.triggers = (world.triggers || []).map(deserializeTrigger);
  return sim;
}

// The full physics state. Every field createPhysicsState() produces plus the
// per-ship detection bookkeeping — anything omitted is a silent parity gap
// (CPP_PORT_PLAN.md §8).
const SCALARS = [
  'heading', 'speed', 'maxSpeed', 'maxReverse', 'accel', 'brakeDecel', 'friction',
  'turnRate', 'grip', 'wallRestitution', 'weight', 'bobTime', 'visualBank', 'visualPitch',
  'airborne', 'verticalVel', 'gravity', 'landingBounce', 'landingBounceVel',
  'boostActive', 'boostReleasing', 'boostHold', 'boostReleaseT', 'boostCap', 'boostEffCap'
];
const VECTORS = ['up', 'forward', 'right', 'groundPos', 'visualGroundPos', 'visualUp', 'moveDir'];

export function serializeShip(ship) {
  const p = ship.physics;
  const out = { physics: {} };
  for (const k of SCALARS) out.physics[k] = p[k];
  for (const k of VECTORS) out.physics[k] = v3(p[k]);
  out.prevTriggerPos = v3(ship.prevTriggerPos);
  out.zoneInside = [...ship.zoneInside.entries()];
  out.triggerStates = [...ship.triggerStates.entries()].map(([id, s]) => [id, { armed: s.armed, flash: s.flash }]);
  const c = ship.lastCheckpoint;
  out.lastCheckpoint = { valid: c.valid, triggerId: c.triggerId, pos: v3(c.pos), forward: v3(c.forward), up: v3(c.up) };
  if (ship.race) {
    // intermediateIds/finishId drive fireTrigger's lap logic and are constant
    // across the trace, but carried each step so a step loaded in isolation
    // (per-step replay) reconstructs the exact lap gate.
    out.race = {
      laps: ship.race.laps, hit: [...ship.race.hit],
      intermediateIds: ship.race.intermediateIds, finishId: ship.race.finishId
    };
  }
  // The respawn fallback pose (used when no checkpoint has been reached). Constant
  // across the trace; carried so a step that respawns replays identically.
  if (ship.startPose) {
    out.startPose = { pos: v3(ship.startPose.pos), up: v3(ship.startPose.up), forward: v3(ship.startPose.forward) };
  }
  return out;
}

// Build a headless ship whose state is exactly `s` (as produced by serializeShip).
export function deserializeShip(s) {
  const physics = createPhysicsState();
  for (const k of SCALARS) physics[k] = s.physics[k];
  for (const k of VECTORS) physics[k] = new Vec3(...s.physics[k]);
  const ship = {
    physics,
    zoneInside: new Map(s.zoneInside || []),
    triggerStates: new Map((s.triggerStates || []).map(([id, st]) => [id, { armed: st.armed, flash: st.flash }])),
    prevTriggerPos: new Vec3(...s.prevTriggerPos),
    lastCheckpoint: {
      valid: s.lastCheckpoint.valid, triggerId: s.lastCheckpoint.triggerId,
      pos: new Vec3(...s.lastCheckpoint.pos), forward: new Vec3(...s.lastCheckpoint.forward), up: new Vec3(...s.lastCheckpoint.up)
    },
    race: s.race ? {
      laps: s.race.laps, hit: new Set(s.race.hit),
      intermediateIds: s.race.intermediateIds || [], finishId: s.race.finishId || null,
      lapStartedAt: 0, totalStartedAt: 0, flashUntil: 0
    } : null,
    startPose: s.startPose
      ? { pos: new Vec3(...s.startPose.pos), up: new Vec3(...s.startPose.up), forward: new Vec3(...s.startPose.forward) }
      : null
  };
  return ship;
}
