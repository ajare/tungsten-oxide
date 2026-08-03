# cpp/core — the track-physics engine

`cpp/core` is a self-contained C++20 static library (target `core`) that loads a schema-10/12 track, bakes its authored spline data into runtime geometry, and runs the corridor/mesh driving simulation. It has no renderer, no image loader, and no platform/input dependency — see `cpp/README.md` for the build-level contract and public data-flow example. This document is the feature- and physics-level companion to that README: what a track can *contain*, and exactly how the simulation resolves it frame to frame, with pointers into the code.

Domain vocabulary (Track, Path, Control point, Zone, Trigger/Checkpoint, Reservation, etc.) is defined once in `UBIQUITOUS_LANGUAGE.md` and `glossary.md` (both in this directory) — this document uses those terms without redefining them. Mesh regions (placed mesh assets, plus the synthetic mesh region a central reservation used to compile to) were removed entirely in `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2, with no interim replacement; "mesh mode" below refers only to the BVH-based `stepMeshPhysics` ship-physics mode, which never used `MeshRegion` and is unaffected.

## Intended use

`core` is meant to be embedded by a host that supplies input, rendering, and timing:

- `cpp/tungsten-monoxide` is the real playable host (see `docs/tungsten-monoxide.md`) — it drives `core` through `GameSession`.
- `cpp/app`'s `track_runner` is a minimal headless session/timing smoke host (no rendering, no input, idle ships).
- `cpp/editor` reuses `core` as a black box purely for baking/validation — it never touches simulation.
- The `parity`/`track_tests` CTest targets replay a committed golden-trace corpus against it (see [Numeric contracts and golden-trace testing](#numeric-contracts-and-golden-trace-testing) below). `raw_parity`/`raw_session_init_parity`/`raw_session_step_parity`/`random_geometry_parity` are currently disabled — every one of their fixtures references the removed `meshAssets`/`meshes` and now hard-fails to load (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2); Milestone 7 plans new mesh-mode-appropriate golden traces to restore this coverage.

`core` accepts **schema 10 through 12** JSON and always normalizes/writes schema 12 (`TrackCore::TRACK_SCHEMA_VERSION = 12`, `TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED = 10`, both in `cpp/core/include/TrackCore.hpp`). Schema 10 support exists solely because the golden-trace corpus is permanently pinned at schema 10. A track that still authors `meshAssets`/`meshes` (removed in schema 12) fails to load with an explicit error rather than silently dropping them — see `TrackLoader.cpp`.

## Track-design features

Authored data lives in `TrackDefinition` (`cpp/core/include/TrackDefinition.hpp`), deliberately kept separate from the compiled runtime record (`Track.hpp`) — see that header's own opening comment.

### Track

`TrackDefinition` (`TrackDefinition.hpp:198`) is the top-level record: `paths`, `textureAssets`, `zones`, `triggers`, `disjointSeams`, `junctions`, `selfIntersectionOverrides`, `handling`, `start`.

- **`handling`** (`HandlingDefinition`, `TrackDefinition.hpp:189`) — `maxSpeed` (140), `accel` (71), `turnSpeed` (137.5 deg/s), `weight` (1000). Copied into every ship by `ShipFactory::applyHandling`. Only these four of the ~12 tunable `Physics` parameters are authorable per track — brake deceleration, friction, grip, gravity, and wall restitution stay at their `Physics` struct defaults (`Ship.hpp:16`).
- **`start`** (`StartDefinition`, `TrackDefinition.hpp:193`) — `path`, `point` (a control-point index), `reverse`. Anchors the starting grid (`StartGrid::startingGridPoses`) and, if no `role="finish"` checkpoint was authored, the auto-generated finish trigger (`TrackBake.cpp`, `bakeTrack`, ~line 1541).
- **`samples`** — an authored hint that is largely vestigial; the physics centerline sample count is computed instead (see [Baking](#baking-pipeline)).

### Path

`PathDefinition` (`TrackDefinition.hpp:96`) — one spline ribbon road: `id`, `closed`, `points` (the mixed control-point list), `texture` binding, `material` (a namespace-qualified Willpower `TrackMaterial` name, falling back to the legacy literal `"road"` when empty), and `reservations`.

### Control points

One struct, `TrackPointDefinition` (`TrackDefinition.hpp:21`), four `TrackPointKind`s (`Position | Roll | Width | CrossSection`), separated at bake time by `split()` (`TrackBake.cpp:22`):

| Kind | Fields | Effect |
|---|---|---|
| **Position** | `pos`, `weight` (1.0) | The rational-spline control vertex and its NURBS-style rational weight — pulls the curve toward that point. Defines the centerline. |
| **Roll** | `t`, `roll` (0.0°) | Banking, in degrees, at path-fraction `t`. Converted to radians and used to rotate `edgeRight`/`normal` in `frame()` (`TrackBake.cpp`). |
| **Width** | `t`, `width` (36.0 m), `centerOffsetPercent` (0.0) | Road chord width, floored at 1.0 m. `centerOffsetPercent` (±50 max) shifts the *baked* road center toward `edgeRight`, independent of the authored position spline. |
| **CrossSection** | `t`, `curvature` (0.0, ±1 clamp), `tightness` (1.0, [0.2, 4.0] clamp), `thickness` (4.0 m) | The banked "cradle" profile amplitude/sharpness across the road. `thickness` is **render-only** (drives the shell/slab depth) — physics never sees it. |

All three scalar channels (`roll`/`width`/`crossSection`) are interpolated by a shared Catmull-Rom-style helper (`scalar()`, `TrackBake.cpp:40`), with correct wrapping for closed paths.

### Reservations (central-reservation voids)

A void carved from the road between `t0` and `t1`, tapering from each end cap's width to `width` at the midpoint. Full design history in `CENTRAL_RESERVATION_PLAN.md` at the repo root; data model in `ReservationDefinition` (`TrackDefinition.hpp:85`).

- **`widthMode`** — `Fixed` (metres) or `Percent` (0–100% of the road's own local width at that point, so the void tracks an authored width curve).
- **End caps** (`endCap0`/`endCap1`, `ReservationEndCapStyle`) — **Joined** (tapers to a self-sealing point), **Mitred** (holds the cap width and cuts off square), or **Rounded** (the last `noseLength` metres close as a half-ellipse dome; `noseLength ≤ 0` defaults to `capWidth/2`, a true half-circle).

A reservation only carves the road void now (gap taper/end-caps) — the synthetic `MeshRegion` it used to compile to (one-way rail collision, plus the Capped-interior curved floor / Uncapped open shaft distinction, `interiorMode`/`wallHeight`/`railClearanceHeight`) was removed in `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2 with no interim replacement. A car in analytic-physics mode can currently drive off the void's edge with nothing to stop it (mesh mode is unaffected — it never used `MeshRegion`).

