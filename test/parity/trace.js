/* test/parity/trace.js — turn a normalized, mesh-free track into a golden trace:
 *
 *   { meta, world, initialState, steps: [ { control, after }, ... ] }
 *
 * `world` is the baked corridor (serialized losslessly); `initialState` is the
 * ship's starting physics state; each step records the control input fed to
 * stepPhysics and the FULL resulting ship state. Per-step replay loads the prior
 * step's `after` (or `initialState` for step 0), runs one stepPhysics, and must
 * reproduce this step's `after`. That is the primary parity gate.
 *
 * JS is the generator (CPP_PORT_PLAN.md §5); C++ never generates a track.
 */

import { Simulation, createShipState, curvedSurfaceFrame, projectToSurface, tangentize, clamp } from '../../js/track-physics.js';
import { bakeTrackPhysics, startPose } from '../../js/track-bake.js';
import { serializeWorld, serializeShip } from './state.js';
import { makeAutopilot } from './autopilot.js';

const TC = () => globalThis.TrackCore;

// Place a headless ship on-track at the start control point, mirroring the
// settling loop in track-game.js startingGridPoses (single ship, lateral 0).
function placeAtStart(sim, track) {
  const ship = createShipState(track, 0);
  const { frame, reverse } = startPose(sim, track);
  let surface = curvedSurfaceFrame(frame, 0);
  let canonical = frame;
  for (let n = 0; n < 3; n++) {
    canonical = sim.sampleTrack(surface.pos.x, surface.pos.y, surface.pos.z);
    const proj = projectToSurface(canonical, surface.pos.x, surface.pos.y, surface.pos.z);
    surface = curvedSurfaceFrame(canonical, clamp(proj.s, proj.loS, proj.hiS));
  }
  const forward = canonical.tangent.clone().multiplyScalar(reverse ? -1 : 1).normalize();
  tangentize(forward, surface.normal, forward);

  const h = TC().normalizeHandling(track.handling);
  ship.physics.maxSpeed = h.maxSpeed;
  ship.physics.accel = h.accel;
  ship.physics.turnRate = h.turnSpeed * Math.PI / 180;
  ship.physics.weight = h.weight;

  // Mirror buildRoster: the start pose doubles as the respawn fallback when no
  // checkpoint has been reached yet (sim.respawn reads ship.startPose).
  ship.startPose = { pos: surface.pos, up: surface.normal, forward };
  sim.placeShipAtPose(ship, ship.startPose);
  return ship;
}

export function buildSimFor(track) {
  const sim = new Simulation({ now: () => 0 });
  const { paths, connectedEndpointIds, trackFloorY, zones, triggers } = bakeTrackPhysics(track);
  sim.paths = paths;
  sim.connectedEndpointIds = connectedEndpointIds;
  sim.trackFloorY = trackFloorY;
  sim.zones = zones;
  sim.triggers = triggers;
  return sim;
}

export function buildTrace(track, { seed = 1, steps = 400, dt = 1 / 120, name = 'trace' } = {}) {
  const sim = buildSimFor(track);
  const ship = placeAtStart(sim, track);
  const auto = makeAutopilot(seed);

  const world = serializeWorld(sim);
  const initialState = serializeShip(ship);
  const out = [];
  for (let i = 0; i < steps; i++) {
    const { throttle, brake, steer } = auto(sim, ship);
    sim.stepPhysics(ship, dt, throttle, brake, steer);
    out.push({ control: { throttle, brake, steer, dt }, after: serializeShip(ship) });
  }
  return { meta: { name, dt, seed, steps }, world, initialState, steps: out };
}
