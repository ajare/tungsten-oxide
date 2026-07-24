# C++ Full-Track and Mesh-Region Port Plan

Status: **milestones M0–M1 complete.** This is the follow-on to
`CPP_PORT_PLAN.md`. The existing baked-corridor C++ parity suite remains intact.

- **M0:** combined MSVC/CMake baseline, shared fixtures, and focused C++ test harness.
- **M1:** complete THREE-free JS physics bake (meshes/effects/floor), renderer-neutral geometry,
  runtime mesh branch tests, and shipping-game use of the shared physics bake.

## 1. Goal

Make the native C++ core capable of loading a complete current-schema track JSON file without
JavaScript, baking its spline paths and placed mesh assets, generating graphics-API-agnostic render
geometry, and running every mesh-related physics branch with measured JS/C++ parity.

JavaScript remains the reference oracle during this port. C++ will independently parse and bake the
raw authored data; it must not depend on JavaScript-baked mesh loops or centerline frames in the new
end-to-end tests.

## 2. Agreed scope

### Included

- Strict loading of the current track schema (currently version 10) from a string or file.
- Runtime-relevant normalization of paths, handling, start pose, seams/junctions, self-intersection
  overrides, mesh assets/placements, texture metadata, zones, and triggers.
- Native spline evaluation and baking required to replace `TrackCore.parseTrack()`,
  `js/track-bake.js`, and the physics/rendering portions of `buildTrack()`.
- Flat placed mesh regions with translation, yaw, elevation, polygons, holes, and shared seams.
- Per-edge rail attributes, finite rail height, two-sided swept collision, slide/restitution, and
  concave-corner resolution.
- Surface arbitration between spline corridors and overlapping mesh regions.
- Grounded mesh movement, mesh-to-mesh and mesh-to-corridor transitions, bare ledges, airborne rail
  impacts, mesh landing, and respawn-floor calculation.
- Mesh-hosted zones and triggers.
- Graphics-API-agnostic geometry for path surfaces, path shells, spline guard rails, mesh surfaces,
  mesh rails, and zone surfaces.
- Position, normal, UV, and RGBA per render vertex. RGBA defaults to `(1,1,1,1)`.
- Texture asset/tile metadata on relevant geometry batches; no image loading or decoding.
- Runtime-relevant JS tests, equivalent C++ tests, and raw-track end-to-end parity traces.

### Excluded

- Historical schema migration. A missing version or any version other than the supported current
  version is a fatal, explicit load error.
- Lossless preservation of unknown/editor-only JSON fields and native track saving/editing.
- Graphics APIs, materials, shaders, image loading, wireframe helpers, minimap/HUD code, and trigger
  debug geometry.
- Exact triangle ordering or diagonal selection between geometry-js and Willpower.Geometry.
- Porting the full geometry-js test suite or editor operations.
- Changes to the external `D:/Code/Projects/willpower` checkout. Only `cpp/willpower` may change.

## 3. Architecture

### 3.1 Build and dependencies

- Move `core` to C++20 and link it functionally to `Willpower::Geometry`.
- Keep `cpp/` as the primary combined build. Preserve standalone `cpp/core` configuration by having
  it import the sibling Willpower CMake project when the target is not already present.
- Promote the vendored `nlohmann/json.hpp` from parity-test-only use to a private implementation
  dependency of `core`; do not expose nlohmann types in public headers.
- Keep `/fp:precise` and disabled FP contraction rules used by the existing parity work.

### 3.2 Public loading API

```cpp
struct TrackWarning {
  std::string code;
  std::string message;
  std::string objectId;
};

struct TrackLoadResult {
  std::optional<Track> track;
  std::vector<TrackWarning> warnings;
  std::string error;
  explicit operator bool() const;
};

TrackLoadResult Track::fromJson(std::string_view json);
TrackLoadResult Track::fromFile(const std::filesystem::path& path);
```

Malformed top-level JSON, an unsupported version, missing required path data, or no usable driving
surface is fatal. Invalid mesh assets/placements are skipped with structured warnings, matching the
recoverable JS policy. A polygon triangulation failure warns and omits its render triangles while
retaining valid containment loops and rails for physics.

### 3.3 Authored and compiled data

Keep authored/normalized records separate from baked runtime records:

- `TrackDefinition`: normalized current-schema runtime subset.
- `PathDefinition`: typed position/roll/width/cross-section points and optional texture binding.
- `MeshAssetDefinition`: source vertices/topology, rail-edge IDs, and rail height.
- `MeshPlacementDefinition`: asset reference and rigid world transform.
- `TextureAsset`: ID/path/dimensions/tile dimensions only.
- Existing `Path`, `Frame`, `Zone`, and `Trigger`: expanded from baked-trace records into native bake
  outputs.
