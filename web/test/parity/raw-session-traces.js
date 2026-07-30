/* test/parity/raw-session-traces.js — the raw-session parity corpus
 * (NATIVE_GAME_RUNTIME_PLAN.md's completion criteria): proves native ship/
 * session INITIALIZATION independently matches JavaScript, and that
 * GameSession-style frame/substep orchestration and gameplay events match,
 * starting from schema-10 JSON plus scripted controls only — no JS-baked
 * world and no JS-created initialState.
 *
 * Two fixture kinds:
 *  - "raw-session-init": the roster js/track-session.js's buildRoster()
 *    produces independently from sourceTrack (no steps). Proves starting-grid
 *    poses, applied handling, and race-state derivation.
 *  - "raw-session-step": a Session driven frame-by-frame with scripted
 *    per-ship ControlIntents. Each recorded frame carries the full roster +
 *    session-time snapshot BEFORE the frame, the intents, the snapshot AFTER,
 *    and the ordered event list — the same bit-exact-replay-from-recorded-
 *    state methodology test/parity/raw-traces.js already uses, so a native
 *    replayer can reload the "before" state and reproduce "after" + events
 *    exactly without needing to also reproduce JS's own free-running
 *    trajectory.
 */
import { readFileSync } from 'node:fs';
import { buildSimFor } from './trace.js';
import { serializeShip } from './state.js';
import { makeAutopilot } from './autopilot.js';
import { buildRoster, createSession, DEFAULT_SHIP_COUNT } from '../../js/track-session.js';

const TC = () => globalThis.TrackCore;
const pathFixture = name => TC().parseTrack(readFileSync(new URL(`../fixtures/path/${name}`, import.meta.url), 'utf8'));
const meshFixture = name => TC().parseTrack(readFileSync(new URL(`../fixtures/mesh/${name}`, import.meta.url), 'utf8'));

function serializeRoster(ships) { return ships.map(serializeShip); }

// A minimal flat, closed circular track with a generously-sized finish gate.
// curved-banked.json's own authored finish trigger sits just below the
// physical crowned/banked driving surface at its gate (a pre-existing
// property of that fixture's authored banking, independent of this harness),
// so it rarely satisfies the trigger's height bound — a flat track keeps the
// lap-completion scenario's pass/fail about session/event plumbing, not
// authored-fixture trigger geometry.
//
// The finish host t is 0.10125, deliberately NOT a round 0.1. triggerPathFrame()
// evaluates the spline continuously, but this path bakes to exactly 400
// centerline frames, so t=0.1 put the gate's plane bit-exactly on frame 40 —
// and a ship's ground position is always curvedSurfaceFrame(sample, s), which
// has zero tangential offset from its sample's own pos. Whenever the ship
// snapped to frame 40, (pos - center).fwd was therefore exactly zero in exact
// arithmetic, leaving detectTriggers' strict `d1 > 0` crossing test decided by
// the last bit: JS and C++ agree on position to ~1e-14 yet disagree about which
// frame the lap fires on. 0.10125 lands the plane midway between frames 40 and
// 41 (~0.71m of a ~1.42m spacing), so the sign of d is never in doubt. Any t
// that is an exact multiple of the frame spacing re-creates the landmine.
function syntheticFlatLapTrack() {
  return TC().parseTrack(JSON.stringify({
    version: 10, name: 'raw-session synthetic flat lap',
    start: { path: 0, point: 0, reverse: false },
    handling: { maxSpeed: 140, accel: 71, turnSpeed: 137.5, weight: 1000 },
    zones: [],
    triggers: [{
      id: 'lap-finish', type: 'checkpoint', role: 'finish', direction: 'both',
      width: 60, height: 20, rotation: 0, host: { kind: 'path', pathId: 'circle', t: 0.10125 }
    }],
    disjointSeams: [], junctions: [], selfIntersectionOverrides: [],
    meshAssets: {}, meshes: [], textureAssets: {},
    paths: [{
      id: 'circle', closed: true, points: [
        { type: 'position', id: 'p0', pos: [100, 0, 0], weight: 1 },
        { type: 'position', id: 'p1', pos: [70.71, 0, 70.71], weight: 1 },
        { type: 'position', id: 'p2', pos: [0, 0, 100], weight: 1 },
        { type: 'position', id: 'p3', pos: [-70.71, 0, 70.71], weight: 1 },
        { type: 'position', id: 'p4', pos: [-100, 0, 0], weight: 1 },
        { type: 'position', id: 'p5', pos: [-70.71, 0, -70.71], weight: 1 },
        { type: 'position', id: 'p6', pos: [0, 0, -100], weight: 1 },
        { type: 'position', id: 'p7', pos: [70.71, 0, -70.71], weight: 1 },
        { type: 'width', t: 0, width: 36 },
        { type: 'roll', t: 0, roll: 0 },
        { type: 'crossSection', t: 0, curvature: 0, tightness: 1, thickness: 4 }
      ]
    }]
  }));
}

// --- Tier A: initial-roster fixtures ---------------------------------------

