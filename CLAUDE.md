# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A browser-based, dependency-free (aside from three.js via CDN) racing track editor and driving game, built as plain HTML/JS with no build step. There is no root `package.json`, no bundler, no test runner for the top-level app — files are opened/served as static assets.

- `track.html` — the driving game. Loads three.js (CDN), `track-core.js`, `js/track-game.js`.
- `editor.html` — the 2D/elevation track editor UI. Loads `track-core.js`, `js/editor.js`.
- `index.html` — unrelated scratch demo (spinning cube in three.js), not part of the track app.
- `track-core.js` — shared track math, used by both the game and the editor (see architecture below).
- `js/track-game.js` — three.js scene, track mesh generation/rendering, input, the animate loop. The physics was extracted into `js/track-physics.js` (below); this module builds the THREE meshes, owns a `Simulation`, and drives it.
- `js/track-physics.js` — the THREE-free physics core (a `Simulation` class + pure helpers + centralized constants), extracted verbatim from `track-game.js` so it runs headless and serves as the reference oracle for the C++ port. Uses `js/vec3.js` instead of `THREE.Vector3`. See "Physics core & C++ port".
- `js/vec3.js` — a hand-rolled `Vec3`, a behavioural mirror of `THREE.Vector3` **as shipped in three.js r128** (the exact CDN build): same op order, same edge cases (zero-length `normalize()`→zero, r128's inverse-quaternion `applyQuaternion`). Do not "modernize" it — parity depends on the op order.
- `js/track-bake.js` — THREE-free baking of a normalized track into the world-space physics data a `Simulation` consumes (Vec3 centerline frames + `connectedEndpointIds` + `trackFloorY`). A faithful extraction of `buildTrack()`'s physics half, so a track baked here matches the game's inline bake; the game additionally builds THREE meshes on top.
- `js/editor.js` — editor state, undo/redo, canvas rendering/interaction for authoring tracks.
- `js/track-mesh.js` — shared mesh-region math (see below). The mesh-world counterpart to `track-core.js`, split out because it depends on geometry-js while `track-core.js` stays dependency-free.
- `js/ship-grid.js` — pure, dependency-free two-column runtime grid layout (slot ordering, spacing, stagger and narrow-road compression), unit-tested without a browser.
- `cpp/` — the native C++ engine (CMake/MSVC) that ports the physics core, with a hand-rolled parity replayer. See "Physics core & C++ port".
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. It's linked into the root project as a local npm dependency (root `package.json` -> `"@willpower/geometry": "file:ext/geoemetry-js"`, installed via `npm install`, resolved as a symlink at `node_modules/@willpower/geometry`) so track code can `import` it as `@willpower/geometry`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and its own codebase map.

## Running / testing

No build step: open `track.html` or `editor.html` directly, or serve the repo root statically. Run `npm install` once (after `git submodule update --init --recursive`) to link the `@willpower/geometry` local dependency.

- `npm test` — Node's built-in runner over `test/*.test.js` (pure logic: mesh geometry, rail collision, schema round trips). Fast, no browser.
- `node tools/browser-smoke.mjs` — drives the real pages in headless Chromium. Catches ESM/import-map breakage, runtime errors and physics regressions the unit tests can't see. Needs `npm install --no-save playwright && npx playwright install chromium`. Deliberately outside `test/` so `node --test` doesn't try to run it.
- `npm run gen-traces` — regenerate the committed golden parity traces in `test/traces/` (deliberate, reviewable; run only when the physics is intentionally changed).
- `npm run parity` — the top-level cross-check: JS↔JS trace replay plus the C++ per-step replayer (if `cpp/build/parity` has been built).

For the `geoemetry-js` submodule, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui run dev`) — see `ext/geoemetry-js/README.md`.

**The `.js` files are ES modules** (root `package.json` has `"type": "module"`), except `track-core.js`, which is deliberately a classic browser script — an IIFE assigning `window.TrackCore`. Tests load it by evaluating its source with a stand-in `window`, not by importing it. `track-physics.js`/`track-bake.js` read `TrackCore` off the global lazily (the same contract), so Node harnesses install `globalThis.TrackCore` before running physics.

## Physics core & C++ port

The physics is being ported to a native C++ engine (Windows/MSVC); JS is the reference oracle during the transition. See `CPP_PORT_PLAN.md` for the full plan and rationale. Status: **milestones 0–3 done** — JS extraction + C++ kinematics/guard-rail corridor (M0–1), zone boost + checkpoint/lap + respawn-recovery effects (M2), and the bounded-trajectory smoke check + tolerance lock (M3). Per-step parity holds at 1 ULP (worst combined ratio 7.3e-5, gate 1e-3) over the full 4000-step corpus; the free-running trajectory tracks JS within the documented growing envelope on every trace.

- **JS side.** `js/track-physics.js` is a *literal* transliteration of the physics that used to live in `track-game.js` — every `THREE.Vector3` became `Vec3` (`js/vec3.js`), which mirrors r128's op order exactly so the shipping game's behaviour did not shift (guarded by the browser-smoke "mesh-free track still drives normally" check). Stateful physics is the `Simulation` class; game-only trigger side effects (console log, player checkpoint flash) are injected as hooks.
- **Golden traces.** `test/parity/` generates traces from deterministic **mesh-free** tracks driven by a seeded "noisy autopilot"; each step records the control input and the full resulting ship state. The trace serializes the already-**baked** corridor (not raw track JSON), so both engines replay against byte-identical frames — baking is removed as a parity variable and the C++ `TrackCore` port shrinks to the runtime cross-section math. `test/parity.test.js` proves the trace replays **bit-exact** in JS (determinism + lossless serialization) before any C++ runs. Traces are committed fixtures in `test/traces/`.
- **C++ side (`cpp/`).** Header-only `Vec3`/`TrackCore`/`Track`/`Ship`/`Simulation` (a 1:1 mirror; mesh-region physics is out of scope, so the mesh branches of the step are omitted as provably-dead on the mesh-free corpus). `tests/parity_main.cpp` is the hand-rolled replayer/comparator (mixed abs+rel tolerance, worst-offender + ULP reporting); `third_party/nlohmann/json.hpp` is vendored. Build + test:
  ```
  cmake -S cpp -B cpp/build -G Ninja && cmake --build cpp/build
  ctest --test-dir cpp/build --output-on-failure
  ```
  (Needs a Developer environment — run after `vcvars64.bat` so CMake finds `cl`. The repo's VS install bundles CMake + Ninja.)

## Editor conventions (`js/editor.js`)

- Editor state lives in a single mutable `track` object (`TrackCore.cloneTrack(TrackCore.STARTER_TRACK)` initially).
- Undo/redo uses whole-track deep-clone snapshots (`undoStack`/`redoStack`, capped at `MAX_HISTORY`), not diffs. Every discrete mutation calls `pushUndo()` once *before* mutating, capturing pre-edit state. Continuous gestures (dragging a point, typing in a field) call `pushUndo()` once at gesture start, not per-tick, so one drag = one undo step — see `dragMutated` for how point-selection-without-dragging avoids recording a no-op step.
- Mesh regions: the JSON in `track` is authoritative (it is what undo snapshots and export read); `meshCache` holds a live geometry-js `Mesh` per asset purely to avoid reparsing. Rail edits mutate the live mesh and then `writeBackAsset()` immediately re-serializes it, so the two never drift. Undo/redo drops the cache entirely, since restored assets may differ.
- Dragging a mesh region in Edit mode moves it; shift+drag rotates it about its own placement origin instead (`dragging === 'meshRotate'`). The rotate branch records the offset between the drag start's angle-from-origin and the placement's current `rotation` at mousedown, then keeps applying that offset as the mouse moves, so the shape doesn't jump to face the cursor the instant the drag begins. Angle convention matches `TrackMesh.localToWorld`: `atan2(dz, dx)` from the placement's `(x, z)`, in degrees.
- The add-point/add-mesh popup (`#addPointMenu`) suppresses its own `contextmenu` event, separately from `topCanvas`'s. Right-click's `contextmenu` fires *after* mousedown, by which point the popup mousedown already opened is the frontmost element under the cursor -- so the browser hit-tests the popup, not the canvas beneath it, and `topCanvas`'s own handler never runs. Skipping this lets the native OS/browser context menu render on top of the custom one.
- The mode dropdown is `edit | create | rails`, with `E`/`C`/`R` shortcuts. All mode changes go through `setEditMode()` — it clears the abandoned create draft, drops the rail pick when leaving Rails mode, and syncs the dropdown, so the keys and the dropdown can't drift. Rails mode is modal on purpose: only mesh edges are pickable, so flagging a rail can't be confused with selecting anything else. Mesh regions are hit-tested *last* in edit mode, after every path handle, so a large region never steals a click from a control point drawn on top of it.
- The top-down grid has a visible checkbox and `G` shortcut. Hiding it temporarily disables both the grid-size control and snapping; the Snap checkbox retains its preference so showing the grid restores the prior snap setting. `snapWorldXZ()` also checks visibility directly, so disabled UI cannot leave hidden-grid snapping active.
- Both modules expose a read-only `window.__editor` / `window.__game` handle for console debugging and the browser smoke tests, since ES modules leak nothing to the page.

## Game conventions (`js/track-game.js`)

- `buildTrack()` (re)generates the entire three.js scene/mesh from the current path list — importing new track JSON at runtime just calls it again rather than patching state incrementally.
- Track geometry/physics constants of note: `COLLISION_WALL_MARGIN` (defined in `track-core.js`). The physics centerline sample count is **not** a fixed `N_DEFAULT` — it scales per path with the track's driven length (`TrackCore.adaptiveSampleCount`, holding ~6 m spacing); `N_DEFAULT` is now just the floor and the USD/`track.samples` default. Cross-section width sampling is separately adaptive, not a fixed segment count — see `TrackCore.crossSectionBreakpoints`.
- **1 world unit = 1 metre** (see `CONTEXT.md`). `physics.maxSpeed` is 140 (= 140 m/s, 504 km/h); the HUD is a straight `m/s × 3.6`. Authored tracks run 7–10 km; the built-in `DEFAULT_TRACK` and the editor's `STARTER_TRACK` (a calibrated flat 8 km circle) live in that regime.