- `CompiledMeshRegion`: placement identity, elevation, rail height, bounds, double-precision polygon
  loops/holes, rail segments, and render triangulation.

References used during simulation are stable indices, not raw pointers. In particular, mesh zones
carry `hostRegionIndex`; sampled path zones retain `hostPathIndex`.

### 3.4 Willpower adapter and precision

Willpower.Geometry uses float `Vector2`, while JS and the physics core use doubles. Use it without
forcing a library-wide scalar conversion:

1. Parse mesh JSON coordinates as doubles and retain them in an adapter-owned vertex table.
2. Build a Willpower `geometry::Mesh` for topology validation, polygon/hole ownership, connectivity,
   and triangulation.
3. Maintain explicit serialized-ID to Willpower-runtime-index maps for vertices, edges, and polygons.
4. Keep rail flags in a side table keyed by mapped runtime edge index; do not require generic
   Willpower user-attribute serialization for the one runtime attribute needed here.
5. Fetch triangle indices from Willpower, but fetch final render positions from the retained double
   vertex table before applying the placement transform.
6. Build physics loops, bounds, normals, and rail segments entirely in double precision.

This uses the native geometry library while avoiding float quantization in collision. Equivalent
triangulation is required; exact geometry-js triangle order is not.

### 3.5 Mesh query layer

Add a C++ counterpart to `js/track-mesh.js` containing faithful double-precision versions of:

- placement transform;
- bounds;
- point-in-loop and polygon-minus-holes containment;
- bounds tests;
- closest point on segment;
- segment crossing;
- rail compilation and outward-normal probing;
- iterative swept `slideAlongRails()` with two-sided resolution and restitution.

The simulation consumes only `CompiledMeshRegion`; it does not call Willpower during a physics step.
All topology/triangulation work happens once during track loading/baking.

### 3.6 Full spline/path bake

The current C++ `Track` only reads JS-baked frames. Port the current-schema runtime subset needed to
independently reproduce JavaScript:

- schema validation and field normalization/defaults;
- path point splitting and stable IDs;
- rational spline evaluator and frame construction;
- adaptive physics sample count;
- edge construction and local self-intersection cleanup;
- junction/disjoint-seam cuts and endpoint normal handling;
- physical wall offsets;
- adaptive render-frame sampling;
- cross-section breakpoints/stitch points;
- path top surface, shell, and spline guard-rail geometry;
- path-hosted zone strips and trigger frames;
- automatic Finish checkpoint repair;
- start pose and handling.

Keep math operation order as close to JavaScript as practical, as was done for `Vec3`. Do not replace
algorithms with superficially equivalent Willpower spline utilities; the JS implementation remains
the parity oracle.

### 3.7 Graphics-agnostic output

```cpp
struct Vec2d { double x, y; };
struct Color4 { double r{1}, g{1}, b{1}, a{1}; };

struct RenderVertex {
  Vec3 position;
  Vec3 normal;
  Vec2d uv;
  Color4 rgba;
};

enum class GeometryKind {
  PathSurface,
  PathShell,
  PathRail,
  MeshSurface,
  MeshRail,
  ZoneSurface
};

struct TextureBinding {
  std::string assetId;
  int tile{0};
};

struct GeometryBatch {
  std::string id;
  GeometryKind kind;
  std::string materialKey;
  std::vector<RenderVertex> vertices;
  std::vector<std::uint32_t> indices;
  bool hasUv{false};
  std::optional<TextureBinding> texture;
};
```

Generate normals with Three.js-compatible triangle accumulation where shared indexed vertices are
used; use face normals where JavaScript converts geometry to non-indexed/flat-shaded form. Vertex
colour remains white. `materialKey` communicates semantic appearance (`road`, `shell`, `rail`,
`mesh-region`, and zone effect) without embedding renderer policy.

## 4. JavaScript reference work

Before changing C++ physics, make the complete mesh world headless and testable:

1. Extend `js/track-bake.js` to compile mesh placements through `TrackMesh`, include mesh elevations
   in `trackFloorY`, and bake mesh-hosted zones/triggers. Remove its current mesh-free limitation.
2. Add a pure graphics-agnostic JS geometry builder for the agreed batches, extracting the relevant
   path/shell/rail/zone construction from `track-game.js` rather than creating a third independent
   algorithm.
