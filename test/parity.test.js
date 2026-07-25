/* JS<->JS parity self-check: proves the golden-trace pipeline is a faithful,
 * lossless oracle BEFORE any C++ exists.
 *
 *  - Per-step replay: load each step's input state (the prior step's `after`, or
 *    initialState), run exactly one stepPhysics, and require the result to equal
 *    the recorded `after` — bit-exact through the JSON boundary (JSON.stringify of
 *    two identical doubles is identical). This proves determinism AND that the
 *    full physics state serializes losslessly (an omitted field would desync).
 *  - Legacy baked-world fixtures isolate runtime math; M6 raw-track fixtures
 *    independently normalize/bake current-schema source in each engine and also
 *    compare surface ownership, rail-hit and respawn outcomes exactly.
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { installTrackCore } from './parity/loadcore.js';

installTrackCore();
const { buildTrace } = await import('./parity/trace.js');
const { loadWorldIntoSim, deserializeShip, serializeShip } = await import('./parity/state.js');
const { Simulation } = await import('../js/track-physics.js');
const { tracks } = await import('./parity/tracks.js');
const { rawScenarios, buildRawTrace, buildSimFor, surfaceLabel, validateRawActivity } = await import('./parity/raw-traces.js');
const {
  rawSessionInitScenarios, buildRawSessionInitTrace,
  rawSessionStepScenarios, buildRawSessionStepTrace, validateRawSessionActivity
} = await import('./parity/raw-session-traces.js');
const { buildRoster, Session } = await import('../js/track-session.js');

// Replay a trace per-step; returns { steps, airborneSteps, firstMismatch|null }.
function replayPerStep(trace) {
  const sim = new Simulation({ now: () => 0 });
  loadWorldIntoSim(sim, trace.world);
  let before = trace.initialState;
  let airborneSteps = 0;
  for (let i = 0; i < trace.steps.length; i++) {
    const ship = deserializeShip(before);
    const c = trace.steps[i].control;
    sim.stepPhysics(ship, c.dt, c.throttle, c.brake, c.steer);
    const got = serializeShip(ship);
    const exp = trace.steps[i].after;
    if (JSON.stringify(got) !== JSON.stringify(exp)) {
      // Narrow to the first differing physics field for a useful message.
      const gp = got.physics, ep = exp.physics;
      let field = '(state)';
      for (const k of Object.keys(ep)) {
        if (JSON.stringify(gp[k]) !== JSON.stringify(ep[k])) { field = k; break; }
      }
      return { firstMismatch: { step: i, field, got: got.physics[field] ?? got, exp: exp.physics[field] ?? exp } };
    }
    if (exp.physics.airborne) airborneSteps++;
    before = trace.steps[i].after;
  }
  return { steps: trace.steps.length, airborneSteps, firstMismatch: null };
}

function replayRawPerStep(trace) {
  const track = globalThis.TrackCore.parseTrack(JSON.stringify(trace.sourceTrack));
  const sim = buildSimFor(track);
  let before = trace.initialState;
  for (let i = 0; i < trace.steps.length; ++i) {
    const ship = deserializeShip(before);
    const c = trace.steps[i].control;
    const result = sim.stepPhysics(ship, c.dt, c.throttle, c.brake, c.steer);
    const got = serializeShip(ship);
    const expected = trace.steps[i];
    if (JSON.stringify(got) !== JSON.stringify(expected.after))
      return { firstMismatch: { step: i, field: 'state' } };
    const outcome = { surface: surfaceLabel(sim, ship), railHit: !!result.railHit, respawned: !!result.respawned };
    if (JSON.stringify(outcome) !== JSON.stringify(expected.outcome))
      return { firstMismatch: { step: i, field: 'outcome', got: outcome, exp: expected.outcome } };
    before = expected.after;
  }
  return { steps: trace.steps.length, firstMismatch: null };
}

for (const { name, track, steps, seed } of tracks()) {
  test(`per-step replay is bit-exact: ${name}`, () => {
    const trace = buildTrace(track, { name, steps, seed });
    const r = replayPerStep(trace);
    assert.equal(r.firstMismatch, null,
      r.firstMismatch && `step ${r.firstMismatch.step} field ${r.firstMismatch.field}: ${JSON.stringify(r.firstMismatch.got)} != ${JSON.stringify(r.firstMismatch.exp)}`);
    assert.equal(r.steps, steps);
  });
}

test('open-curve trace actually exercises the airborne path', () => {
  const t = tracks().find(x => x.name === 'open-curve');
  const trace = buildTrace(t.track, { name: t.name, steps: t.steps, seed: t.seed });
  const airborne = trace.steps.filter(s => s.after.physics.airborne).length;
  assert.ok(airborne > 0, 'ship should leave the open end and go airborne at least once');
});

for (const scenario of rawScenarios()) {
  test(`raw-track per-step replay is bit-exact: ${scenario.name}`, () => {
    const trace = buildRawTrace(scenario.track, scenario);
    validateRawActivity(trace, scenario.require);
    const replay = replayRawPerStep(trace);
    assert.equal(replay.firstMismatch, null, replay.firstMismatch && JSON.stringify(replay.firstMismatch));
  });
}

test('committed fixtures in test/traces/ replay bit-exact', () => {
  const manifestUrl = new URL('./traces/manifest.json', import.meta.url);
  if (!existsSync(manifestUrl)) {
    // Fixtures are optional for the pure npm-test flow; generate with
    // `npm run gen-traces`.
    return;
  }
  const manifest = JSON.parse(readFileSync(manifestUrl, 'utf8'));
  assert.ok(manifest.length > 0);
  for (const entry of manifest) {
    const trace = JSON.parse(readFileSync(new URL(`./traces/${entry.file}`, import.meta.url), 'utf8'));
    const r = replayPerStep(trace);
    assert.equal(r.firstMismatch, null,
      r.firstMismatch && `${entry.file} step ${r.firstMismatch.step} field ${r.firstMismatch.field}`);
    assert.equal(r.steps, entry.steps);
  }
});

test('committed raw-track fixtures replay bit-exact from source schema', () => {
  const directory = new URL('./traces/raw/', import.meta.url);
  const manifest = JSON.parse(readFileSync(new URL('manifest.json', directory), 'utf8'));
  assert.ok(manifest.length > 0);
  for (const entry of manifest) {
    const trace = JSON.parse(readFileSync(new URL(entry.file, directory), 'utf8'));
    const replay = replayRawPerStep(trace);
    assert.equal(replay.firstMismatch, null,
      replay.firstMismatch && `${entry.file} step ${replay.firstMismatch.step} field ${replay.firstMismatch.field}`);
    assert.equal(replay.steps, entry.steps);
  }
});

/* --- raw-session (NATIVE_GAME_RUNTIME_PLAN.md) ----------------------------
 * Tier A: rebuilding the roster from `sourceTrack` alone (no serialized
 * ship/roster input) must reproduce the recorded roster bit-exactly — this
 * is the self-check half of proving independent ship/session initialization.
 * Tier B: replaying one recorded frame from its "before" roster + session
 * time snapshot must reproduce the recorded "after" roster and event list
 * exactly, the same bit-exact-replay-from-recorded-state technique the
 * raw-track layer already uses.
 */
