/* Raw current-schema parity scenarios. JS loads and bakes each source track;
 * traces retain that normalized source (not the baked world) so C++ repeats the
 * loader, spline bake, Willpower mesh compile and physics step independently. */
import { readFileSync } from 'node:fs';
import { createShipState, tangentize } from '../../js/track-physics.js';
import { Vec3 } from '../../js/vec3.js';
import { buildSimFor, placeAtStart } from './trace.js';
export { buildSimFor };
import { serializeShip } from './state.js';
import { makeAutopilot } from './autopilot.js';

const TC = () => globalThis.TrackCore;
const fixture = name => TC().parseTrack(readFileSync(new URL(`../fixtures/mesh/${name}`, import.meta.url), 'utf8'));
const pathFixture = name => TC().parseTrack(readFileSync(new URL(`../fixtures/path/${name}`, import.meta.url), 'utf8'));
const controls = (steps, control) => Array.from({ length: steps }, () => ({ ...control }));
const direction = (x, z) => new Vec3(x, 0, z).normalize();

export function surfaceLabel(sim, ship) {
  if (ship.physics.airborne) return 'airborne';
  const p = ship.physics.groundPos;
  const sample = sim.sampleTrack(p.x, p.y, p.z);
  const mesh = sim.surfaceOwnerAt(p.x, p.z, p.y, sample);
  return mesh ? `mesh:${mesh.compiled.id}` : `path:${sim.paths.indexOf(sample.pathObj)}`;
}

export function buildRawTrace(track, { name, initial, inputs, steps: requestedSteps }) {
  const sim = buildSimFor(track);
  let ship;
  if (initial.trackStart) {
    ship = placeAtStart(sim, track);
  } else {
    ship = createShipState(track, 0);
    const forward = initial.forward.clone().normalize();
    ship.startPose = { pos: initial.pos.clone(), up: new Vec3(0, 1, 0), forward: forward.clone() };
    sim.placeShipAtPose(ship, ship.startPose);
    const handling = TC().normalizeHandling(track.handling);
    ship.physics.maxSpeed = handling.maxSpeed;
    ship.physics.accel = handling.accel;
    ship.physics.turnRate = handling.turnSpeed * Math.PI / 180;
    ship.physics.weight = handling.weight;
    ship.physics.moveDir.copy(forward);
    tangentize(ship.physics.moveDir, ship.physics.up, forward);
  }
  ship.physics.speed = initial.speed || 0;
  if (initial.airborne) {
    ship.physics.airborne = true;
    ship.physics.verticalVel = initial.verticalVel || 0;
  }

  const initialState = serializeShip(ship);
  const steps = [];
  for (let i = 0; i < (typeof inputs === 'function' ? requestedSteps : inputs.length); ++i) {
    const input = typeof inputs === 'function' ? inputs(sim, ship, i) : inputs[i];
    const control = { throttle: 0, brake: 0, steer: 0, dt: 1 / 120, ...input };
    const result = sim.stepPhysics(ship, control.dt, control.throttle, control.brake, control.steer);
    steps.push({
      control,
      outcome: { surface: surfaceLabel(sim, ship), railHit: !!result.railHit, respawned: !!result.respawned },
      after: serializeShip(ship)
    });
  }
  return {
    meta: { name, kind: 'raw-track', steps: steps.length },
    sourceTrack: JSON.parse(TC().serializeTrack(track)),
    initialState,
    steps
  };
}

