# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native C++ (CMake/MSVC) racing track editor and driving game engine, all under `cpp/`. There is no browser/JS implementation in this repo; the physics core, track loading/baking, mesh topology, and renderer-neutral geometry are all C++ from the ground up.

- `cpp/core/` — the track-physics engine: schema-10 JSON loading, spline/mesh baking, `Simulation`/`Ship` physics, renderer-neutral geometry. See "Physics core" below.
- `cpp/editor/` — `track_editor`, the native ImGui/SDL2/OpenGL track editor. See "Editor conventions" below and `EDITOR_CPP_PORT_PLAN.md`.
- `cpp/app/` — `track_runner`, a headless CLI session host.
- `cpp/launcher/`, `cpp/applib/`, `cpp/tungsten-monoxide/`, `cpp/model-tool/` — see `cpp/CMakeLists.txt`'s header comment for the full tree and each subproject's own docs.
- `cpp/willpower/` — the Willpower geometry/math libraries (`Willpower.Common`, `Willpower.Geometry`, ...), used at load time for mesh topology and triangulation.
- `assets/` — bundled track textures (`assets/track/`, plus a loose `assets/test-1.png` fixture), read by `cpp/editor/src/TextureCache.cpp`'s `findAssetsDir()`. Stays at the repo root as a sibling of `cpp/`. Bundled `TextureAsset.path` values are stored relative to the repo root (e.g. `"assets/track/foo.png"`).
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. This is the one JavaScript codebase still in the repo; it is not part of the track app and has no bearing on `cpp/`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and codebase map. `cpp/willpower` references it only for vendored-copy provenance notes, not as a runtime dependency.

## Editing

When making changes to C++ code, make sure that you adhere to the .clang-format file in the root, running clang-format afterwards if necessary.  Do not run clang-format on any upstream code.

## Running / testing

From an MSVC Developer prompt, **from the repo root**:

```
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

See `cpp/README.md` for target-by-target detail. `cpp/core/CMakeLists.txt`'s `add_test()` commands and `cpp/editor/CMakeLists.txt`'s `editor_track_resources` test read golden fixtures/traces from `cpp/test-data/` (a sibling of `core/`/`editor/`, not nested under either) — `traces/` for physics replay, `fixtures/` for loader/mesh/geometry oracles. This corpus is a fixed, committed regression suite; there is no in-repo tool to regenerate it, so treat it as append-only unless you're prepared to hand-author or validate new fixtures directly against the C++ implementation.

For the `geoemetry-js` submodule, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui run dev`) — see `ext/geoemetry-js/README.md`.

## Physics core

C++ independently loads strict schema-10 JSON, bakes rational spline paths, compiles Willpower mesh topology, emits renderer-neutral geometry, and runs corridor/mesh physics, zones, checkpoints, landing, and recovery.

