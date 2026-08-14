# Mesh-based ship physics — implementation plan

Branch: `mesh-based-ship-physics`

## Goal

Add a second, fully independent ship-physics mode that determines ground
height/normal, wall/lateral collision, and airborne/landing purely from the
baked triangle mesh + BVH (`TrackCollisionSurface`), instead of the existing
analytic curve/corridor math (`Simulation::sampleTrack` /`projectToSurface`
/`curvedSurfaceFrame`, `MeshRegion::elevationAt`/`slideAlongRails`,
`meshRegions` bounds checks). Toggleable live, mid-race, from the existing
in-game debug ImGui overlay in `StatePlayTungstenMonoxide`. Analytic mode
was the default at the time this plan was written and remains the only mode
covered by the golden trace regression suite (`cpp/test-data/traces`) — see
"Decisions locked in" below for the point where the default flipped.

Decisions locked in (see conversation record, not repeated here in detail):
- Global flag, not per-ship; live-toggleable.
- Collision surface (BVH) becomes a mandatory invariant for any drivable
  track: editor export enforces it, loader hard-errors on old-schema tracks
  without one.
- BVH triangle sources widen from today's `PathSurface`/`MeshSurface`-only
  to also include `ReservationWall`, `PathRail`, `MeshRail`. `PathShell`
  stays render-only, excluded from collision.
- `MeshRegion`'s curved floor (`MeshFloorTriangle`) gets a real triangle
  export path (today it's analytic/query-only) so it lands in the BVH too.
- Mesh mode is airborne/landing-pure (BVH raycast only, no
  `meshRegions`/corridor bounds bookkeeping).
- Default mode was analytic at merge, with mesh mode opt-in via the debug
  checkbox — **superseded**: `Simulation::meshPhysicsEnabled_` now defaults
  to `true` (mesh physics is the shipped default; analytic mode is the
  live-toggleable fallback). The golden trace regression suite still only
  covers analytic mode, so `parity`/`raw_parity`/`raw_session_*_parity` pin
  their `Simulation`/`GameSession` instances to analytic mode explicitly
  rather than relying on the (now-flipped) default.

Work one step at a time. After each step: build, run `ctest --test-dir
cpp/build -C Release --output-on-failure`, and commit before moving to the
next step.

---

## Milestone 1 — Collision surface as a mandatory, fuller invariant

**1.1 — Emit real 3D geometry for `MeshRegion` curved floors**
- File: `cpp/core/src/TrackMesh.cpp` (`addGeometry`, ~171-200; consumer
  reference `elevationAt`, ~213-235; struct `MeshFloorTriangle`,
  `TrackMesh.hpp:33-37`).
- When `region.floor` is non-empty, build the `MeshSurface` batch from
  `region.floor` triangles instead of the flat scalar-`region.elevation`
  extrusion of `region.triangles`: synthesize 3D positions from
  `{points[i].x, heights[i], points[i].y}`, compute a face normal (or
  per-vertex averaged normal across adjacent floor triangles), fill
  UV/RGBA consistent with the existing `MeshSurface` convention. Keep the
  existing flat-extrusion path for regions with no floor data.
- Test: `ctest` (loader/mesh/geometry fixtures under `cpp/test-data/fixtures`
  should still pass since output topology for flat regions is unchanged).
  Commit.

**1.2 — Widen the collidable `GeometryKind` set**
- Files: `cpp/tungsten-monoxide/src/Map.cpp` (`gameplayKind()`, ~59-61),
  `cpp/editor/src/MppModelExport.cpp` (`<TrackMeshes>` emission loop,
  ~274-278), doc comment `cpp/editor/include/MppModelExport.hpp:78-84`.
- Add `ReservationWall`, `PathRail`, `MeshRail` to both the editor's
  `<TrackMeshes>` selection and `Map.cpp`'s `gameplayKind()` filter (these
  two must stay in lock-step — same set, same order of reasoning). Leave
  `PathShell` excluded from both.
- Test: `ctest`; re-export one bundled sample track resource (e.g. the
  `NewTrack2`/`model` files under `cpp/tungsten-monoxide/resources`) via the
  editor and confirm `Map::load` accepts it without the 1:1
  `expectedNames`/`TrackMeshes` mismatch error. Commit.

**1.3 — Enforce collision-surface presence at editor export time**
- File: `cpp/editor/src/TrackResourceSave.cpp` (~159-161, save/export
  sequence).
- Before writing the resource, require at least one collidable
  (`PathSurface`/`MeshSurface`/etc.) batch to exist in `bakedTrack.geometry`;
  refuse/fail the export otherwise with a clear message. This mirrors the
  existing runtime check at `Map.cpp:191-192` but moves the failure to
  authoring time.
- Test: manually attempt exporting a track with all drivable
  paths/mesh-regions removed and confirm export is refused; `ctest`. Commit.

