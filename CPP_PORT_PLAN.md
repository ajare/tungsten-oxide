# C++ Port Plan — Track Physics Core

Status: **agreed, not yet started.** This is the implementation reference produced from a
grilling/planning session. Every decision below was made deliberately; the "Why" notes exist so a
future implementer (human or agent) doesn't silently re-litigate or undo a choice.

---

## 1. Goal & source-of-truth trajectory

- **End state:** C++ becomes the authoritative native game engine (Windows / MSVC). JS is eventually
  retired or demoted to editor-only.
- **Right now:** JS is the reference oracle. Parity testing is **transitional scaffolding** to prove the
  port is faithful — a **literal-first transliteration**, not an idiomatic rewrite. Once parity holds,
  C++ is free to diverge and be made idiomatic; the committed golden traces become the durable
  regression oracle even after JS is gone.

## 2. Scope

**In (first library):** spline-corridor physics —
`sampleTrack`, `stepPhysics`, `projectToSurface`, `corridorContains`, `tangentize`,
`beginAirborne`/`landOnSurface`, guard-rail corridor collision, plus the `Zone`/`Trigger` classes
(present from day one; their *effects* land in milestone 2). All of this rides on `track-core.js`
math, which is already dependency-free.

**Out (deferred):** mesh-region physics — `slideAlongRails`, swept polygon collision,
`surfaceOwnerAt` arbitration. It drags in geometry-js (`@willpower/geometry`) triangulation and
roughly doubles the surface area. Deferred to a self-contained follow-on, exactly as it already is in
the JS. Random parity tracks are configured to emit **zero** mesh sections.

**Also out:** all rendering (THREE.js scene/mesh/material/camera).

## 3. Architecture

- Port `track-core.js` as a **faithful stateless `TrackCore` free-function namespace** — a 1:1 mirror.
  This is where the spline evaluation and transcendental-heavy math lives; keeping it a stateless
  mirror is the single biggest lever for per-step parity holding.
- Classes wrap the **stateful** parts (this is the "classes for each" the port asked for):
  - **`Track`** — parsed track data + baked centerline/edges (produced by calling `TrackCore`).
  - **`Ship`** — the physics-state struct with `stepPhysics` as a method.
  - **`Zone`** / **`Trigger`** — data + their detection predicates.
  - **`Simulation`** — owns the `Track`, the ship roster, zones, triggers; runs the loop.
