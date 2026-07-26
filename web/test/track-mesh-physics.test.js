import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const coreSource = readFileSync(new URL('../track-core.js', import.meta.url), 'utf8');
const fakeWindow = {};
new Function('window', coreSource)(fakeWindow);
globalThis.TrackCore = fakeWindow.TrackCore;
const TrackCore = globalThis.TrackCore;

const TrackMesh = await import('../js/track-mesh.js');
const { bakeTrackPhysics } = await import('../js/track-bake.js');
const { Simulation, createShipState } = await import('../js/track-physics.js');
const { Vec3 } = await import('../js/vec3.js');

function fixture(name) {
  return TrackCore.parseTrack(readFileSync(new URL(`./fixtures/mesh/${name}`, import.meta.url), 'utf8'));
}

function simFor(track) {
  const world = bakeTrackPhysics(track);
  const sim = new Simulation({ TrackMesh, now: () => 0 });
  Object.assign(sim, {
    paths: world.paths, meshRegions: world.meshRegions, zones: world.zones, triggers: world.triggers,
    connectedEndpointIds: world.connectedEndpointIds, trackFloorY: world.trackFloorY
  });
  return { sim, world };
}

function shipAt(sim, track, position, forward = new Vec3(0, 0, 1)) {
  const ship = createShipState(track, 0);
  sim.placeShipAtPose(ship, { pos: position, up: new Vec3(0, 1, 0), forward });
  return ship;
}

test('headless bake compiles mesh regions, mesh effects, and the lowest respawn floor', () => {
  const effects = fixture('mesh-effects.json');
  const { world } = simFor(effects);
  assert.equal(world.meshRegions.length, 1);
  assert.equal(world.meshRegions[0].compiled.id, 'arena-placed');
  assert.equal(world.zones.find(z => z.id === 'mesh-boost').kind, 'mesh');
  assert.equal(world.triggers.find(t => t.id === 'mesh-finish').center.y, 5);

  const overlap = fixture('overlapping-elevations.json');
  overlap.meshes[0].elevation = -35;
  const baked = bakeTrackPhysics(overlap);
  assert.equal(baked.trackFloorY, -135, 'mesh elevation participates in fall recovery floor');
});

test('surface ownership selects the nearest overlapping mesh elevation', () => {
  const track = fixture('overlapping-elevations.json');
  const { sim } = simFor(track);
  const sample = sim.sampleTrack(0, 11, 0);
  assert.equal(sim.surfaceOwnerAt(0, 0, 11, sample).compiled.id, 'upper-deck');
  assert.equal(sim.surfaceOwnerAt(0, 0, 1, sample).compiled.id, 'lower-deck');
});

test('grounded mesh rail collision stops and reflects the ship', () => {
  const track = fixture('pad-with-hole.json');
  const { sim } = simFor(track);
  const ship = shipAt(sim, track, new Vec3(20, 4, -25), new Vec3(0, 0, -1));
  ship.physics.speed = 60;
  sim.stepPhysics(ship, 0.1, 0, 0, 0);
  assert.equal(ship.physics.airborne, false);
  assert.ok(ship.physics.groundPos.z > -30, `held inside rail at z=${ship.physics.groundPos.z}`);
  assert.ok(ship.physics.moveDir.z > 0, 'normal velocity reflected by restitution');
});

test('a bare mesh edge launches the ship when no corridor receives it', () => {
  const track = fixture('transformed-square.json');
  Object.assign(track.meshes[0], { x: 100, z: 0, rotation: 0, elevation: 8 });
  const { sim } = simFor(track);
  const ship = shipAt(sim, track, new Vec3(120, 8, 35));
  ship.physics.speed = 60;
  sim.stepPhysics(ship, 0.1, 0, 0, 0);
  assert.equal(ship.physics.airborne, true);
  assert.ok(ship.physics.groundPos.z > 40);
});

test('an airborne ship hits finite rails below their top and clears them above', () => {
  const track = fixture('pad-with-hole.json');
  const run = y => {
    const { sim } = simFor(track);
    const ship = shipAt(sim, track, new Vec3(20, y, -35));
    ship.physics.airborne = true;
    ship.physics.speed = 80;
    ship.physics.moveDir.set(0, 0, 1);
    sim.stepPhysics(ship, 0.1, 0, 0, 0);
    return ship.physics.groundPos;
  };
  const blocked = run(6);   // rail spans y=4..10
  assert.ok(blocked.z < -29, `below rail top remains outside, z=${blocked.z}`);
  const clear = run(12);
  assert.ok(clear.z > -30, `above rail top crosses wall, z=${clear.z}`);
});

test('an airborne ship lands on a solid mesh polygon but not its hole', () => {
  const track = fixture('pad-with-hole.json');
  const land = x => {
    const { sim } = simFor(track);
    const ship = shipAt(sim, track, new Vec3(x, 7, 0));
    ship.physics.airborne = true;
    ship.physics.verticalVel = -15;
    ship.physics.speed = 0;
    sim.stepPhysics(ship, 0.15, 0, 0, 0);
    return ship.physics;
  };
  const solid = land(20);
  assert.equal(solid.airborne, false);
  assert.equal(solid.groundPos.y, 4);
  const hole = land(0);
  assert.equal(hole.airborne, true, 'hole remains a void during airborne landing');
});

test('leaving an upper mesh transfers to an overlapping lower mesh', () => {
  const track = fixture('overlapping-elevations.json');
  Object.assign(track.meshes[1], { x: -55, z: -35, rotation: 0, elevation: 12 });
  const { sim } = simFor(track);
  const ship = shipAt(sim, track, new Vec3(10, 12, 0), new Vec3(1, 0, 0));
  ship.physics.speed = 80;
  sim.stepPhysics(ship, 0.1, 0, 0, 0);
  assert.equal(ship.physics.airborne, false);
  assert.equal(ship.physics.groundPos.y, 0, 'settled onto lower overlapping region');
});

test('leaving a mesh transfers to an underlying corridor without becoming airborne', () => {
  const track = fixture('corridor-mesh-bridge.json');
  const { sim } = simFor(track);
  const ship = shipAt(sim, track, new Vec3(0, 0, 35));
  ship.physics.speed = 80;
  sim.stepPhysics(ship, 0.1, 0, 0, 0);
  assert.equal(ship.physics.airborne, false);
  assert.ok(ship.physics.groundPos.z > 40, 'continued onto the path beyond the mesh boundary');
});

test('mesh-hosted boost and checkpoint use the generic per-ship state machines', () => {
  const track = fixture('mesh-effects.json');
  const { sim } = simFor(track);
  const ship = shipAt(sim, track, new Vec3(0, 5, 0));
  sim.stepPhysics(ship, 1 / 120, 0, 0, 0);
  assert.equal(ship.physics.boostActive, true);
  assert.equal(ship.zoneInside.get('mesh-boost'), true);

  ship.prevTriggerPos.set(0, 5, 19);
  ship.physics.groundPos.set(0, 5, 21);
  sim.detectTriggers(ship, ship.prevTriggerPos, ship.physics.groundPos);
  assert.equal(ship.lastCheckpoint.valid, true);
  assert.equal(ship.lastCheckpoint.triggerId, 'mesh-finish');
});