- **`cpp/core/`.** Public authored records (`TrackDefinition.hpp`) are separate from compiled runtime records (`Track.hpp`, `TrackMesh.hpp`) and renderer-neutral batches (`TrackGeometry.hpp`). `Track::fromJson()` / `fromFile()` accept schema 10 only and return structured warnings for recoverable mesh failures. Willpower.Geometry is used at load time for topology and triangulation; simulation consumes retained double-precision loops, rails, and bounds. `nlohmann/json` remains a private implementation dependency. `fromJson()`/`fromFile()` also take a `detectSelfIntersections` flag (default `true`) gating an O(N²) crossing-detection pass; the editor's live-preview rebake passes `false` mid-drag and reuses its last result.
- **Golden fixture corpus (`cpp/test-data/`).** The original four baked-world traces isolate runtime math: 4000 steps, `atol=rtol=1e-12`, ratio gate `1e-3`, observed worst `7.322e-5` (1 ULP). Twelve raw-track traces force independent loading/baking and cover 1116 steps of spline and mesh behavior: `atol=rtol=1e-12`, ratio gate `0.1`, observed worst `0.01129` (`1.42e-14`); surface IDs, rail hits, airborne/boost/trigger/checkpoint/lap/respawn state match exactly. Both layers include bounded free runs. See `cpp/test-data/traces/raw/README.md`.
- **Native tests.** `track_tests` covers strict loading, normalization/geometry, mesh topology and renderer attributes, and direct simulation scenarios. `parity` and `raw_parity` replay the golden traces above and preserve the physics gates. `random_geometry_parity` bakes five deterministic randomly-generated schema-10 JSON tracks in C++ and checks batch metadata, topology counts, bounds, UVs, area centroids, and oriented area against a committed geometry summary.
- `Vec3` has an intentional, non-obvious operation order (e.g. zero-length `normalize()` → zero) inherited from a historical reference implementation this engine was independently verified against during development. Do not "modernize" it — several fixtures in the golden corpus above pin exact bit behavior that depends on that order.
- Track geometry/physics constants of note: `TrackCore::COLLISION_WALL_MARGIN`. The physics centerline sample count is **not** a fixed `TrackCore::N_DEFAULT` — it scales per path with the track's driven length (`TrackBake.cpp`'s `sampleCount()`, holding ~6 m spacing); `N_DEFAULT` is now just the floor and the USD/`track.samples` default. Cross-section width sampling is separately adaptive, not a fixed segment count — see `TrackBake.cpp`'s `crossBreak()`.
- **1 world unit = 1 metre** (see `CONTEXT.md`). `PathHandling::maxSpeed` defaults to 140 (= 140 m/s, 504 km/h); `tungsten-monoxide`'s HUD is a straight `speed * 3.6` → km/h. Authored tracks run 7–10 km; the editor's starter track (a calibrated flat 8 km circle) lives in that regime.

## Editor conventions (`cpp/editor/`)

- Editor state lives in `editor::EditorState`, wrapping a single mutable `editor::TrackDefinition` (see `EditorState.hpp`).
- Undo/redo (`EditorHistory.hpp`) uses whole-track deep-clone snapshots (`undoStack_`/`redoStack_`, capped at `kMaxHistory`), not diffs. Every discrete mutation calls `pushUndo()` once *before* mutating, capturing pre-edit state. Continuous gestures (dragging a point, typing in a field) call `pushUndo()` once at gesture start, not per-tick, so one drag = one undo step — see `dragMutated_` for how point-selection-without-dragging avoids recording a no-op step.
- Dragging a mesh region in Edit mode moves it; shift+drag rotates it about its own placement origin instead (`meshRotating_`). The rotate branch records the offset between the drag start's angle-from-origin and the placement's current `rotation` at mousedown, then keeps applying that offset as the mouse moves, so the shape doesn't jump to face the cursor the instant the drag begins. Angle convention matches `TrackMesh`'s `localToWorld`: `atan2(dz, dx)` from the placement's `(x, z)`, in degrees.
- The top-down canvas's right-click context menu (`TopDownCanvas.cpp`) opens on mouse-*release* only when the accumulated right-drag delta is negligible (< 3px) — a real drag still pans, since the drag delta is reset only on release, after the drag's own per-frame pan deltas already applied.
- The mode dropdown is `Edit | Create | Rails` (`EditMode`), with `E`/`C`/`R` shortcuts. All mode changes go through `setEditMode()` — it clears the abandoned create draft, drops the rail pick when leaving Rails mode, and syncs the dropdown, so the keys and the dropdown can't drift. Rails mode is modal on purpose: only mesh edges are pickable, so flagging a rail can't be confused with selecting anything else. Mesh regions are hit-tested *last* in edit mode, after every path handle, so a large region never steals a click from a control point drawn on top of it.
- The top-down grid has a visible checkbox and `G` shortcut. Hiding it temporarily disables both the grid-size control and snapping; the Snap checkbox retains its preference so showing the grid restores the prior snap setting. `TopDownView::snapWorldXZ()` also checks visibility directly, so disabled UI cannot leave hidden-grid snapping active.
- Bundled texture assets are stored as `"assets/track/..."`, relative to the repo root. `cpp/editor/src/TextureCache.cpp`'s `get()` resolves a relative path against `findAssetsDir()`'s parent (the repo root), so track_editor.exe can be launched from any working directory (e.g. `cpp/build/editor/Release`) and still find the same on-disk texture.