- **`Vec3` is hand-rolled (double)**, mirroring `THREE.Vector3`'s method set **and** its edge cases:
  zero-length `normalize()` → zero vector (not NaN), the same `applyAxisAngle` quaternion path, and
  the **same operation order** (float add isn't associative). No glm, no Eigen.
  - Why: removes an entire class of "why does glm's normalize/rotate disagree" parity hunts, keeps the
    library self-contained (repo ethos), and makes the port a near-line-for-line transliteration.

## 4. Numerics & parity method

- **Tolerance-based, not bit-exact.** We are **not** vendoring fdlibm; we call `std::` math and accept
  1–2 ULP transcendental drift (`sin`/`cos`/`atan2`/`pow`). `sqrt` and +/-/* are IEEE correctly-rounded
  in both V8 and MSVC, so those already match.
  - Near-free hardening we still apply: `/fp:precise` (MSVC default) to stop FMA contraction fusing
    `a*b+c` that JS never used — keeps per-step error small and the tolerance easy to pick.
  - (Rejected: vendoring fdlibm for bit-exact parity. Stronger guarantee but more upfront work; the
    port owner chose the simpler tolerance path. If ever revisited, it would make per-step parity
    bit-exact and make the C++ engine deterministic across shipping platforms.)
- **Golden-trace architecture.** JS is the generator. For each random track it writes:

  ```json
  { "track": {…}, "initialState": {…}, "steps": [ { "input": {…}, "outputState": {…} }, … ] }
  ```

  - Doubles serialize as JS shortest-round-trip decimals; C++ parses with **correctly-rounded `strtod`**
    (via nlohmann/json) → the identical double. Lossless.
  - **Full physics state must serialize losslessly** — not just position/velocity/orientation, but the
    detection bookkeeping: `zone-inside` map, per-trigger `armed`/`prevTriggerPos`, `boostActive` /
    `boostReleasing` / `boost*` timers, `effectiveMaxSpeed` state. **Anything omitted is a silent
    parity gap.**
  - `dt` and input are baked per-step into the trace and replayed identically by both engines
    (`stepPhysics(ship, dt, throttle, brake, steer)` already takes explicit `dt`; substepping lives in
    the animate loop and is out of scope). No wall-clock, no `THREE.Clock`.
- **Per-step parity is the workhorse** (weighted primary). Feed both engines the trace's *input state*,
  run exactly one `stepPhysics`, compare output.
  - Gate: **single mixed absolute+relative tolerance**, one pair for all fields:
    `|a − b| ≤ atol + rtol·|b|`.
  - Calibration: start permissive (`atol = rtol = 1e-9`), **measure** the worst per-step per-field delta
    across all committed traces, then set the gate at a small fixed multiple (~8×) above the observed
    worst. Evidence-based, not guessed. Harness reports the worst-offending field/step (with ULP delta).
  - Escalate to per-field-class tolerances only if one field's natural scale genuinely can't share a
    bound with the others.
- **Bounded-trajectory parity** is a secondary smoke check: C++ feeds its own prior output, runs many
  steps, compares against the JS trajectory over a documented **growing-tolerance** horizon. Chaotic
  divergence is expected and acknowledged — no pretense a long race stays close. Per-step is the real
  gate.

## 5. JS side — phase-0 refactor comes first

- **Extract `js/track-physics.js`** out of `js/track-game.js`: a dependency-free, THREE-free module
  that `track-game.js` then imports. Mirrors the existing `track-core.js` split.
  - Swap the physics code's `THREE.Vector3` / `THREE.MathUtils` for the tiny **JS `Vec3`** (which
    mirrors `THREE.Vector3`'s exact op order). This keeps the extraction **parity-neutral against
    today's game** — the game's own behavior must not shift.
  - Net for this refactor: the browser-smoke playtest (`node tools/browser-smoke.mjs`) **plus** the new
    headless node tests. Note `track-game.js` today has **no** automated physics coverage — this
    extraction retrofits the first-ever headless physics tests before any C++ exists.
- **Track generation is JS-only.** Extract the seeded `generateRandomTrack(complexity, seed, ranges)`
  (mulberry32, only 3 DOM touches — all `localStorage` for ranges) to run headless with a tiny shim.
  Emit tracks as JSON into the trace. **C++ never generates a track.**
- **Ship is driven by a seeded "noisy autopilot"** — a crude JS controller biased toward
  forward + centerline-following with seeded perturbations, plus scripted excursions that deliberately
  aim off open-curve ends (to exercise airborne launch/landing) and cross boost pads / checkpoints.
  JS-only throwaway scaffolding; inputs are baked into the trace so C++ just replays.
  - Why not pure-random inputs: a random-steering ship just grinds into the first wall and tests almost
    nothing.

## 6. Toolchain, dependencies, layout, workflow

- **Compiler/build:** MSVC (Visual Studio Build Tools) driven through **CMake** (`CMakeLists.txt` is the
  "CMake-compatible" build the request asked for). Nothing is currently installed — MSVC + CMake must
  be installed as a first step. Pinning parity to MSVC's FP behavior now avoids re-validating later.
- **Dependencies:**
  - Vendor **`nlohmann/json`** (single header, committed under `cpp/third_party/nlohmann/`). Its
    correctly-rounded double parsing is exactly the fiddly, silently-wrong-if-hand-rolled code we don't
    want to write.
  - **Hand-roll** the ~30-line assert/report test harness (`check_close(a, b, tol)` + pass/fail counter
    + worst-offender reporting). Keeps third-party surface to the one piece that's genuinely hard.
  - (Rejected: doctest/Catch2/GoogleTest — unnecessary given how small the harness is.)
- **Layout:**
  ```
  cpp/
    include/            # Vec3, TrackCore, Track, Ship, Zone, Trigger, Simulation headers
    src/
    tests/              # C++ parity replayer/comparator + hand-rolled harness
    third_party/nlohmann/
    CMakeLists.txt
  test/
    traces/             # committed golden .json fixtures, read by BOTH sides
    parity/             # JS trace-gen + noisy autopilot + JS<->JS parity self-check
  ```
- **Traces are committed** as fixtures (self-contained C++ suite, reviewable diffs, durable oracle after
  JS retires) + a `regen-traces` script / CMake target for deliberate, reviewable regeneration when
  physics is intentionally changed.
- **Suites stay separate:**
  - `npm test` stays fast/pure (Node) and **gains** the JS↔JS parity + trace generation. It does **not**
    hard-depend on CMake/MSVC — contributors without the C++ toolchain can still run it.
  - C++ has its own `cmake --build … && ctest` flow.
  - A thin top-level convenience script (e.g. `tools/parity.mjs` or an npm script) runs both end-to-end
    for the full cross-check.

## 7. Milestones

0. **Phase-0 JS extraction.** Create `js/track-physics.js` + JS `Vec3`; rewire `track-game.js` to import
   it. Prove JS↔JS parity across the refactor (browser-smoke + new node tests). No C++ yet.
1. **C++ core, kinematics-first.** `Vec3`, `TrackCore` namespace, `Track`, `Ship`, `stepPhysics`.
   Per-step parity on: position, velocity, orientation basis, speed, airborne/landing, **and guard-rail
   corridor collision**. `Zone`/`Trigger` are built and serialized but their effects are not yet
   asserted.
2. **Zones + triggers effects.** Layer boost speed-clamp (`Zone`) and the checkpoint state machine +
   recovery pose (`Trigger`) into the trace and the assertions. Full `Simulation.step` per-step parity.
3. **Trajectory + tolerance lock.** Bounded-trajectory smoke test with the growing-tolerance envelope;
   measure worst per-step drift across all traces and lock the calibrated tolerance.

## 8. Risks / things to watch

- **Phase-0 is parity-sensitive** and, until the new node tests exist, is covered only by the
  browser-smoke playtest. The JS `Vec3` must transliterate `THREE.Vector3` **exactly** (op order + edge
  cases) or the shipping game's own behavior shifts.
- **Scattered constants.** `maxSpeed`, `accel`, `gravity`, `UP`, `COLLISION_WALL_MARGIN`,
  `SEGMENT_ALONG_TOL`, `CORRIDOR_ALONG_TOL`, boost `ZONE_RELEASE`, `TRIGGER_REARM_MARGIN`, etc. are
  spread across `track-core.js` and `track-game.js`. Centralize them in `track-physics.js` as the single
  source of truth, then transliterate into one C++ header. A drifted constant is an invisible parity
  bug.
- **Chaotic divergence** makes trajectory tests inherently soft; treat per-step as the real gate and
  don't tune tolerances to force a long trajectory to pass.
- **Lossless state coverage.** Every field in `createPhysicsState()` (js/track-game.js) plus the
  per-ship detection maps must appear in the trace. Cross-check against `createPhysicsState` when
  defining the serialization.

---

## Appendix — key source anchors (as of planning)

- `track-core.js` (1874 lines): pure math IIFE, `window.TrackCore`. No THREE, no geometry-js. Port
  target for the `TrackCore` namespace.
- `js/track-game.js` (2259 lines): physics + rendering mixed. Physics functions to extract:
  `shipParamG` (701), `effectiveMaxSpeed` (714), `detectZoneTriggers` (749), `detectTriggers` (944),
  `projectToSurface` (1021), `corridorContains` (1044), `surfaceOwnerAt` (1060), `applyHandling` (1075),
  `slideAlongRails` (1108, **mesh — out of scope**), `curvedSurfaceHeight`/`Frame` (1128/1135),
  `sampleTrack` (1165), `createPhysicsState` (1464), `tangentize` (1548), `beginAirborne` (1567),
  `stepPhysics` (1603). Physics step body uses only `THREE.Vector3` (math) + `THREE.MathUtils`
  (clamp/lerp) — trivially THREE-free after the `Vec3` swap.
- `js/editor.js`: `generateRandomTrack` at ~3848, `mulberry32` at ~3723. Extract headless.
- Tests run under Node's built-in runner (`test/*.test.js`); `node tools/browser-smoke.mjs` is the
  headless-Chromium playtest.