export function buildRawSessionInitTrace(track, { name, shipCount = DEFAULT_SHIP_COUNT } = {}) {
  const sim = buildSimFor(track);
  const roster = buildRoster(sim, track, shipCount, 0);
  return {
    meta: { name, kind: 'raw-session-init', shipCount },
    sourceTrack: JSON.parse(TC().serializeTrack(track)),
    roster: serializeRoster(roster)
  };
}

export function rawSessionInitScenarios() {
  const narrow = pathFixture('curved-banked.json');
  narrow.paths[0].points = narrow.paths[0].points.map(p =>
    p.type === 'width' ? { ...p, width: 8 } : p);

  return [
    // Closed, banked, non-uniform-width path with an authored REVERSED start
    // (start.reverse is true in the fixture itself) — covers both the
    // "closed" and "reversed start" completion criteria in one fixture.
    { name: 'raw-session-init-closed-reversed', track: pathFixture('curved-banked.json'), shipCount: 8 },
    // Open path: the grid's backward walk off the start index must clamp
    // instead of wrapping.
    { name: 'raw-session-init-open', track: meshFixture('mesh-effects.json'), shipCount: 8 },
    // Narrow road: lateral grid spacing must compress within the corridor.
    { name: 'raw-session-init-narrow', track: narrow, shipCount: 8 }
  ];
}

// --- Tier B: session-step fixtures ------------------------------------------

export function buildRawSessionStepTrace(track, { name, shipCount = DEFAULT_SHIP_COUNT, dt = 1 / 60, frameIntents }) {
  const sim = buildSimFor(track);
  const session = createSession(sim, track, shipCount);

  const steps = [];
  const frameCount = typeof frameIntents === 'function' ? frameIntents.frames : frameIntents.length;
  for (let i = 0; i < frameCount; i++) {
    const before = { roster: serializeRoster(session.ships), sessionTime: session.sessionTime };
    const intents = typeof frameIntents === 'function' ? frameIntents(session, i) : frameIntents[i];
    session.step(intents, dt);
    steps.push({
      intents,
      dt,
      before,
      after: { roster: serializeRoster(session.ships), sessionTime: session.sessionTime },
      events: session.events.map(e => ({ ...e }))
    });
  }
  return {
    meta: { name, kind: 'raw-session-step', shipCount, steps: steps.length },
    sourceTrack: JSON.parse(TC().serializeTrack(track)),
    steps
  };
}

export function rawSessionStepScenarios() {
  const scenarios = [];

  // A single autopilot-driven ship completes a lap on the closed, reversed-
  // start, banked track: 'curve-finish' has role finish with zero
  // intermediates, so one forward crossing both accepts the checkpoint and
  // immediately laps (CheckpointAccepted -> LapCompleted, same crossing).
  const lapAutopilot = makeAutopilot(4242);
  const lapFrames = (session, i) => [{ ...lapAutopilot(session.sim, session.ships[0]), respawn: false }];
  lapFrames.frames = 900;
  scenarios.push({
    name: 'raw-session-step-lap', track: syntheticFlatLapTrack(), shipCount: 1,
    frameIntents: lapFrames, require: { lapCompleted: true }
  });

  // A full 8-ship roster driven with a fixed scripted throttle (no autopilot
  // required — proves multi-ship stepping, not multi-ship navigation).
  const scriptedThrottle = (session, i) =>
    session.ships.map(() => ({ throttle: 1, brake: 0, steer: 0, respawn: false }));
  scriptedThrottle.frames = 30;
  scenarios.push({
    name: 'raw-session-step-roster', track: pathFixture('curved-banked.json'), shipCount: 8,
    frameIntents: scriptedThrottle, require: {}
  });

  // A single ship drives forward, then an explicit respawn intent mid-run,
  // then a few idle frames after.
  const respawnFrames = (session, i) => [{
    throttle: i < 10 ? 1 : 0, brake: 0, steer: 0, respawn: i === 10
  }];
  respawnFrames.frames = 15;
  scenarios.push({
    name: 'raw-session-step-respawn', track: pathFixture('curved-banked.json'), shipCount: 1,
    frameIntents: respawnFrames, require: { respawnedExplicit: true }
  });

  return scenarios;
}

export function validateRawSessionActivity(trace, require = {}) {
  const allEvents = trace.steps.flatMap(step => step.events);
  if (require.lapCompleted && !allEvents.some(e => e.type === 'LapCompleted'))
    throw new Error(`${trace.meta.name} never completed a lap`);
  if (require.respawnedExplicit && !allEvents.some(e => e.type === 'Respawned' && !e.automatic))
    throw new Error(`${trace.meta.name} never fired an explicit respawn`);
  return {
    events: allEvents.length,
    lapCompletions: allEvents.filter(e => e.type === 'LapCompleted').length,
    explicitRespawns: allEvents.filter(e => e.type === 'Respawned' && !e.automatic).length,
    automaticRespawns: allEvents.filter(e => e.type === 'Respawned' && e.automatic).length
  };
}