Reservations must be authored non-overlapping per path — `reservationHalfGapAt` (`TrackBake.cpp:311`) takes the first span containing `t` as authoritative and does not validate overlap itself (the editor enforces it at authoring time).

### Zones

`ZoneDefinition` (`TrackDefinition.hpp:158`) — a flat area with an `effect`: `"velocityChange"` (boost, `factor` 1.5 × maxSpeed by default, `duration` 2.0s), `"jump"`, or `"startGrid"`. Hosted on a path (`t`, `lateral` offset); `host.kind` stays a string discriminator (always `"path"` now) for a future drivable-mesh-object-hosted variant to reuse.

> **Note:** `"startGrid"` is authored and survives baking, but nothing currently acts on it at runtime — `Simulation::detectZoneTriggers` (`Simulation.cpp:286`) only handles `"velocityChange"` and `"jump"`; the actual starting grid is derived independently from `TrackDefinition::start`.

### Triggers / checkpoints

`TriggerDefinition` (`TrackDefinition.hpp:173`) — a vertical gate: `type` (`"checkpoint"` or `"dummy"`), `role` (`"finish"` or empty = intermediate), `direction` (`"forward"`/`"backward"`/`"both"`), same path-hosted model as zones. A lap requires crossing every intermediate checkpoint before the finish (`Simulation::fireTrigger`, `Simulation.cpp:341`).

### Junctions, disjoint seams, self-intersection overrides

`ConnectionDefinition` (`TrackDefinition.hpp:180`) backs both:

- **Disjoint seams** weld two open-path endpoints — their frame normals are averaged and both edge corners are re-cut at the intersection.
- **Junctions** mark a branch point, which disables self-intersection processing for every path touching it.
- **`SelfIntersectionOverrideDefinition`** (`TrackDefinition.hpp:185`) overrides the default collapse-span heuristic for one specific crossing (identified by control-point ids, so it survives resampling).