3. Rewire `track-game.js` to consume shared baked/query data where practical so tests exercise the
   shipping implementation, not a test-only facsimile.
4. Preserve browser behavior with `tools/browser-smoke.mjs`.
5. Keep geometry-js as the JS mesh topology/triangulation implementation and `cpp/willpower` as its
   C++ counterpart.

## 5. C++ simulation integration

Port the omitted branches from `Simulation.stepPhysics()` in the same order as JS:

- `meshRegionAt()` with bounds/containment and overhead penalty;
- `surfaceOwnerAt()` corridor-versus-mesh arbitration;
- airborne sweep against finite-height mesh rails;
- airborne mesh landing before corridor landing;
- grounded movement and rail slide on a mesh;
- same-elevation/nearest-height mesh transition;
- mesh-to-corridor snap;
- bare-edge launch;
- parked-on-mesh behavior;
- mesh-hosted zone detection.

Update `trackFloorY` to include mesh elevations. Mesh trigger gates are compiled during loading and
continue through the existing generic swept trigger code.

Do not weaken the existing spline-only branches while integrating meshes. Retain source ordering and
intermediate operation order wherever possible.

## 6. Test strategy

### 6.1 Shared fixtures

Create current-schema JSON fixtures used by both languages:

- transformed square pad;
- pad with a hole and hole rails;
- two polygons sharing an unrailed interior seam;
- concave fully railed pad;
- corridor-to-mesh bridge course;
- upper/lower overlapping mesh placements;
- mesh-hosted boost and checkpoint course.

Fixtures stay small and use non-grazing coordinates except in dedicated boundary tests.

### 6.2 JavaScript unit/integration tests

Add runtime-relevant tests for:

- current-version loading expectations and dangling-reference removal;
- placement transforms and bounds;
- polygon/hole containment;
- rail attribute mapping and outward normals;
- equivalent triangulated area and hole subtraction;
- head-on, glancing, high-speed, outside, restitution, and corner rail collisions;
- corridor/mesh surface ownership;
- mesh-to-mesh, mesh-to-corridor, and bare-ledge transitions;
- airborne collision below rail top, clearance above rail top, and mesh landing;
- mesh contribution to respawn floor;
- mesh zones and triggers;
- render attributes: finite positions/normals/UVs, valid indices, and white RGBA.

### 6.3 Equivalent C++ tests

Add a dedicated C++ test executable rather than growing `parity_main.cpp` indefinitely. Port the
same fixture scenarios and semantic assertions. Geometry equivalence tests compare:

- exact topology relationships and rail identities where deterministic;
- bounds and transforms within geometry tolerance;
- total triangulated area, surface coverage, and hole exclusion rather than triangle order;
- unit/finite normals, expected UV presence, valid indices, and exact white RGBA.

### 6.4 Two parity layers

Retain the existing baked-world suite unchanged:

- JS-baked corridor frames loaded by both engines;
- current strict 1-ULP-derived mixed tolerance;
- isolates changes to simulation math.

Add raw-track end-to-end parity:

- trace contains the normalized current-schema source track, controls, initial state, and JS outputs;
- JS independently loads/bakes the raw track;
- C++ `Track::fromJson()` independently loads/bakes the same raw track;
- C++ runs per-step replay from each recorded prior JS state;
- C++ also performs a bounded free run using its own preceding states.

Add deterministic scenarios for:

1. corridor -> mesh -> corridor;
2. bare ledge -> airborne -> lower mesh landing;
3. head-on/glancing/corner rail impacts;
4. airborne outside approach below rail height;
5. airborne clearance above rail height;
6. overlapping regions at different elevations;
7. mesh-hosted boost and checkpoint;
8. traversal around a polygon hole;
9. one longer mixed mesh course.

Discrete results (surface branch, airborne state, collision hit, boost/trigger/checkpoint state,
respawn, lap state) must match exactly. Continuous values use a separately measured raw-bake/mesh
tolerance.

## 7. Tolerance policy

- Do not change the existing baked-corridor parity tolerance.
- Begin native-bake geometry and physics checks with diagnostic tolerances, record worst offenders by
  field/fixture/step, and report absolute, relative, and ULP differences.
- Lock separate geometry and end-to-end physics tolerances at a small fixed multiple of the observed
  worst legitimate drift.
- Do not hide branch disagreements with numeric tolerance: booleans, IDs, enums, selected surface,
  and event state compare exactly.
- Keep fixtures away from ambiguous boundaries unless testing the boundary itself; boundary tests
  assert the shared semantic rule rather than expecting tolerance to choose a side.

