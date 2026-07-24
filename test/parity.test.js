/* JS<->JS parity self-check: proves the golden-trace pipeline is a faithful,
 * lossless oracle BEFORE any C++ exists.
 *
 *  - Per-step replay: load each step's input state (the prior step's `after`, or
 *    initialState), run exactly one stepPhysics, and require the result to equal
 *    the recorded `after` — bit-exact through the JSON boundary (JSON.stringify of
 *    two identical doubles is identical). This proves determinism AND that the
 *    full physics state serializes losslessly (an omitted field would desync).
 *  - Committed fixtures in test/traces/ round-trip the same way — the exact files
 *    the C++ replayer will read.
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

test('committed fixtures in test/traces/ replay bit-exact', () => {
  const manifestUrl = new URL('./traces/manifest.json', import.meta.url);
  if (!existsSync(manifestUrl)) {
    // Fixtures are optional for the pure npm-test flow; generate with
    // `node test/parity/gen-traces.mjs`.
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