### Texture assets

`TextureAssetDefinition` (`TrackDefinition.hpp:146`) — `path`, `width`/`height`, `tileWidth`/`tileHeight`. A path's `texture` field (`TextureBindingDefinition`) references an asset id plus a tile index.

## Baking pipeline

Entry: `Track::fromJson`/`fromFile` (`Track.hpp:117`) → `TrackLoader.cpp`'s `normalize()` → `bakeTrack()` (`TrackBake.cpp:1282`).

This is the part of the codebase where "analytical" and "geometry/mesh-based" genuinely diverge, and it matters for anyone reasoning about correctness or performance:

### Analytical (closed-form math, no triangles consulted)

| What | Where | How |
|---|---|---|
| Spline evaluation | `Evaluator::baseEval`, `TrackBake.cpp:96` | A rational cubic B-spline: uniform basis functions, each control point's `weight` in both numerator and denominator, tangent via the quotient rule. |
| Signed path curvature | `Evaluator::curvatureAt`, `TrackBake.cpp:146` | Second derivative via the basis functions' second derivative, then `κ = (x'z''−z'x'')/(x'²+z'²)^1.5`. **Editor-only** — exposed as `TrackCore::pathSignedCurvatureAt` (declared `TrackCore.hpp`, body `TrackBake.cpp:1274`), never baked into `Frame`; it's what drives the editor's Camber render mode. |
| Center-offset shift | `Evaluator::shiftedPosition`/`eval`, `TrackBake.cpp:184` | Guarded by `hasCenterOffset` so zero-offset tracks (the common case) take the historical bit-identical path. |
| Road frame construction | `frame()`, `TrackBake.cpp:219` | Builds `h = cross(UP, tangent)`, a binormal, then rotates both by `roll` to get `edgeRight`/`normal`. |
| Cross-section profile | `TrackCore::crossSectionHeight`/`…Derivative`, `TrackCore.cpp:22` | `curvature·(width/2)·(√(1−u²))^tightness` — the banked-cradle shape. |
| Runtime surface at an offset | `curvedSurfaceFrame`, `Simulation.cpp:71` | `pos + edgeRight·offset + normal·lift`; normal from `cross(tangent, edgeRight·span + normal·dLift/dOffset)`. This is the function physics actually stands ships on when it isn't consulting exported triangles. |
| Reservation gap width | `reservationHalfGapAt`, `TrackBake.cpp:311` | Closed-form: peak width (Fixed vs. Percent) × a Hermite 0→1→0 taper, then the Mitred shelf or Rounded-ellipse floor. |
| Edge offsetting/trimming | `edges()`/`trim()`, `TrackBake.cpp:502`/`383` | Offset ±half-width, then collapse backward-running (self-crossing) edge runs to a line intersection. |

**Sample density is adaptive, not fixed.** The physics centerline uses `sampleCount()` (`TrackBake.cpp:373`) = `clamp(driven_length / 6, 400, 2000)` — roughly 6 m spacing regardless of track length; `TrackCore::N_DEFAULT` (400) is only the floor. The separate **render** mesh (`adaptiveRenderBake`, `TrackBake.cpp:573`) uses its own non-uniform ring set via chord-tolerance bisection plus forced rings at reservation boundaries — a materially finer, independent sampling from the physics centerline.

### Geometry / mesh-based (triangles, Willpower topology, or exported collision data)

