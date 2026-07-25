/* js/track-session.js — headless (THREE-free) starting-grid layout, ship-
 * factory roster construction, and frame/substep session stepping with a
 * portable gameplay-event list. This is the authoritative description of the
 * non-graphics behavior `cpp/core`'s StartGrid.hpp/ShipFactory.hpp/
 * GameSession.hpp mirror (NATIVE_GAME_RUNTIME_PLAN.md), extracted from
 * js/track-game.js's DOM-coupled startingGridPoses/createShip/buildRoster/
 * updateShip so both the browser game and the parity harness describe the
 * same thing once. track-game.js layers THREE groups, ship colors, and
 * input-controller assignment on top of buildRoster's physics-only ships;
 * this module never touches THREE, the DOM, or a platform clock.
 *
 * Longitudinal grid offsets are positive distances behind the authored start
 * in the driven direction; lateral offsets are negative on the driver's left
 * and positive on the right (mirror of js/ship-grid.js's own convention).
 */

import { DEFAULT_SHIP_COUNT, gridSlot } from './ship-grid.js';
import {
  MAX_PHYSICS_STEP, createShipState, applyHandling,
  curvedSurfaceFrame, projectToSurface, tangentize, clamp
} from './track-physics.js';

const TC = () => globalThis.TrackCore;

// Half the ship's collision footprint used to keep the grid off the walls.
export const SHIP_HALF_WIDTH = 1.2;

// Largest frame delta accepted before clamping, so a debugger pause or a
// dropped frame cannot inject a huge, tunneling-prone physics step.
export const MAX_FRAME_DELTA = 0.05;

export { DEFAULT_SHIP_COUNT };

// The events a Session.step() call can report (mirror of cpp/core's
// GameEventType). Trigger crossings mirror Simulation's own onTriggerFired
// notice values ('fired' | 'checkpointAccepted' | 'lapCompleted') 1:1.
export const GameEventType = Object.freeze({
  TriggerFired: 'TriggerFired',
  CheckpointAccepted: 'CheckpointAccepted',
  LapCompleted: 'LapCompleted',
  Respawned: 'Respawned',
  RailHit: 'RailHit'
});

const NOTICE_TO_EVENT_TYPE = {
  fired: GameEventType.TriggerFired,
  checkpointAccepted: GameEventType.CheckpointAccepted,
  lapCompleted: GameEventType.LapCompleted
};

export const IDLE_INTENT = Object.freeze({ throttle: 0, brake: 0, steer: 0, respawn: false });

// Interpolates a corridor-style sample at `distanceBehind` metres behind
// `startIndex` along `path`'s centerline, walking backward (reverse=false) or
// forward (reverse=true) through the driven direction.
export function interpolatedGridFrame(path, startIndex, distanceBehind, reverse) {
  const cl = path.centerline, count = cl.length;
  const step = reverse ? 1 : -1;
  let at = startIndex, remaining = distanceBehind, next = at, frac = 0;
  for (let n = 0; n < count && remaining > 1e-9; n++) {
    const candidate = path.closed ? (at + step + count) % count : at + step;
    if (candidate < 0 || candidate >= count) break;
    const len = cl[at].pos.distanceTo(cl[candidate].pos);
    if (remaining <= len && len > 0) { next = candidate; frac = remaining / len; remaining = 0; break; }
    remaining -= len; at = candidate; next = at; frac = 0;
  }
  const a = cl[at], b = cl[next];
  const lerpVec = key => a[key].clone().lerp(b[key], frac).normalize();
  return {
    pos: a.pos.clone().lerp(b.pos, frac), tangent: lerpVec('tangent'), edgeRight: lerpVec('edgeRight'), normal: lerpVec('normal'),
    sLeft: a.sLeft + (b.sLeft - a.sLeft) * frac, sRight: a.sRight + (b.sRight - a.sRight) * frac,
    crossSectionCurvature: a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * frac,
    crossSectionTightness: a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * frac
  };
}

