# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A browser-based racing track editor and driving game built as plain HTML/JS with no application build step, living under `web/` — a sibling of the native C++ implementation under `cpp/`. Three.js is loaded from a CDN; `web/package.json` links the local geometry-js submodule and provides Node test/parity scripts. Pages are still opened or served as static assets. `assets/` (bundled textures) and the `ext/geoemetry-js` submodule stay at the repo root, shared between `web/` and `cpp/` — see both's notes below.

- `web/track.html` — the driving game. Loads three.js (CDN), `track-core.js`, `js/track-game.js`.
- `web/editor.html` — the 2D/elevation track editor UI. Loads `track-core.js`, `js/editor.js`.
- `web/index.html` — unrelated scratch demo (spinning cube in three.js), not part of the track app.
- `web/track-core.js` — shared track math, used by both the game and the editor (see architecture below).
- `web/js/track-game.js` — three.js scene, track mesh generation/rendering, input, the animate loop. The physics was extracted into `js/track-physics.js` (below); this module builds the THREE meshes, owns a `Simulation`, and drives it.
- `web/js/track-physics.js` — the THREE-free physics core (a `Simulation` class + pure helpers + centralized constants), extracted verbatim from `track-game.js` so it runs headless and serves as the reference oracle for the C++ port. Uses `js/vec3.js` instead of `THREE.Vector3`. See "Physics core & C++ port".
- `web/js/vec3.js` — a hand-rolled `Vec3`, a behavioural mirror of `THREE.Vector3` **as shipped in three.js r128** (the exact CDN build): same op order, same edge cases (zero-length `normalize()`→zero, r128's inverse-quaternion `applyQuaternion`). Do not "modernize" it — parity depends on the op order.
- `web/js/track-bake.js` — THREE-free baking of a normalized track into complete world-space physics data: spline frames, mesh regions, zones/triggers, endpoint connectivity, and respawn floor. It is the JS reference oracle for native loading/baking.
- `web/js/track-render-geometry.js` — graphics-API-neutral path, shell, rail, mesh, and zone triangle batches used by tests and mirrored by C++.
- `web/js/editor.js` — editor state, undo/redo, canvas rendering/interaction for authoring tracks.
- `web/js/track-mesh.js` — shared mesh-region math (see below). The mesh-world counterpart to `track-core.js`, split out because it depends on geometry-js while `track-core.js` stays dependency-free.
- `web/js/ship-grid.js` — pure, dependency-free two-column runtime grid layout (slot ordering, spacing, stagger and narrow-road compression), unit-tested without a browser.
- `cpp/` — the native C++ engine (CMake/MSVC) that ports the physics core, with a hand-rolled parity replayer. A sibling of `web/` at the repo root, not nested under it. See "Physics core & C++ port".
- `assets/` — bundled track textures (`assets/track/`, plus a loose `assets/test-1.png` fixture), shared between `web/` and `cpp/editor/` (see `cpp/editor/src/TextureCache.cpp`'s `findAssetsDir()`); stays at the repo root rather than moving under `web/` for that reason. Both editors store bundled `TextureAsset.path` values relative to `web/` (e.g. `"../assets/track/foo.png"`), not relative to the repo root.
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. Stays at the repo root (a sibling of `web/`, not nested under it — `cpp/willpower` also references it for its own vendored copy considerations). It's linked into `web/` as a local npm dependency (`web/package.json` -> `"@willpower/geometry": "file:../ext/geoemetry-js"`, installed via `npm install` from `web/`, resolved as a symlink at `web/node_modules/@willpower/geometry`) so track code can `import` it as `@willpower/geometry`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and its own codebase map.

## Editing

When making changes to C++ code, make sure that you adhere to the .clang-format file in the root, running clang-format afterwards if necessary.  Do not run clang-format on any upstream code.

## Running / testing

No build step: open `web/track.html` or `web/editor.html` directly, or serve the repo root statically and browse to `web/`. Run `npm install` from `web/` once (after `git submodule update --init --recursive`) to link the `@willpower/geometry` local dependency.

All commands below run from `web/` (where `package.json` lives) unless noted otherwise:

- `npm test` — Node's built-in runner over app/submodule logic and bit-exact JS replay of both committed parity layers. It does not regenerate traces or require a browser/C++ toolchain.
- `node tools/browser-smoke.mjs` — drives the real pages in headless Chromium. Catches ESM/import-map breakage, runtime errors and physics regressions the unit tests can't see. Needs `npm install --no-save playwright && npx playwright install chromium`. Deliberately outside `test/` so `node --test` doesn't try to run it. Its static server roots at the actual repo root (not `web/`), since it must also serve `assets/`; pages are requested as `web/editor.html` / `web/track.html`.
- `npm run gen-traces` — regenerate both committed physics parity layers in `test/traces/`: legacy baked-world traces and current-schema raw-track traces. This is deliberate and reviewable; run only when physics, loading/baking, or the corpus intentionally changes.
- `npm run gen-random-mesh-fixtures` — regenerate the deterministic random schema-10 JSON tracks and JS renderer-neutral geometry oracle under `test/fixtures/random-track-mesh/`.
- `npm run parity` — the top-level cross-check: JS↔JS replay plus C++ baked-world, raw-track, and seeded random JSON-to-geometry parity (when `cpp/build` has been built). `web/tools/parity.mjs` looks for `cpp/build` two levels up from itself (the actual repo root), since `cpp/` is a sibling of `web/`, not nested under it.
- Native build/test — **from the repo root** (not `web/`), from an MSVC Developer prompt: `cmake -S cpp -B cpp/build`, `cmake --build cpp/build --config Release`, then `ctest --test-dir cpp/build -C Release --output-on-failure`. See `cpp/README.md`. `cpp/core/CMakeLists.txt`'s `add_test()` commands read fixtures/traces from `web/test/...`.

For the `geoemetry-js` submodule, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui run dev`) — see `ext/geoemetry-js/README.md`.

**The `.js` files are ES modules** (`web/package.json` has `"type": "module"`), except `track-core.js`, which is deliberately a classic browser script — an IIFE assigning `window.TrackCore`. Tests load it by evaluating its source with a stand-in `window`, not by importing it. `track-physics.js`/`track-bake.js` read `TrackCore` off the global lazily (the same contract), so Node harnesses install `globalThis.TrackCore` before running physics.

## Physics core & C++ port

The full current-schema track runtime is now ported to C++ (Windows/MSVC); JS remains the measured reference oracle. `CPP_PORT_PLAN.md` documents the original corridor core and `MESH_CPP_PORT_PLAN.md` documents the completed full-track follow-on. Status: **all M0–M7 milestones complete**. C++ independently loads strict schema-10 JSON, bakes rational spline paths, compiles Willpower mesh topology, emits renderer-neutral geometry, and runs corridor/mesh physics, zones, checkpoints, landing, and recovery.

- **JS reference.** `web/js/track-physics.js`, `web/js/track-bake.js`, `web/js/track-render-geometry.js`, and `web/js/track-mesh.js` are the headless oracle. `Vec3` still mirrors three.js r128 operation order exactly; do not modernize it casually.
- **C++ core (`cpp/core/`).** Public authored records (`TrackDefinition.hpp`) are separate from compiled runtime records (`Track.hpp`, `TrackMesh.hpp`) and renderer-neutral batches (`TrackGeometry.hpp`). `Track::fromJson()` / `fromFile()` accept schema 10 only, return structured warnings for recoverable mesh failures, and perform all baking without JavaScript. Willpower.Geometry is used at load time for topology and triangulation; simulation consumes retained double-precision loops, rails, and bounds. `nlohmann/json` remains a private implementation dependency. `fromJson()`/`fromFile()` also take a `detectSelfIntersections` flag (default `true`) gating an O(N²) crossing-detection pass; the editor's live-preview rebake passes `false` mid-drag and reuses its last result (EDITOR_PARITY_GAPS.md gap 1).
- **Parity layers.** The original four baked-world traces isolate runtime math: 4000 steps, unchanged `atol=rtol=1e-12`, ratio gate `1e-3`, observed worst `7.322e-5` (1 ULP). Twelve raw-track traces force independent loading/baking and cover 1116 steps of spline and mesh behavior: `atol=rtol=1e-12`, ratio gate `0.1`, observed worst `0.01129` (`1.42e-14`); surface IDs, rail hits, airborne/boost/trigger/checkpoint/lap/respawn state match exactly. Both layers include bounded free runs. See `web/test/traces/raw/README.md`.
- **Native tests.** `track_tests` covers strict loading, JS-generated normalization/geometry oracles, mesh topology and renderer attributes, and direct simulation scenarios. `parity` and `raw_parity` preserve the physics gates. `random_geometry_parity` independently bakes five deterministic randomly-generated schema-10 JSON tracks in C++ and compares batch metadata, topology counts, bounds, UVs, area centroids, and oriented area against the JS geometry oracle.

## Editor conventions (`web/js/editor.js`)

- Editor state lives in a single mutable `track` object (`TrackCore.cloneTrack(TrackCore.STARTER_TRACK)` initially).
- Undo/redo uses whole-track deep-clone snapshots (`undoStack`/`redoStack`, capped at `MAX_HISTORY`), not diffs. Every discrete mutation calls `pushUndo()` once *before* mutating, capturing pre-edit state. Continuous gestures (dragging a point, typing in a field) call `pushUndo()` once at gesture start, not per-tick, so one drag = one undo step — see `dragMutated` for how point-selection-without-dragging avoids recording a no-op step.
- Mesh regions: the JSON in `track` is authoritative (it is what undo snapshots and export read); `meshCache` holds a live geometry-js `Mesh` per asset purely to avoid reparsing. Rail edits mutate the live mesh and then `writeBackAsset()` immediately re-serializes it, so the two never drift. Undo/redo drops the cache entirely, since restored assets may differ.
- Dragging a mesh region in Edit mode moves it; shift+drag rotates it about its own placement origin instead (`dragging === 'meshRotate'`). The rotate branch records the offset between the drag start's angle-from-origin and the placement's current `rotation` at mousedown, then keeps applying that offset as the mouse moves, so the shape doesn't jump to face the cursor the instant the drag begins. Angle convention matches `TrackMesh.localToWorld`: `atan2(dz, dx)` from the placement's `(x, z)`, in degrees.
- The add-point/add-mesh popup (`#addPointMenu`) suppresses its own `contextmenu` event, separately from `topCanvas`'s. Right-click's `contextmenu` fires *after* mousedown, by which point the popup mousedown already opened is the frontmost element under the cursor -- so the browser hit-tests the popup, not the canvas beneath it, and `topCanvas`'s own handler never runs. Skipping this lets the native OS/browser context menu render on top of the custom one.
- The mode dropdown is `edit | create | rails`, with `E`/`C`/`R` shortcuts. All mode changes go through `setEditMode()` — it clears the abandoned create draft, drops the rail pick when leaving Rails mode, and syncs the dropdown, so the keys and the dropdown can't drift. Rails mode is modal on purpose: only mesh edges are pickable, so flagging a rail can't be confused with selecting anything else. Mesh regions are hit-tested *last* in edit mode, after every path handle, so a large region never steals a click from a control point drawn on top of it.
- The top-down grid has a visible checkbox and `G` shortcut. Hiding it temporarily disables both the grid-size control and snapping; the Snap checkbox retains its preference so showing the grid restores the prior snap setting. `snapWorldXZ()` also checks visibility directly, so disabled UI cannot leave hidden-grid snapping active.
- Both modules expose a read-only `window.__editor` / `window.__game` handle for console debugging and the browser smoke tests, since ES modules leak nothing to the page.
- Bundled texture assets are stored as `"../assets/track/..."` (`BUNDLED_TEXTURE_ROOT`), not `"assets/track/..."` — a page-relative path from `editor.html`'s/`track.html`'s location in `web/` up to the repo-root `assets/` directory. `cpp/editor/src/TextureCache.cpp`'s resolution logic mirrors this convention (resolves relative to `<repo root>/web`, not the repo root itself) so a track authored in either editor references the same bundled texture with the same on-disk path string.

## Game conventions (`web/js/track-game.js`)

- `buildTrack()` (re)generates the entire three.js scene/mesh from the current path list — importing new track JSON at runtime just calls it again rather than patching state incrementally.
- Track geometry/physics constants of note: `COLLISION_WALL_MARGIN` (defined in `track-core.js`). The physics centerline sample count is **not** a fixed `N_DEFAULT` — it scales per path with the track's driven length (`TrackCore.adaptiveSampleCount`, holding ~6 m spacing); `N_DEFAULT` is now just the floor and the USD/`track.samples` default. Cross-section width sampling is separately adaptive, not a fixed segment count — see `TrackCore.crossSectionBreakpoints`.
- **1 world unit = 1 metre** (see `CONTEXT.md`). `physics.maxSpeed` is 140 (= 140 m/s, 504 km/h); the HUD is a straight `m/s × 3.6`. Authored tracks run 7–10 km; the built-in `DEFAULT_TRACK` and the editor's `STARTER_TRACK` (a calibrated flat 8 km circle) live in that regime.