| What | Where | How |
|---|---|---|
| Render/collision triangle batches | `TrackBake.cpp:800`–`1015`, `1418`–`1663` | `GeometryBatch`/`GeometryKind` (`TrackGeometry.hpp:20`). The road surface uses smooth per-vertex analytical normals (`triSmooth`); everything else (rails, shell, zones, triggers) uses flat per-triangle normals (`triNormal`). |
| Reservation void carve | `carveQuad`, `TrackBake.cpp:784` | Corner-wise strict-interior test against each ring's gap band, shared by the top surface and the shell underside so both holes agree exactly — the void is always an open shaft now (the Capped-interior curved-floor case was removed with `MeshRegion`, `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2). |
| Exported collision mesh | `TrackCollision.cpp` | A BVH (median split, ≤8 triangles/leaf) over exported triangles, with Möller–Trumbore ray intersection and barycentric-interpolated vertex normals. **Populated only by the game host** — `cpp/tungsten-monoxide/src/Map.cpp` builds it from a `.mppmodel`; JSON-only consumers (the editor, `track_runner`, this library's own tests) leave `Track::collisionSurface` null and physics stays fully analytical. |

### Collision BVH: construction and consumers

`TrackCollisionSurface` (`TrackCollision.hpp`/`.cpp`) is `core`'s only spatial index — everything else in this library (`sampleTrack`, `surfaceOwnerAt`, mesh-region queries) is a brute-force scan. It is built exactly once per loaded map, entirely inside the game host:

1. **Triangle selection** (`buildCollisionTriangles`, `Map.cpp:79`) — for each name in the exported `.mppmodel`'s `<TrackMeshes>` list, decode its 36-byte non-indexed triangle vertex stream (position + normal) and keep it only if its `GeometryBatch::kind` passes `gameplayKind()` (`Map.cpp:59`): `PathSurface`, `MeshSurface`, `ReservationWall`, `PathRail`, `MeshRail`. `PathShell` (and anything else) is excluded — render-only, never collidable. `MeshSurface`/`MeshRail`/`ReservationWall` are legacy `GeometryKind` values left in place after `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2 removed everything that used to produce them (`MeshRegion` and its reservation-wall geometry); nothing in `core` bakes them anymore, so `gameplayKind()` accepting them is currently a no-op, not a bug. Each decoded triangle is checked position-for-position and normal-for-normal against the corresponding `TrackData` `GeometryBatch` (`matchesExportedFloat`) before being accepted, so a stale or hand-edited `.mppmodel` fails loudly instead of silently diverging from the JSON it's supposed to match. `gameplayKind()` must stay in lock-step with the editor's own `<TrackMeshes>` export-selection loop (`MppModelExport.cpp`) — same set, same reasoning, checked in both directions.
2. **Load-time invariant** (`Map.cpp:197`–`202`) — `Map::load` throws if the resulting triangle list is empty; a drivable track is required to export at least one collidable batch (enforced earlier too, at editor export time in `TrackResourceSave.cpp`).
3. **BVH build** (`TrackCollisionSurface::build`, `TrackCollision.cpp:84`) — recursive median-split: each node's triangles are bounded (`Bounds`, both vertex-extent and centroid-extent), splitting stops and the node becomes a leaf once it holds ≤8 triangles, otherwise it splits along whichever of x/y/z has the **largest centroid extent** (not a fixed or round-robin axis) via `std::nth_element` on the centroid coordinate — an unbalanced-but-cheap median split, not a full SAH build.

**Consumers: physics only, not rendering.** The BVH is read in exactly one place outside its own file: `cpp/core/src/Ship.cpp`, via `nearestAlongAxis`/`nearestAcrossAxis`/`sweep`/`sweepWall` — the collision-surface pre-probe in analytic mode (§3 below) and the entirety of mesh-mode grounding/wall/airborne resolution (`stepMeshPhysics`), gated on `simulation.track().collisionSurface` being non-null (`Ship.cpp:299`). **There is no renderer-side consumer.** Nothing in this codebase uses `TrackCollisionSurface` (or any other spatial structure) for view-frustum or occlusion culling — every `GeometryBatch` `TrackBake.cpp` produces is handed to the renderer and drawn unconditionally, whether or not it's currently visible. If per-triangle or per-batch visibility culling is wanted, it would need to be built new; the collision BVH's Möller–Trumbore/segment-bounds queries (`querySegment`, `TrackCollision.cpp:125`) are ray/segment-shaped, not frustum-shaped, so it isn't a drop-in fit for that purpose even if reused.

### Self-intersection detection and collapse

`removeSelfLoops` (`TrackBake.cpp:424`) — two O(N²) passes over the edge polyline:

1. **Detection** (unbounded, full pairwise scan) — records every crossing with its span into `Track::selfIntersections`, gated by the `detectSelfIntersections` flag on `fromJson`/`fromFile` (default `true`; the editor's live-preview rebake passes `false` mid-drag and reuses its last result).
2. **Collapse** (iterative, up to `segmentCount` passes each O(N²), so worst case O(N³)) — collapses a crossing when its span ≤ `TrackCore::DEFAULT_SELF_INTERSECTION_SPAN` (100), unless a `SelfIntersectionOverrideDefinition` says otherwise. Skipped entirely for paths touching a junction.

## Physics

`Ship::step` (`cpp/core/src/Ship.cpp:11`) is the whole per-frame algorithm — one call per fixed sub-step (see [GameSession](#gamesession--shipfactory--startgrid) below for how sub-stepping works). Every branch is annotated below with whether it's analytical (closed-form corridor/cross-section math) or geometry-based (consults exported/baked triangles).

### Per-frame flow

1. **Longitudinal integration** (analytical) — `speed += accel·dt` on throttle, `-= brakeDecel·dt` on brake, or friction decay toward zero; clamped to `[maxReverse, effectiveMaxSpeed]`. Boost (`triggerBoost`/`tickBoost`, `Simulation.cpp:10`) raises `effectiveMaxSpeed` for a hold period then a 1-second linear release.
2. **`Simulation::sampleTrack`** (`Simulation.cpp:140`, analytical) — finds the nearest centerline `Frame` across every path/segment (brute-force O(paths × segments), no spatial index; called up to 3× per step). Returns a blended `Sample` (position, tangent, edgeRight, normal, lateral bounds `sLeft`/`sRight`). Notably this **does not carry reservation, roll, width, or thickness data** — the physics corridor is blind to reservation voids; a car in analytic mode can drive off a reservation's edge, since the synthetic one-way-railed mesh region that used to prevent this was removed (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2, no interim replacement).
3. **Collision-surface pre-probe** (geometry-based, only if the host supplied one) — while grounded, the steering axis is refined by `TrackCollisionSurface::nearestAlongAxis(pos, up, 4.0)`, a raycast against the exported triangle mesh.
4. **One of two motion branches** (analytic mode only; mesh mode is BVH-only throughout, see `stepMeshPhysics`):
   - **Airborne** (`Ship.cpp:61`) — always falls back to the fully analytical `curvedSurfaceFrame` corridor surface for landing; the mesh-region rail-clip/landing branch was removed along with `MeshRegion`.
   - **On the corridor, moving/parked** (`Ship.cpp:165`) — analytical: `curvedSurfaceFrame` wall collision with restitution gated on `dot(velocity, wallNormal) > 0` (so a narrowing road doesn't drain speed every frame it merely brushes the limit). The reservation one-way-rail collision that used to run first here was removed with `MeshRegion`.
5. **Final collision-surface pass** (`Ship.cpp:298`, geometry-based, only if the host supplied one) — a `sweep`/`nearestAlongAxis` raycast against the exported triangle mesh **overrides** the analytical position/normal entirely when it hits. This is what makes the shipped game's contact model materially different from the editor's/`track_runner`'s pure-analytical one — see [Limitations](#limitations).
6. **Zones/triggers/respawn** — zone detection only while grounded; trigger crossing detection (segment/plane test against each trigger's quad) always; automatic respawn when airborne and below `Track::trackFloorY`.

### Key constants

From `TrackCore.hpp`'s `TrackCore`/`Consts` namespaces and `Ship.hpp`'s `Physics` defaults:

| Constant | Value | Meaning |
|---|---|---|
| `Physics::maxSpeed` / `accel` / `brakeDecel` / `friction` | 140 / 71 / 115 / 55 | m/s, m/s², m/s², m/s² |
| `Physics::turnRate` / `grip` | 2.4 rad/s / 3.2 | Steering response |
| `Physics::wallRestitution` / `weight` / `gravity` | 0.75 / 1000 / 60 | Bounce, mass (scales restitution/speed-retention), m/s² |
| `TrackCore::COLLISION_WALL_MARGIN` | 1.8 m | Inset from `sLeft`/`sRight` |
| `Consts::SURFACE_SNAP_UP` | 3.0 m | Vestigial — was the max snap-up when leaving a mesh region onto the corridor; unused since `MeshRegion`'s removal (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2) |
| `Consts::CORRIDOR_ALONG_TOL` / `SEGMENT_ALONG_TOL` | 8.0 / 0.5 m | Along-track tolerance for corridor/segment membership |
| `Consts::RESPAWN_FALL_DEPTH` | 100 m | Below `trackFloorY` triggers auto-respawn |
| `Consts::MAX_PHYSICS_STEP` | 1/120 s | Sub-step size (see below) |
| `Consts::MIN_LAUNCH_UPWARD_SPEED` | 30 m/s | Floor for a jump-zone launch |

`1 world unit = 1 metre` (`docs/glossary.md`); `maxSpeed` 140 ⇒ 504 km/h.

## GameSession / ShipFactory / StartGrid

The native session layer a real host drives, above raw `Ship::step`:

- **`ControlIntent`** (`GameSession.hpp:18`) — `throttle`, `brake`, `steer`, `respawn`; translated from platform input outside `core`.
- **`GameSession::step`** (`GameSession.cpp:42`) — clamps `dt` to `MAX_FRAME_DELTA` (0.05s), then splits it into equal sub-steps of at most `Consts::MAX_PHYSICS_STEP` (1/120s) each, calling `Ship::step` once per sub-step. An automatic respawn (fell off-track) breaks the remaining sub-steps for that ship. Emits `GameEvent`s (`TriggerFired`, `CheckpointAccepted`, `LapCompleted`, `Respawned`, `RailHit`) via `events()`.
- **`ShipFactory`** (`ShipFactory.cpp`) — `applyHandling` copies the track's 4 authorable handling fields (converting `turnSpeed` degrees/s → `turnRate` radians/s); `createRaceState` partitions baked checkpoints into intermediates + the first `role="finish"`; `buildRoster` builds a full ship roster on the starting grid.
- **`StartGrid`** (`StartGrid.cpp`) — `DEFAULT_SHIP_COUNT = 8`, two-column staggered layout. `startingGridPoses` places each ship analytically via `curvedSurfaceFrame`, then runs 3 fixed-point settle iterations (`sampleTrack` → `projectToSurface` → `curvedSurfaceFrame`) so an idle ship doesn't creep on its first frames.

## Numeric contracts and golden-trace testing

`cpp/test-data/` (a sibling of `core/`) is a fixed, committed regression corpus with **no in-repo regeneration tool** — treat it as append-only. Two layers, replayed by the `parity`/`raw_parity`/`track_tests`/`random_geometry_parity` CTest targets:

- **Baked-world traces** (`traces/`) — 4000 steps of pure runtime math, `atol=rtol=1e-12`, observed worst deviation ~1 ULP.
- **Raw-track traces** (`traces/raw/`, `traces/raw-session/`) — 1116 steps that force independent load/bake/compile in C++, looser tolerance (ratio gate 0.1). Discrete state (surface IDs, rail hits, airborne/boost/trigger/checkpoint/lap/respawn) must match **exactly**.

Practical effect: several functions are pinned to a specific formula *because of this corpus*, not purely for correctness (e.g. `TrackCore::clamp`'s exact boundary behavior, `Evaluator::eval`'s zero-offset bypass). Reordering floating-point operations in the corridor math is a breaking change; see `cpp/core/CLAUDE.md` before touching any of it.

## Limitations

- **The exported-collision-mesh contact model is opt-in and host-supplied.** Only `cpp/tungsten-monoxide` populates `Track::collisionSurface`; every other consumer (editor, `track_runner`, this library's own tests) runs on the fully analytical corridor model. The two are close but not identical — see `docs/tungsten-monoxide.md`.
- **The collision BVH is not used for rendering.** It exists solely for physics (see "Collision BVH: construction and consumers" above); there is no view-frustum or occlusion culling anywhere in this codebase, against the BVH or otherwise. Every baked `GeometryBatch` is rendered unconditionally.
- **`physics.up` is frozen at spawn.** Written only by `placeShipAtPose` and never updated during a run (a deliberate, golden-trace-pinned characteristic — verified bit-identical across a full run in the fixture corpus). `Ship::renderNormal` exists as a live, per-frame-correct substitute for renderers; see `Ship.hpp`.
- **`sampleTrack` has no spatial index** — a full scan of every segment of every path, per call, up to 3 calls per `Ship::step`. Cost scales with total track length × ship count.
- **Self-intersection collapse is worst-case O(N³)** per bake (see above); `detectSelfIntersections=false` skips only the detection half, and an empty `selfIntersections` list is indistinguishable from "genuinely none."
- **The physics corridor is blind to reservations** — `Sample` carries no reservation data, and (since `MeshRegion`'s removal, `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2, no interim replacement) there is no longer any collision fallback either: a car in analytic mode can drive straight through/off a reservation void.
- **Zone effect `"startGrid"` is authored but inert** at runtime (see above).