function replaySessionInit(trace) {
  const track = globalThis.TrackCore.parseTrack(JSON.stringify(trace.sourceTrack));
  const sim = buildSimFor(track);
  const roster = buildRoster(sim, track, trace.meta.shipCount, 0).map(serializeShip);
  return JSON.stringify(roster) === JSON.stringify(trace.roster) ? null : { got: roster, exp: trace.roster };
}

function replaySessionFrame(trace, step) {
  const track = globalThis.TrackCore.parseTrack(JSON.stringify(trace.sourceTrack));
  const sim = buildSimFor(track);
  const ships = step.before.roster.map(deserializeShip);
  const session = new Session(sim, ships);
  session.sessionTime = step.before.sessionTime;
  session.step(step.intents, step.dt);
  const gotAfter = { roster: ships.map(serializeShip), sessionTime: session.sessionTime };
  const gotEvents = session.events.map(e => ({ ...e }));
  if (JSON.stringify(gotAfter) !== JSON.stringify(step.after)) return { field: 'after', got: gotAfter, exp: step.after };
  if (JSON.stringify(gotEvents) !== JSON.stringify(step.events)) return { field: 'events', got: gotEvents, exp: step.events };
  return null;
}

for (const scenario of rawSessionInitScenarios()) {
  test(`raw-session initial roster is bit-exact: ${scenario.name}`, () => {
    const trace = buildRawSessionInitTrace(scenario.track, scenario);
    const mismatch = replaySessionInit(trace);
    assert.equal(mismatch, null, mismatch && JSON.stringify(mismatch));
  });
}

for (const scenario of rawSessionStepScenarios()) {
  test(`raw-session frame replay is bit-exact: ${scenario.name}`, () => {
    const trace = buildRawSessionStepTrace(scenario.track, scenario);
    validateRawSessionActivity(trace, scenario.require);
    for (let i = 0; i < trace.steps.length; i++) {
      const mismatch = replaySessionFrame(trace, trace.steps[i]);
      assert.equal(mismatch, null, mismatch && `frame ${i}: ${JSON.stringify(mismatch)}`);
    }
  });
}

test('committed raw-session init fixtures replay bit-exact from source schema', () => {
  const directory = new URL('./traces/raw-session/init/', import.meta.url);
  const manifest = JSON.parse(readFileSync(new URL('manifest.json', directory), 'utf8'));
  assert.ok(manifest.length > 0);
  for (const entry of manifest) {
    const trace = JSON.parse(readFileSync(new URL(entry.file, directory), 'utf8'));
    const mismatch = replaySessionInit(trace);
    assert.equal(mismatch, null, mismatch && `${entry.file}: ${JSON.stringify(mismatch)}`);
  }
});

test('committed raw-session step fixtures replay bit-exact from source schema', () => {
  const directory = new URL('./traces/raw-session/steps/', import.meta.url);
  const manifest = JSON.parse(readFileSync(new URL('manifest.json', directory), 'utf8'));
  assert.ok(manifest.length > 0);
  for (const entry of manifest) {
    const trace = JSON.parse(readFileSync(new URL(entry.file, directory), 'utf8'));
    for (let i = 0; i < trace.steps.length; i++) {
      const mismatch = replaySessionFrame(trace, trace.steps[i]);
      assert.equal(mismatch, null, mismatch && `${entry.file} frame ${i}: ${JSON.stringify(mismatch)}`);
    }
    assert.equal(trace.steps.length, entry.steps);
  }
});
