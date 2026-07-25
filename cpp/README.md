# Native C++ track engine

`cpp/core` is a C++20 static library that independently loads and runs complete
schema-10 tracks. It does not call JavaScript or consume JS-baked geometry.
`cpp/willpower` provides the embedded Willpower.Common and Willpower.Geometry
libraries used for mesh topology validation and triangulation.

## Capabilities

- strict schema-10 JSON/file loading with structured recoverable warnings;
- rational spline evaluation, adaptive physics/render sampling, seams, walls,
  cross-sections, shells and guard rails;
- placed mesh polygons, holes, double-precision containment/bounds/rails and
  Willpower triangulation;
- renderer-neutral indexed batches with positions, normals, UVs, white RGBA,
  semantic material keys and texture tile metadata;
- complete path/mesh simulation including surface ownership, two-sided swept
  rail collision, transitions, airborne landing, zones, checkpoints and respawn;
- legacy baked-world and current-schema raw-track parity against JavaScript.

The loader accepts only version 10. Migration and native saving/editing are not
part of the runtime API.

## Build and test

Use an x64 MSVC Developer prompt (or run `vcvars64.bat` first):

```text
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

The combined build compiles the repository-pinned Willpower sources and copies
shared-library runtime dependencies next to `parity` and `track_tests`.
Single-config generators work too:

```text
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

`cpp/core` can also be configured directly; it imports sibling `cpp/willpower`
when `Willpower::Geometry` does not already exist:

```text
cmake -S cpp/core -B cpp/build-core
cmake --build cpp/build-core --config Release
ctest --test-dir cpp/build-core -C Release --output-on-failure
```

CTest entries:

- `parity` — unchanged 4000-step baked-world runtime gate;
- `raw_parity` — 12 independently loaded/baked tracks, 1116 steps;
- `track_tests` — loader, bake, topology, geometry and simulation scenarios.

Run `npm run parity` from the repository root for JS self-replay followed by both
C++ parity layers. Trace regeneration is deliberate: `npm run gen-traces`.

## Public data flow

```cpp
#include "Track.hpp"
#include "Simulation.hpp"

const tox::TrackLoadResult loaded = tox::Track::fromFile("track.json");
if (!loaded) {
  // Report loaded.error and stop.
  return;
}
// loaded.warnings may contain recoverable skipped-mesh diagnostics.
const tox::Track& track = *loaded.track;
tox::Simulation simulation(track);
```

`Track::definition` retains normalized authored runtime data. Compiled data is
kept separately in `Track::paths`, `meshRegions`, `zones`, `triggers`, and
`geometry`. `GeometryBatch` is graphics-API-neutral; no renderer or image loader
is linked into `core`.

Important headers:

- `TrackDefinition.hpp` — normalized authored schema subset;
- `Track.hpp` — compiled paths, effects, gates and loading result;
- `TrackMesh.hpp` — compiled mesh records and collision queries;
- `TrackGeometry.hpp` — renderer-neutral vertex/batch contract;
- `Simulation.hpp` / `Ship.hpp` — runtime physics API and state;
- `TrackCore.hpp` / `Vec3.hpp` — shared parity-sensitive math.

## Numeric contracts

MSVC builds use `/fp:precise`; non-MSVC builds disable FP contraction. Locked
Release measurements are documented in `MESH_CPP_PORT_PLAN.md`:

- baked world: `atol=rtol=1e-12`, ratio gate `1e-3`;
- raw track: `atol=rtol=1e-12`, ratio gate `0.1`;
- surface IDs, rail hits and all other discrete state compare exactly.

Do not reorder parity-sensitive vector/math operations casually. Run
clang-format only on project C++ files, never indiscriminately on embedded
upstream Willpower code.