**1.4 — Hard error on load for tracks with no collision surface**
- Files: `cpp/core/src/TrackLoader.cpp` (schema-version handling near line
  2; error pattern via `std::runtime_error` → `TrackLoadResult::error`,
  `Track.hpp:125`), `cpp/tungsten-monoxide/src/Map.cpp` (~170-193).
- Confirm/extend the existing `ResourceException` at `Map.cpp:191-192`
  (thrown when `collisionTriangles.empty()`) so it clearly covers the
  old-schema case (i.e. a track whose exported resource predates the
  `<TrackMeshes>` contract, or has no `PathSurface`/`MeshSurface`/etc.
  batches at all) with an explicit, actionable error message rather than a
  generic empty-collection failure.
- Test: load a deliberately stripped/old-schema fixture and confirm a hard
  error is thrown, not a silent empty-physics track; `ctest`. Commit.

---

## Milestone 2 — Mesh-driven physics as a live-toggleable alternate mode

**2.1 — Add a lateral/side query to `TrackCollisionSurface`**
- Files: `cpp/core/include/TrackCollision.hpp`, `cpp/core/src/TrackCollision.cpp`.
- Today's queries (`sweep`, `nearestAlongAxis`) are one-sided, road-facing.
  Add a query suited to wall/lateral detection (e.g. a bounded-distance
  raycast along an arbitrary axis, both-sides, returning nearest hit +
  interpolated normal) reusing the existing BVH traversal/`intersect()`
  Möller–Trumbore code — don't duplicate the BVH walk.
- Test: unit-level exercise if a test target exists for `TrackCollision`
  (check `cpp/core/CMakeLists.txt`); otherwise smoke-test via a small
  scratch call site. `ctest`. Commit.

**2.2 — Add the global mesh-physics-enabled flag**
- Likely home: `Simulation` (`cpp/core/include/Simulation.hpp`), since
  `Ship::step` already takes `Simulation&` and it's naturally
  session-global rather than per-ship. Default `false`.
- Test: `ctest` (no behavior change yet — flag unused). Commit.

**2.3 — Mesh-mode ground path in `Ship::step`**
- File: `cpp/core/src/Ship.cpp`.
- When the flag is set, bypass the plain analytic-corridor branch
  (~165-273: `sampleTrack`/`projectToSurface`/`curvedSurfaceFrame`) and the
  grounded-on-`meshRegion` branch (~122-164, `elevationAt`) entirely.
  Ground height/normal instead come from a BVH raycast (`sweep`/
  `nearestAlongAxis`, already used as the late override at ~294-320 — in
  mesh mode this becomes the *primary* source, not an after-the-fact
  correction).
- Test: `ctest` (analytic-mode traces must be byte-identical — mesh mode is
  gated behind the still-false-by-default flag). Commit.

**2.4 — Mesh-mode wall path in `Ship::step`**
- File: `cpp/core/src/Ship.cpp`.
- Replace corridor `sLeft`/`sRight` bounce and `slideAlongRails` calls (in
  mesh mode only) with side raycasts against the BVH using the query added
  in 2.1, now that walls/rails (`ReservationWall`/`PathRail`/`MeshRail`,
  from 1.2) are present in it.
- Test: `ctest`; manual drive-into-wall check once the toggle is wired
  (may land after 2.6 in practice — note dependency). Commit.

**2.5 — Mesh-mode airborne/landing (pure BVH)**
- File: `cpp/core/src/Ship.cpp` (~65-121 airborne-over-`meshRegions` loop).
- In mesh mode, replace this with a pure gravity/velocity-direction BVH
  raycast each tick; no `meshRegions` bounds or corridor checks. Landing =
  first BVH triangle hit within range.
- Test: `ctest`. Commit.

**2.6 — Wire the debug UI checkbox**
- File: `cpp/tungsten-monoxide/src/StatePlayTungstenMonoxide.cpp`
  (`_renderImGui`, "Debug" tab, ~540-551, alongside the existing
  `mShowTriggersDebug`/etc. checkboxes at ~543-546).
- Add a member bool + checkbox that live-flips the `Simulation`-level flag
  from 2.2 via `mGameSession` (see `renderShipPhysicsTab`, ~517-527, for
  how this file already reaches ship/session state).
- Test: build + manual smoke test — toggle live mid-drive, confirm no
  crash, ship stays roughly coherent across the flip. `ctest`. Commit.

**2.7 — End-to-end manual validation pass**
- Run the game (`track_runner` or the tungsten-monoxide play state), drive
  a full lap in analytic mode, flip to mesh mode mid-lap, drive a lap in
  mesh mode: check ground contact, walls/rails, a jump/airborne moment, and
  a `MeshRegion` (Capped reservation) area specifically, since that's the
  one with materially different geometry (1.1) between the two modes.
- Fix anything broken; `ctest`; commit.

**2.8 — Full regression pass**
- Full `ctest` run confirming analytic-mode golden traces are unaffected;
  confirm no new trace fixtures were required (per the decision that
  mesh-mode is out of formal regression scope for this branch).
- Commit (likely a no-op beyond plan doc cleanup, unless issues found in
  2.7 needed fixes not yet committed).
