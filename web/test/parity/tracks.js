/* The parity corpus: deterministic, MESH-FREE tracks driven by the seeded noisy
 * autopilot. `starter-circle` / `open-curve` are the milestone-1 kinematics +
 * guard-rail corridor cases (no zones/triggers); `boost-circuit` (milestone 2)
 * adds a velocityChange boost zone and checkpoint gates so per-step parity covers
 * the zone boost state machine and the checkpoint/lap logic too.
 *
 * `TrackCore.parseTrack` normalizes each (fills widths/handling, migrates schema),
 * the same path the game and editor take. Requires globalThis.TrackCore. */

const TC = () => globalThis.TrackCore;

// The editor's calibrated flat 8 km circle — a long, gently curved closed loop.
export function starterCircle() {
  return TC().cloneTrack(TC().STARTER_TRACK);
}

// An open curve that bends and climbs; the autopilot follows it to the far end
// and launches off, exercising beginAirborne / the airborne branch / the fall to
// trackFloorY and respawn.
export function openCurve() {
  const raw = {
    schemaVersion: 10,
    name: 'parity-open-curve',
    paths: [{
      id: 'p0', closed: false, points: [
        { type: 'width', t: 0, width: 80 },
        { type: 'position', id: 'a', pos: [0, 6, 0] },
        { type: 'position', id: 'b', pos: [0, 6, 140] },
        { type: 'position', id: 'c', pos: [30, 9, 260] },
        { type: 'position', id: 'd', pos: [90, 12, 360] }
      ]
    }],
    start: { path: 0, point: 0, reverse: false }
  };
  return TC().parseTrack(JSON.stringify(raw));
}

// A small flat closed circle (driven length ~900 m) carrying a boost pad and two
// checkpoint gates (a finish just ahead of the start + one intermediate at the
// far side). Small enough that the autopilot enters the boost zone within the
// first steps and laps within the budget, so the trace exercises triggerBoost +
// the full hold/release tick, the checkpoint state machine, and a lap increment.
export function boostCircuit() {
  const R = 150;
  const points = [];
  for (let k = 0; k < 12; k++) {
    const a = (k / 12) * Math.PI * 2;
    points.push({ type: 'position', id: 'q' + k, pos: [R * Math.cos(a), 0, R * Math.sin(a)], weight: 1 });
  }
  points.push({ type: 'width', t: 0, width: 50 }, { type: 'width', t: 0.5, width: 50 });
  const raw = {
    version: 10,
    name: 'parity-boost-circuit',
    paths: [{ id: 'p0', closed: true, points }],
    start: { path: 0, point: 0, reverse: false },
    zones: [
      { id: 'boost1', effect: 'velocityChange', host: { kind: 'path', pathId: 'p0', t: 0.05, lateral: 0 }, width: 30, length: 40, factor: 1.5, duration: 2 }
    ],
    triggers: [
      { id: 'cp-finish', type: 'checkpoint', role: 'finish', host: { kind: 'path', pathId: 'p0', t: 0.015 }, width: 54, height: 15, direction: 'forward' },
      { id: 'cp-mid', type: 'checkpoint', role: 'intermediate', host: { kind: 'path', pathId: 'p0', t: 0.5 }, width: 54, height: 15, direction: 'both' }
    ]
  };
  return TC().parseTrack(JSON.stringify(raw));
}

// A straight, flat OPEN path with a single checkpoint at the midpoint. The
// autopilot crosses the checkpoint (setting lastCheckpoint), drives off the open
// end, goes airborne and falls below trackFloorY — so the trace exercises the
// respawn RECOVERY POSE (placeShipAtPose back to the last checkpoint, its gate
// left disarmed) and the resulting continued-drive parity from the placed state.
export function recoveryRun() {
  const raw = {
    version: 10,
    name: 'parity-recovery-run',
    paths: [{
      id: 'p0', closed: false, points: [
        { type: 'width', t: 0, width: 60 },
        { type: 'position', id: 'a', pos: [0, 6, 0] },
        { type: 'position', id: 'b', pos: [0, 6, 120] },
        { type: 'position', id: 'c', pos: [0, 6, 240] },
        { type: 'position', id: 'd', pos: [0, 6, 360] }
      ]
    }],
    start: { path: 0, point: 0, reverse: false },
    triggers: [
      { id: 'cp-finish', type: 'checkpoint', role: 'finish', host: { kind: 'path', pathId: 'p0', t: 0.5 }, width: 64, height: 15, direction: 'both' }
    ]
  };
  return TC().parseTrack(JSON.stringify(raw));
}

export function tracks() {
  return [
    { name: 'starter-circle', track: starterCircle(), steps: 600, seed: 12345 },
    { name: 'open-curve', track: openCurve(), steps: 900, seed: 777 },
    { name: 'boost-circuit', track: boostCircuit(), steps: 1600, seed: 2024 },
    { name: 'recovery-run', track: recoveryRun(), steps: 900, seed: 4242 }
  ];
}