// Resolves the authored start (track.start) and produces one settled pose per
// grid slot: the alternating two-column staggered grid, compressed laterally
// on narrow roads, each analytically-placed slot then settled onto the same
// sampled curved surface physics uses.
export function startingGridPoses(sim, track, count = DEFAULT_SHIP_COUNT) {
  const trackStart = track.start || { path: 0, point: 0, reverse: false };
  const paths = sim.paths;
  const pathIndex = clamp(trackStart.path || 0, 0, paths.length - 1);
  const path = paths[pathIndex];
  const pointIndex = clamp(trackStart.point || 0, 0, path.anchors.length - 1);
  const anchor = path.anchors[pointIndex];
  const reverse = !!trackStart.reverse;

  let startIndex = 0, bestD = Infinity;
  for (let i = 0; i < path.centerline.length; i++) {
    const d = path.centerline[i].pos.distanceToSquared(anchor);
    if (d < bestD) { bestD = d; startIndex = i; }
  }

  return Array.from({ length: count }, (_, i) => {
    const rough = gridSlot(i);
    const frame = interpolatedGridFrame(path, startIndex, rough.behind, reverse);
    const lo = frame.sLeft + TC().COLLISION_WALL_MARGIN + SHIP_HALF_WIDTH;
    const hi = frame.sRight - TC().COLLISION_WALL_MARGIN - SHIP_HALF_WIDTH;
    const slot = gridSlot(i, { lateralLimit: Math.max(0, Math.min(-lo, hi)) });
    let surface = curvedSurfaceFrame(frame, slot.lateral);
    let canonical = frame;
    // Settle the analytically-placed slot onto the exact same sampled surface
    // the parked physics branch uses, so an idle ship does not creep while the
    // two representations converge over its first frames.
    for (let n = 0; n < 3; n++) {
      canonical = sim.sampleTrack(surface.pos.x, surface.pos.y, surface.pos.z);
      const proj = projectToSurface(canonical, surface.pos.x, surface.pos.y, surface.pos.z);
      surface = curvedSurfaceFrame(canonical, clamp(proj.s, proj.loS, proj.hiS));
    }
    const forward = canonical.tangent.clone().multiplyScalar(reverse ? -1 : 1).normalize();
    tangentize(forward, surface.normal, forward);
    return { pos: surface.pos, up: surface.normal, forward, slot };
  });
}

// Builds one fully-initialized headless ship at `pose`: applies handling,
// initializes race/detection state, and places it (which also clears boost
// and arms triggers).
export function makeShip(sim, track, pose, now = 0) {
  const ship = createShipState(track, now);
  applyHandling(track, ship.physics);
  ship.startPose = { pos: pose.pos, up: pose.up, forward: pose.forward };
  sim.placeShipAtPose(ship, ship.startPose);
  return ship;
}

// Builds a full roster of `count` headless ships on the authored starting
// grid (physics-only — no THREE.Group, color, or controller; track-game.js
// layers those on top of ships this function returns).
export function buildRoster(sim, track, count = DEFAULT_SHIP_COUNT, now = 0) {
  return startingGridPoses(sim, track, count).map(pose => makeShip(sim, track, pose, now));
}

// Owns a Simulation + roster and reproduces the browser game's per-frame
// orchestration: clamps dt, divides moving ships into equal MAX_PHYSICS_STEP
// sub-steps, processes explicit/automatic respawns, and collects one frame's
// gameplay events. Installs its own onTriggerFired/now hooks on `sim` for the
// duration of its lifetime — construct one Session per Simulation instance.
export class Session {
  constructor(sim, ships) {
    this.sim = sim;
    this.ships = ships;
    this.events = [];
    this.sessionTime = 0;
    sim.onTriggerFired = (ship, rec, dir, notice) => {
      const type = NOTICE_TO_EVENT_TYPE[notice] || GameEventType.TriggerFired;
      this.events.push({ type, shipIndex: this.ships.indexOf(ship), triggerId: rec.id, direction: dir, automatic: false });
    };
    sim.now = () => this.sessionTime;
  }

  // Advances every ship by one rendered frame. Replaces the previous frame's
  // event list.
  step(intents, dt) {
    this.events.length = 0;
    const clamped = Math.min(dt, MAX_FRAME_DELTA);
    this.sessionTime += clamped;

    this.ships.forEach((ship, i) => {
      const intent = (intents && intents[i]) || IDLE_INTENT;
      if (intent.respawn) {
        this.sim.respawn(ship);
        this.events.push({ type: GameEventType.Respawned, shipIndex: i, triggerId: '', direction: '', automatic: false });
        return;
      }

      const subSteps = Math.max(1, Math.ceil(clamped / MAX_PHYSICS_STEP));
      const sdt = clamped / subSteps;
      for (let s = 0; s < subSteps; s++) {
        const r = this.sim.stepPhysics(ship, sdt, intent.throttle || 0, intent.brake || 0, intent.steer || 0);
        if (r.railHit) this.events.push({ type: GameEventType.RailHit, shipIndex: i, triggerId: '', direction: '', automatic: false });
        if (r.respawned) {
          this.events.push({ type: GameEventType.Respawned, shipIndex: i, triggerId: '', direction: '', automatic: true });
          break;  // ship already reset; skip the rest of this frame's sub-steps
        }
      }
    });
  }
}

// Builds a Simulation-backed Session with a freshly-built roster in one call.
export function createSession(sim, track, count = DEFAULT_SHIP_COUNT) {
  const session = new Session(sim, []);
  session.ships = buildRoster(sim, track, count, session.sessionTime);
  return session;
}
