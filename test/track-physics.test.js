import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// track-core.js is a classic browser IIFE (window.TrackCore); the physics core
// reads it off the global lazily, so we install it before any physics runs.
// (The static imports below never touch TrackCore at load time.)
function loadTrackCore() {
  const src = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
  const fakeWindow = {};
  new Function('window', src)(fakeWindow);
  return fakeWindow.TrackCore;
}
globalThis.TrackCore = loadTrackCore();
const TrackCore = globalThis.TrackCore;

const {
  Simulation, createShipState, curvedSurfaceFrame, projectToSurface, corridorContains,
  tangentize, signedAngleAbout, beginAirborne, landOnSurface, triggerBoost, tickBoost,
  effectiveMaxSpeed, createPhysicsState, clamp, UP
} = await import('../js/track-physics.js');
const { Vec3 } = await import('../js/vec3.js');
const { bakeTrackPhysics, startPose } = await import('../js/track-bake.js');

// Build a Simulation on a normalized track (no mesh, so TrackMesh is unused).
function simFor(track, opts = {}) {
  const sim = new Simulation({ now: () => 0, ...opts });
  const { paths, connectedEndpointIds, trackFloorY } = bakeTrackPhysics(track);
  sim.paths = paths;
  sim.connectedEndpointIds = connectedEndpointIds;
  sim.trackFloorY = trackFloorY;
  return sim;
}

// Place a headless ship on the track at its start control point, mirroring the
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
  const applied = TrackCore.normalizeHandling(track.handling);
  ship.physics.maxSpeed = applied.maxSpeed;
  ship.physics.accel = applied.accel;
  ship.physics.turnRate = applied.turnSpeed * Math.PI / 180;
  ship.physics.weight = applied.weight;
  sim.placeShipAtPose(ship, { pos: surface.pos, up: surface.normal, forward });
  return ship;
}

const starter = () => TrackCore.cloneTrack(TrackCore.STARTER_TRACK);

test('bakeTrackPhysics produces a usable corridor', () => {
  const { paths, trackFloorY } = bakeTrackPhysics(starter());
  assert.ok(paths.length >= 1);
  const cl = paths[0].centerline;
  assert.ok(cl.length > 10);
  for (const f of cl) {
    assert.ok(Number.isFinite(f.pos.x) && Number.isFinite(f.pos.y) && Number.isFinite(f.pos.z));
    assert.ok(f.sLeft < f.sRight, 'left edge offset is left of right edge');
    assert.ok(f.pos instanceof Vec3);
  }
  assert.ok(Number.isFinite(trackFloorY) && trackFloorY < 0);
});

test('a placed ship rests on the corridor surface (supported, grounded)', () => {
  const track = starter();
  const sim = simFor(track);
  const ship = placeAtStart(sim, track);
  const p = ship.physics;
  assert.equal(p.airborne, false);
  const s = sim.sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
  const proj = projectToSurface(s, p.groundPos.x, p.groundPos.y, p.groundPos.z);
  assert.ok(corridorContains(s, p.groundPos.x, p.groundPos.y, p.groundPos.z, proj),
    'start pose is over the drivable corridor');
});

test('throttle accelerates, then speed clamps at maxSpeed; ship stays grounded', () => {
  const track = starter();
  const sim = simFor(track);
  const ship = placeAtStart(sim, track);
  const p = ship.physics;
  const dt = 1 / 120;

  let prev = p.speed;
  let sawIncrease = false;
  for (let i = 0; i < 60; i++) {           // ~0.5s of accel
    const r = sim.stepPhysics(ship, dt, 1, 0, 0);
    assert.equal(r.respawned, false);
    if (p.speed > prev + 1e-9) sawIncrease = true;
    prev = p.speed;
  }
  assert.ok(sawIncrease, 'ship gained speed under throttle');
  assert.ok(p.speed > 10, `expected meaningful speed, got ${p.speed}`);

  // Drive long enough to confirm speed never exceeds maxSpeed. (With no steering
  // the ship ploughs into the curve's outer wall and bounces, so it won't reach
  // the cap — the invariant under test is the clamp, not top speed.)
  let maxSeen = p.speed;
  for (let i = 0; i < 1200; i++) {
    sim.stepPhysics(ship, dt, 1, 0, 0);
    maxSeen = Math.max(maxSeen, p.speed);
    assert.ok(p.speed <= p.maxSpeed + 1e-9, `speed ${p.speed} exceeded maxSpeed ${p.maxSpeed}`);
  }
  assert.ok(maxSeen > 40, `expected to build real speed, peaked at ${maxSeen}`);
  assert.equal(p.airborne, false, 'stayed grounded on the closed loop');
});