## 8. Implementation milestones

### M0 — Baseline and harness

- Restore a repeatable MSVC/CMake invocation in the agent environment.
- Run and record `npm test`, browser smoke, combined C++ build, and existing CTest parity.
- Add shared current-schema mesh fixtures and a second C++ test target.

**Exit:** all pre-existing tests pass unchanged.

### M1 — Headless JS full-track reference

- Extend mesh baking, mesh zones/triggers, and floor calculation.
- Add graphics-agnostic JS geometry records and tests.
- Add direct JS simulation tests for every mesh branch.
- Keep the browser game behavior unchanged.

**Exit:** Node tests and browser smoke prove the reference implementation is testable headlessly.

### M2 — Native loader and normalization

- Add public load-result API and private nlohmann implementation.
- Strictly accept current schema only.
- Parse the runtime subset, defaults, references, texture metadata, zones/triggers, handling, and
  start descriptor.
- Add structured warning/error tests.

**Exit:** JS and C++ normalized records agree on shared fixtures; invalid mesh records recover as
agreed.

### M3 — Native spline bake and path geometry

- Port the required TrackCore evaluator/bake algorithms.
- Produce path physics frames and graphics-agnostic surface/shell/rail/zone geometry.
- Compare normalized and baked outputs against JS before involving mesh physics.

**Exit:** C++ can load and independently bake spline-only current-schema files, with locked geometry
comparison diagnostics and the old baked parity suite still passing.

### M4 — Willpower mesh adapter and compiled regions

- Link `core` to Willpower.Geometry.
- Deserialize topology with ID maps and rail side tables.
- Compile double-precision world loops, holes, bounds, rails, and render geometry.
- Port the runtime-relevant `track-mesh` unit tests.

**Exit:** JS/C++ mesh transforms, containment, rails, and equivalent render surfaces agree.

### M5 — Mesh simulation branches

- Add mesh regions to `Track` and port surface ownership, rail sweep, grounded transitions, airborne
  behavior, landing, and mesh zone detection.
- Compile mesh trigger gates through the existing trigger state machine.

**Exit:** equivalent JS/C++ scenario tests pass, including exact discrete outcomes.

### M6 — Raw-track traces and tolerance lock

- Add the deterministic mesh parity corpus and a longer mixed course.
- Extend trace tooling without removing old baked fixtures.
- Calibrate and lock separate native-bake geometry and end-to-end physics tolerances.
- Retain worst-field/step reporting and bounded free-run checks.

**Exit:** both parity layers pass; tolerance rationale and observed maxima are documented.

### M7 — Final integration and documentation

- Run clang-format only on project C++ changes, never upstream/external source indiscriminately.
- Run all JS, browser, C++ unit, loader, geometry, and parity suites.
- Update `CLAUDE.md`, `CPP_PORT_PLAN.md`, build instructions, and trace-generation documentation.

**Exit:** a clean checkout can load a full current-schema track in C++, expose renderer-neutral
geometry, and reproduce JS mesh physics within the locked tolerances.

## 9. Expected file layout

```text
cpp/core/include/
  Track.hpp                 expanded compiled runtime Track
  TrackDefinition.hpp       normalized authored records
  TrackLoader.hpp           public load-result declarations if not kept on Track
  TrackGeometry.hpp         render vertex/batch API
  TrackMesh.hpp             compiled mesh-region/query declarations
cpp/core/src/
  TrackLoader.cpp
  TrackBake.cpp
  TrackGeometry.cpp
  TrackMesh.cpp
  Simulation.cpp            mesh branches restored
cpp/core/tests/
  parity_main.cpp           existing baked suite + raw trace mode or shared helpers
  track_tests.cpp           loader/bake/geometry/mesh scenario tests
js/
  track-bake.js             full physics bake including mesh regions
  track-render-geometry.js  pure graphics-agnostic reference geometry
  track-mesh.js             existing shared mesh queries
test/
  fixtures/mesh/            shared current-schema fixtures
  track-mesh-physics.test.js
  track-render-geometry.test.js
  traces/                   old baked traces plus raw mesh traces
```

Exact header splitting may be adjusted to keep ownership and compile times sensible, but the authored
definition, compiled physics representation, render geometry, and mesh query responsibilities must
remain separate.

## 10. Required verification commands

```text
npm test
node tools/browser-smoke.mjs
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
npm run parity
```

Trace regeneration remains deliberate and reviewable; only regenerate committed fixtures when the
new raw-track/mesh format is intentionally introduced or physics behavior intentionally changes.