export function rawScenarios() {
  const scenarios = [];
  scenarios.push({
    name: 'raw-corridor-mesh-corridor', track: fixture('corridor-mesh-bridge.json'),
    initial: { pos: new Vec3(0, 0, -50), forward: direction(0, 1), speed: 80 },
    inputs: controls(150, { throttle: 1, dt: 1 / 120 }), require: { surfaces: ['path:0', 'mesh:bridge-pad'] }
  });

  const ledge = fixture('transformed-square.json');
  Object.assign(ledge.meshes[0], { x: 100, z: 0, rotation: 0, elevation: 8 });
  ledge.meshes.push({ id: 'lower-pad', asset: 'pad', x: 100, z: 45, rotation: 0, elevation: 0 });
  scenarios.push({
    name: 'raw-bare-ledge-lower-landing', track: ledge,
    initial: { pos: new Vec3(120, 8, 35), forward: direction(0, 1), speed: 10 },
    inputs: controls(30, { throttle: 1, dt: 0.1 }), require: { airborne: true, surfaces: ['mesh:square-placed', 'mesh:lower-pad'] }
  });

  scenarios.push({
    name: 'raw-head-on-rail', track: fixture('pad-with-hole.json'),
    initial: { pos: new Vec3(20, 4, -25), forward: direction(0, -1), speed: 60 },
    inputs: controls(4, { dt: 0.1 }), require: { railHits: 1 }
  });
  scenarios.push({
    name: 'raw-glancing-rail', track: fixture('mesh-effects.json'),
    initial: { pos: new Vec3(-10, 5, -48), forward: direction(20, -5), speed: 100 },
    inputs: controls(3, { dt: 0.1 }), require: { railHits: 1 }
  });
  scenarios.push({
    name: 'raw-corner-rail', track: fixture('mesh-effects.json'),
    initial: { pos: new Vec3(-45, 5, -45), forward: direction(-1, -1), speed: 100 },
    inputs: controls(3, { dt: 0.1 }), require: { railHits: 1 }
  });
  scenarios.push({
    name: 'raw-airborne-outside-below-rail', track: fixture('pad-with-hole.json'),
    initial: { pos: new Vec3(20, 6, -35), forward: direction(0, 1), speed: 80, airborne: true },
    inputs: controls(2, { dt: 0.1 }), require: { railHits: 1 }
  });
  scenarios.push({
    name: 'raw-airborne-clears-rail', track: fixture('pad-with-hole.json'),
    initial: { pos: new Vec3(20, 12, -35), forward: direction(0, 1), speed: 80, airborne: true },
    inputs: controls(2, { dt: 0.1 }), require: { railHits: 0 }
  });

  const overlap = fixture('overlapping-elevations.json');
  Object.assign(overlap.meshes[1], { x: -55, z: -35, rotation: 0, elevation: 12 });
  scenarios.push({
    name: 'raw-overlapping-elevations', track: overlap,
    initial: { pos: new Vec3(10, 12, 0), forward: direction(1, 0), speed: 80 },
    inputs: [{ dt: 0 }, ...controls(3, { dt: 0.1 })], require: { surfaces: ['mesh:upper-deck', 'mesh:lower-deck'] }
  });

  scenarios.push({
    name: 'raw-mesh-boost-checkpoint', track: fixture('mesh-effects.json'),
    initial: { pos: new Vec3(0, 5, 0), forward: direction(0, 1), speed: 80 },
    inputs: controls(8, { throttle: 1, dt: 0.1 }), require: { boost: true, checkpoint: 'mesh-finish' }
  });
  scenarios.push({
    name: 'raw-hole-traversal', track: fixture('pad-with-hole.json'),
    initial: { pos: new Vec3(20, 4, -25), forward: direction(0, 1), speed: 60 },
    inputs: controls(10, { throttle: 1, dt: 0.1 }), require: { surfaces: ['mesh:hole-pad'] }
  });

  const curvedAutopilot = makeAutopilot(9090);
  scenarios.push({
    name: 'raw-curved-banked-native-bake', track: pathFixture('curved-banked.json'),
    initial: { trackStart: true, speed: 0 }, steps: 300,
    inputs: (simulation, ship) => ({ ...curvedAutopilot(simulation, ship), dt: 1 / 120 }),
    require: { surfaces: ['path:0'] }
  });

  const mixed = fixture('corridor-mesh-bridge.json');
  mixed.zones = [{ id: 'mixed-boost', effect: 'velocityChange', factor: 1.4, duration: 1.25,
    width: 20, length: 30, host: { kind: 'mesh', meshId: 'bridge-pad', x: 0, z: 0, rotation: 0 } }];
  mixed.triggers = [{ id: 'mixed-finish', type: 'checkpoint', role: 'finish', direction: 'both',
    width: 50, height: 15, rotation: 0, host: { kind: 'mesh', meshId: 'bridge-pad', x: 0, z: 20 } }];
  scenarios.push({
    name: 'raw-long-mixed-course', track: mixed,
    initial: { pos: new Vec3(0, 0, -70), forward: direction(0, 1), speed: 80 },
    inputs: controls(600, { throttle: 1, dt: 1 / 120 }),
    require: { surfaces: ['path:0', 'mesh:bridge-pad'], boost: true, checkpoint: 'mixed-finish' }
  });
  return scenarios;
}

export function validateRawActivity(trace, require = {}) {
  const surfaces = new Set(trace.steps.map(step => step.outcome.surface));
  for (const surface of require.surfaces || [])
    if (!surfaces.has(surface)) throw new Error(`${trace.meta.name} never selected ${surface}`);
  const railHits = trace.steps.filter(step => step.outcome.railHit).length;
  if (require.railHits != null && (require.railHits === 0 ? railHits !== 0 : railHits < require.railHits))
    throw new Error(`${trace.meta.name} rail hits ${railHits}, expected ${require.railHits}`);
  if (require.airborne && !trace.steps.some(step => step.after.physics.airborne))
    throw new Error(`${trace.meta.name} never became airborne`);
  if (require.boost && !trace.steps.some(step => step.after.physics.boostActive))
    throw new Error(`${trace.meta.name} never activated boost`);
  if (require.checkpoint && !trace.steps.some(step => step.after.lastCheckpoint.triggerId === require.checkpoint))
    throw new Error(`${trace.meta.name} never fired ${require.checkpoint}`);
  return { surfaces: [...surfaces], railHits,
    airborneSteps: trace.steps.filter(step => step.after.physics.airborne).length,
    boostSteps: trace.steps.filter(step => step.after.physics.boostActive).length,
    checkpointSteps: trace.steps.filter(step => step.after.lastCheckpoint.valid).length };
}