test('steering turns the ship (heading changes)', () => {
  const track = starter();
  const sim = simFor(track);
  const ship = placeAtStart(sim, track);
  const p = ship.physics;
  const dt = 1 / 120;
  for (let i = 0; i < 30; i++) sim.stepPhysics(ship, dt, 1, 0, 0);   // build speed
  const before = p.forward.clone();
  for (let i = 0; i < 60; i++) sim.stepPhysics(ship, dt, 1, 0, 1);   // steer left
  const dot = clamp(before.dot(p.forward), -1, 1);
  assert.ok(Math.acos(dot) > 0.05, 'facing rotated under sustained steering');
});

test('physics is deterministic: identical inputs from identical state match exactly', () => {
  const track = starter();
  const run = () => {
    const sim = simFor(track);
    const ship = placeAtStart(sim, track);
    const dt = 1 / 120;
    // A scripted, varied input sequence.
    for (let i = 0; i < 200; i++) {
      const throttle = i % 7 === 0 ? 0 : 1;
      const brake = i % 23 === 0 ? 1 : 0;
      const steer = ((i % 5) - 2);
      sim.stepPhysics(ship, dt, throttle, brake, steer);
    }
    const p = ship.physics;
    return [p.groundPos.x, p.groundPos.y, p.groundPos.z, p.speed, p.heading,
      p.forward.x, p.forward.y, p.forward.z, p.moveDir.x, p.moveDir.y, p.moveDir.z];
  };
  const a = run(), b = run();
  for (let i = 0; i < a.length; i++) {
    assert.equal(a[i], b[i], `field ${i} diverged: ${a[i]} vs ${b[i]}`);
  }
});

test('tangentize: projects onto the plane, falls back when parallel to the normal', () => {
  const v = new Vec3(1, 1, 0);
  const out = tangentize(v.clone(), new Vec3(0, 1, 0), new Vec3(1, 0, 0));
  assert.ok(Math.abs(out.y) < 1e-12, 'y-component removed');
  assert.ok(Math.abs(out.length() - 1) < 1e-12, 'renormalized');
  // v parallel to n -> zero projection -> fallback returned.
  const fb = new Vec3(0, 0, 1);
  const par = tangentize(new Vec3(0, 5, 0), new Vec3(0, 1, 0), fb);
  assert.deepEqual([par.x, par.y, par.z], [0, 0, 1]);
});

test('signedAngleAbout: right-hand sign about an axis', () => {
  const a = new Vec3(1, 0, 0), b = new Vec3(0, 0, -1);   // +X -> -Z is +90 about +Y
  const ang = signedAngleAbout(a, b, new Vec3(0, 1, 0));
  assert.ok(Math.abs(ang - Math.PI / 2) < 1e-9);
  const ang2 = signedAngleAbout(a, b, new Vec3(0, -1, 0));  // flip axis -> flip sign
  assert.ok(Math.abs(ang2 + Math.PI / 2) < 1e-9);
});

test('beginAirborne / landOnSurface round-trip on flat ground', () => {
  const ship = { physics: createPhysicsState() };
  ship.physics.forward.set(0, 0, 1);
  beginAirborne(ship, new Vec3(0, 5, 30));
  assert.equal(ship.physics.airborne, true);
  assert.equal(ship.physics.verticalVel, 5);
  assert.ok(Math.abs(ship.physics.speed - 30) < 1e-12, 'horizontal speed preserved');
  assert.ok(Math.abs(ship.physics.moveDir.y) < 1e-12, 'air travel dir is horizontal');
  landOnSurface(ship, UP);
  assert.equal(ship.physics.airborne, false);
  assert.equal(ship.physics.verticalVel, 0);
});

test('boost state machine: hold at cap, then smooth release to maxSpeed', () => {
  const ship = { physics: createPhysicsState(), zoneInside: new Map() };
  const p = ship.physics;
  p.maxSpeed = 140; p.speed = 100;
  triggerBoost(ship, { effect: 'velocityChange', factor: 1.5, duration: 2 });
  assert.equal(p.boostActive, true);
  assert.ok(p.boostCap > p.maxSpeed);
  assert.equal(effectiveMaxSpeed(p), p.boostCap, 'cap raised while boosting');
  assert.ok(p.speed >= p.boostCap, 'speed snapped up to the boost cap');
  // Hold phase: 2s hold (+ margin) elapses, then it enters release.
  for (let i = 0; i < 250; i++) tickBoost(ship, 1 / 120);
  assert.equal(p.boostReleasing, true);
  assert.equal(p.boostActive, true, 'still boosting during the release ramp');
  // Release phase eases the effective cap back to maxSpeed over ZONE_RELEASE (1s).
  for (let i = 0; i < 140; i++) tickBoost(ship, 1 / 120);
  assert.equal(p.boostActive, false);
  assert.equal(p.boostEffCap, p.maxSpeed);
  assert.equal(effectiveMaxSpeed(p), p.maxSpeed);
});
