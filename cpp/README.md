# Native C++ track engine

`cpp/core` is a C++20 static library that independently loads and runs complete
schema-10 through schema-12 tracks. `ext/willpower` is the repository-pinned Willpower submodule.

## Capabilities

- strict schema-10/12 JSON/file loading with structured recoverable warnings;
- rational spline evaluation, adaptive physics/render sampling, seams, walls,
  cross-sections, shells and guard rails;
- renderer-neutral indexed batches with positions, normals, UVs, white RGBA,
  semantic material keys and texture tile metadata;
- complete path simulation including two-sided swept rail collision,
  transitions, airborne landing, zones, checkpoints and respawn;
- legacy baked-world parity against a committed golden trace corpus.

The loader accepts versions 10 through 12. Migration and native saving/editing are not
part of the runtime API. Mesh regions (placed mesh assets, authored via `meshAssets`/`meshes`) were
removed entirely in schema 12 (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2) — a track still
authoring them fails to load with an explicit error.

## Build and test

Use an x64 MSVC Developer prompt (or run `vcvars64.bat` first):

```text
git submodule update --init --recursive
cmake -S ext/willpower -B ext/willpower/build
cmake --build ext/willpower/build --config Release
cmake --build ext/willpower/build/_deps/massive-poly-pusher-build --config Release --target MppResourceParsers MppAppSupport assimp
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

The `cpp` build imports Willpower from `ext/willpower/build` and MassivePolyPusher from
`ext/willpower/build/_deps/massive-poly-pusher-build`; it never builds either dependency. Set
`TOX_WILLPOWER_BUILD_DIR` or `TOX_MPP_BUILD_DIR` to use prebuilt trees elsewhere. Shared-library
runtime dependencies are copied next to their executables.
Single-config generators work too:

```text
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

`cpp/core` can also be configured directly; it imports the prebuilt `ext/willpower/build`
libraries when `Willpower.Geometry` does not already exist:

```text
cmake -S cpp/core -B cpp/build-core
cmake --build cpp/build-core --config Release
ctest --test-dir cpp/build-core -C Release --output-on-failure
```

CTest entries:

- `parity` — unchanged 4000-step baked-world runtime gate;
- `track_tests` — loader, bake, geometry and simulation scenarios, plus a hard-break check for
  the removed `meshAssets`/`meshes` fields.

`raw_parity`/`raw_session_init_parity`/`raw_session_step_parity`/`random_geometry_parity` are
currently disabled: every fixture they replay references the `meshAssets`/`meshes` fields removed
in schema 12 (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2) and now hard-fails to load. Milestone 7
plans new mesh-mode-appropriate golden traces to restore this coverage.

The fixture/trace corpus under `cpp/test-data/` is a fixed, committed regression suite; there is no
in-repo tool to regenerate it, so treat it as append-only unless you're prepared to hand-author or
validate new fixtures directly against this C++ implementation. See
`cpp/test-data/fixtures/random-track-mesh/README.md`.

## Native runtime host

`cpp/app` builds `track_runner`, a minimal command-line host outside `core`
(only added by the combined `cpp` configure, not standalone `cpp/core`):

```text
track_runner <track.json>
```

It loads and bakes the track (`Track::fromFile`, including renderer-neutral
geometry), builds a `GameSession` with the default roster, and runs the
deterministic simulation loop in real time — each frame's `dt` comes from a
steady clock, not a fixed step — printing a once-a-second status line until
Escape is pressed. The roster stays idle (zero throttle): there is no
rendering, audio, or driving input, so this is a session lifecycle/timing
smoke host, not a playable client.

## Public data flow

```cpp
#include "Track.hpp"
#include "Simulation.hpp"

const tox::TrackLoadResult loaded = tox::Track::fromFile("track.json");
if (!loaded) {
  // Report loaded.error and stop.
  return;
}
// loaded.warnings may contain recoverable diagnostics.
const tox::Track& track = *loaded.track;
tox::Simulation simulation(track);
```

`Track::definition` retains normalized authored runtime data. Compiled data is
kept separately in `Track::paths`, `zones`, `triggers`, and `geometry`.
`GeometryBatch` is graphics-API-neutral; no renderer or image loader
is linked into `core`.

Important headers:

- `TrackDefinition.hpp` — normalized authored schema subset;
- `Track.hpp` — compiled paths, effects, gates and loading result;
- `TrackGeometry.hpp` — renderer-neutral vertex/batch contract;
- `Simulation.hpp` / `Ship.hpp` — runtime physics API and state;
- `TrackCore.hpp` / `Vec3.hpp` — shared parity-sensitive math.

## Numeric contracts

MSVC builds use `/fp:precise`; non-MSVC builds disable FP contraction. Locked
Release measurements against the committed golden trace corpus:

- baked world: `atol=rtol=1e-12`, ratio gate `1e-3`;
- raw track: `atol=rtol=1e-12`, ratio gate `0.1`;
- surface IDs, rail hits and all other discrete state compare exactly.

Do not reorder parity-sensitive vector/math operations casually. Run
clang-format only on project C++ files, never on the `ext/willpower` submodule.
