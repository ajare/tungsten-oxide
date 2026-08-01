# Drivable mesh objects — implementation plan

Branch: `drivable-mesh-objects` (not yet created)

## Goal

Let an author import an arbitrary externally modeled 3D mesh, place one or
more instances of it in a track with full 6-DOF transforms, and have a ship
drive on it with the same physics used anywhere else on the track — mesh
mode (`Simulation::meshPhysicsEnabled_`, `Ship::stepMeshPhysics`,
`docs/MESH_PHYSICS_PLAN.md`). This entity, **drivable mesh object**,
entirely replaces today's flat, 2D-footprint **Mesh region**
(`MeshAsset`/`MeshPlacement`/`MeshRegion`/`MeshFloorTriangle`) — including
the Capped central-reservation special case — rather than coexisting with
it.

This supersedes the prior requirements-analysis pass on this feature; all
open decisions from that pass have been resolved below and are not
re-litigated here.

## Decisions locked in (see conversation record, not repeated here in detail)

- **Physics mode.** Any track containing a drivable mesh object placement
  forces mesh mode; the analytic/mesh debug toggle is disabled/hidden for
  such tracks. Analytic mode never supports drivable mesh objects — this is
  structural (its ground query is a single-valued height function of
  `(x, z)`), not a deferred feature.
- **Mesh regions are removed, not kept alongside.** `MeshAsset`,
  `MeshVertex`, `MeshPlacement`, `MeshRegion`, `MeshFloorTriangle`, the
  Willpower.Geometry 2D-polygon compile path for them, and the Capped
  central-reservation's special-cased curved-floor authoring all go away.
  A central reservation becomes an ordinary drivable mesh object placement.
  Hard break: old track JSON using Mesh regions fails to load with a clear
  error; there is no migration tool and no auto-upcast.
- **Mesh source storage.** Drivable mesh object geometry lives in an
  external `.mppmodel` resource, referenced by id from track JSON — never
  embedded in track JSON.
- **Import pipeline.** `model-tool` (`cpp/model-tool`) gains a track-aware
  export mode that is the single source of truth for a sub-mesh's collision
  flag. The editor does not get its own per-sub-mesh flag inspector.
- **Collision flag representation.** A per-named-sub-mesh **orthogonal**
  flag (collidable vs. decorative), independent of `GeometryKind` — not a
  new enum value per combination. Render/export classification still needs
  one new `GeometryKind` (`MeshObjectSurface`) for drivable-mesh-object
  batches generally; the collidable flag is an *additional* gate on top of
  that kind when building `TrackCollisionSurface`, not a replacement for
  kind-based classification.
- **Normals.** Auto-recomputed at import from winding order, computed
  smoothly *across* a model's sub-meshes (not isolated per sub-mesh) —
  never trust the source file's normals. No manifold/topology validation
  gate; malformed input is accepted as-is once normals are fixed up.
- **Placement translation.** The top-down canvas gains a global projection
  mode: **Top-down** (view dir `Y = -1`, drag moves X/Z), **Front** (view
  dir `Z = -1`, drag moves X/Y), **Side** (view dir `X = 1`, drag moves
  Y/Z). This is a canvas-wide mode (like `EditMode`), not scoped to
  drivable mesh objects — it applies to every entity's editing.
- **Placement rotation.** Shift+drag-to-rotate is generalized per mode:
  top-down produces yaw, front produces pitch, side produces roll. This is
  new and specific to drivable mesh objects, which are the only entity
  needing pitch/roll (Mesh regions, the only other rotatable entity, are
  being removed).
- **ElevationView is retired.** `ElevationView.cpp`/`.hpp` (the path-profile
  arc-length side panel) is removed; Front canvas mode takes over all
  height editing, including for path control points. This trades away
  arc-length-accurate x-axis positioning for path point height edits —
  accepted knowingly, not an oversight.
- **Selected-mesh rendering in Front/Side modes.** When a drivable mesh
  object is selected, render the mesh (or its bounding box, if rendering
  the actual geometry there is too costly/complex) in Front/Side canvas
  modes, scaling with the view's Y axis, alongside whatever path is also
  shown.
- **Host surface.** Zones/Triggers extend `Host surface` to include a
  drivable mesh object placement, replacing the capability Mesh regions
  provided (boost pads etc. on non-path surfaces).
- **Performance.** No triangle budget set now; measure against real
  content once it exists, no enforcement added this branch.
- **Regression.** Add mesh-mode golden-trace fixtures to
  `cpp/test-data/traces` as part of this work — mesh mode has had zero
  regression coverage since `mesh-based-ship-physics`, and this branch
  makes it load-bearing for a headline feature, not just a debug toggle.
- **All physics testing is headless.** Never validate ground/wall/airborne
  behavior by launching the Launcher/game and driving interactively —
  injecting reliable scripted input that way is difficult and the results
  aren't reproducible. `mesh-based-ship-physics` already established the
  pattern (see commit `6fe16f1`, "Fix two more mesh-mode bugs found via
  headless investigation"): a headless tool loads a real track resource,
  builds the collision BVH exactly as `Map::load` would, drives a ship
  through `GameSession` with scripted throttle/steer input, and logs
  position/speed/airborne state every frame — no window, no GUI, no manual
  input, fully reproducible. That tool (`cpp/app/tools/mesh_physics_diag.cpp`)
  was written as a throwaway and removed afterward; this branch needs the
  same capability repeatedly (Milestones 6–8) and should make it a
  permanent fixture rather than reinventing it each time — see Milestone
  6.0.

Work one step at a time. After each step: build, run `ctest --test-dir
cpp/build -C Release --output-on-failure`, and commit before moving to the
next step.

---

## Milestone 1 — Canvas projection-mode toggle (prerequisite)

Drivable mesh object placement has no usable Y/Z editing without this; it
lands first, as `MESH_PHYSICS_PLAN.md` front-loaded its own collision-surface
invariant milestone before the physics-mode work.

**1.1 — Add `ProjectionMode` state** — done (`a07878f`)
- Files: `cpp/editor/include/EditorState.hpp`, `cpp/editor/main.cpp` (toolbar
  combo + `1`/`2`/`3` shortcuts live alongside `E`/`C`/`R`'s existing wiring
  here, not in `TopDownCanvas.cpp`).
- Added `enum class ProjectionMode { TopDown, Front, Side }` alongside the
  existing `EditMode`, with a getter/setter on `EditorState`. Default
  `TopDown`, matching today's only behavior; mode is otherwise unused until
  1.2.
- Test: `ctest` (no behavior change yet — mode unused). Committed.

**1.2 — Generalize drag-to-move math** — done (see commit after this plan
update)
- Files: `cpp/editor/include/EditorState.hpp` (`planeCoords`/`setPlaneCoords`,
  generalizing `withinPick`/`hitTestPosition`/`selectPositionAt`/
  `hoverTestPosition`/`dragSelectedTo` to read/write the active
  `projectionMode_`'s two axes instead of hardcoded x/z), `cpp/editor/src/TopDownCanvas.cpp`
  (a matching free `planeCoords` used by `drawAuthoredPositionPoints` so
  rendering stays in step with hit-testing/dragging).
- `TopDown` → (X, Z) as today, `Front` → (X, Y), `Side` → (Y, Z); the third
  axis outside the active plane is left untouched by a drag. `view.screenToWorld`/
  `worldToScreen` themselves are unchanged (they're plane-agnostic screen<->2D
  math); only the meaning of the two axes they carry changes with the mode.
  Scoped to Position points only, per this step's original scope — weld/
  join-drag/rubber-band endpoint hit-testing (`hitTestOpenEndpoint`) and every
  non-point hit-test (zones/triggers/road) are still X/Z-only, since Mesh
  regions are being removed in Milestone 2 and nothing else needs Front/Side
  yet.
- Test: `ctest` (all 7 suites pass, including main.cpp's built-in M3 smoke
  check exercising `dragSelectedTo` in default TopDown mode). Manual
  interactive drag verification in Front/Side modes was not performed — no
  GUI session available in this environment; a smoke launch confirmed the
  editor starts and its self-checks pass, but on-canvas Front/Side dragging
  should get a real manual pass before relying on it further. Committed.

**1.3 — Generalize shift+drag-to-rotate**
- File: `cpp/editor/src/TopDownCanvas.cpp`.
- Today's rotate gesture (Mesh-region-only, `meshRotating_`, angle
  convention `atan2(dz, dx)` from the placement's `(x, z)`) is being
  removed with Mesh regions in Milestone 2. Rebuild the gesture generically
  here so Milestone 5 can attach it to drivable mesh object placements:
  `TopDown` → yaw (`atan2(dz, dx)`, matching today's convention), `Front` →
  pitch, `Side` → roll. No entity uses this yet after Milestone 2 removes
  Mesh regions and before Milestone 3/5 add drivable mesh objects — land
  the gesture plumbing here, wire it to a real entity in Milestone 5.
- Test: `ctest`. Commit.

**1.4 — Retire `ElevationView`**
- Files: `cpp/editor/src/ElevationView.cpp`, `cpp/editor/include/ElevationView.hpp`,
  `cpp/editor/main.cpp` (panel wiring), `cpp/editor/src/PropertiesPanel.cpp`,
  `cpp/editor/src/TopDownCanvas.cpp` (`showPositionPoints` cross-wiring).
- Remove the panel; confirm Front canvas mode (1.2) reaches path control
  point height editing (today `ElevationView`'s only consumer) end to end,
  including right-click-to-insert parity (`showPositionPoints` gating) if
  still relevant in the new mode.
- Test: `ctest`; manual check that path point height editing still works
  via Front mode. Commit.

**1.5 — Update editor conventions doc**
- File: `cpp/editor/CLAUDE.md`.
- Document `ProjectionMode`, the generalized drag/rotate gestures per mode,
  and `ElevationView`'s removal, following the existing terse
  bullet-per-convention style.
- Commit (docs-only).

---

## Milestone 2 — Remove 2D Mesh regions

Removed *before* drivable mesh objects are added, per the "hard break, no
coexistence" decision — this avoids ever having both entities live in the
schema/codebase simultaneously.

**2.1 — Remove core compiled types**
- Files: `cpp/core/include/TrackMesh.hpp`, `cpp/core/src/TrackMesh.cpp`.
- Remove `MeshRegion`, `MeshFloorTriangle`, `MeshTriangle`/`MeshPolygon`/
  `MeshRail` and their Willpower.Geometry-driven compile path
  (`addGeometry`, `elevationAt`, `slideAlongRails`). Remove `Track::meshRegions`.
- Test: expect widespread compile breakage downstream — do not fix
  call sites yet if the change is large; proceed to 2.2–2.4 first, then
  build.

**2.2 — Remove authored schema types and editor UI**
- Files: `cpp/editor/include/EditorTrackDefinition.hpp`,
  `cpp/editor/src/MeshPanel.cpp` (remove entirely),
  `cpp/editor/src/TopDownCanvas.cpp` (remove mesh-region hit-testing,
  drag, and the old `meshRotating_` shift+drag path superseded by 1.3),
  `cpp/editor/src/EditorState.*` (`importMeshAsset`/`importMeshFromJsonText`/
  `placeMeshAsset`).
- Remove `MeshAsset`, `MeshVertex`, `MeshPlacement` and the geometry-js
  JSON import path (toolbar Import, canvas right-click "Paste Mesh").
- Test: build editor + core; fix remaining call sites. `ctest`. Commit.

**2.3 — Remove Capped-reservation special-case authoring**
- Files: wherever central-reservation authoring UI/logic lives (surfaced
  during 2.1/2.2's build fixups — likely `MeshPanel.cpp`/`EditorState`
  reservation-specific branches).
- Central reservations are not replaced with an equivalent parametric
  entity; they become ordinary drivable mesh object placements once
  Milestone 3/5 exist. No interim replacement needed here.
- Test: `ctest`. Commit.

**2.4 — Drop Mesh-region host-surface type**
- Files: wherever `Host surface` is modeled for Zones/Triggers (per
  `docs/UBIQUITOUS_LANGUAGE.md`, "Zone"/"Trigger"/"Host surface" entries —
  locate the concrete type, likely near `Track.hpp`/zone-trigger schema
  types).
- Remove the Mesh-region host-surface variant. Do not add the
  drivable-mesh-object host-surface variant yet — that's Milestone 3.5,
  once the entity exists. Existing Path-hosted zones/triggers are
  unaffected.
- Test: `ctest`. Commit.

**2.5 — Schema version / hard-break loader error**
- Files: `cpp/core/src/TrackLoader.cpp`, `cpp/editor` schema-load path.
- Bump schema version (per repo convention — check how schema 10 → 11 was
  introduced for precedent). Old tracks whose JSON contains `meshAssets`/
  `meshes` fields fail to load with a clear, actionable error naming the
  removed feature — not a silent-drop or generic parse failure.
- Test: load a fixture with the old fields present, confirm the explicit
  error. `ctest`. Commit.

**2.6 — Update fixtures and docs**
- Files: `cpp/test-data/fixtures` (remove/replace any fixture exercising
  Mesh regions — note per repo convention this corpus is normally
  append-only; removal here is justified by the hard-break decision, but
  confirm no fixture is *also* covering unrelated behavior before deleting
  it wholesale), `docs/UBIQUITOUS_LANGUAGE.md` (remove **Mesh region**;
  update **Region rail**, **Ledge**, **Mesh section**, **Platform
  sequence**, **Launch ramp**, **Host surface** entries that currently
  reference it — decide per-term whether the concept transfers to
  **Drivable mesh object** or is retired with no replacement),
  `docs/core.md`, `docs/editor.md`.
- Test: full `ctest`. Commit.

---

## Milestone 3 — Drivable mesh object schema & core compile pipeline

**3.1 — Authored placement type**
- File: `cpp/editor/include/EditorTrackDefinition.hpp`.
- New type, e.g. `DrivableMeshObjectPlacement { modelId; position{x,y,z};
  rotation{yaw,pitch,roll}; scale; name/id }`, referencing an external
  `.mppmodel` by `modelId` (per the storage decision) rather than embedding
  geometry.
- Test: `ctest` (schema addition only, unused). Commit.

**3.2 — Runtime compiled representation**
- Files: `cpp/core/include/TrackMesh.hpp` (or a new header, since
  `MeshRegion` is gone), `cpp/core/src/TrackMesh.cpp`.
- Load the referenced `.mppmodel`'s sub-meshes, apply the placement's 6-DOF
  transform (position/rotation/scale) to positions and normals, producing
  world-space triangle data per instance. This is instancing, not
  triangulation — the source mesh arrives already triangulated from
  `model-tool` (Milestone 4), unlike the old Willpower.Geometry 2D-polygon
  compile path this replaces.
- Test: `ctest`. Commit.

**3.3 — Per-sub-mesh collidable flag → BVH**
- Files: `cpp/core/src/TrackMesh.cpp`, `cpp/tungsten-monoxide/src/Map.cpp`
  (`buildCollisionTriangles`/`gameplayKind()`).
- Only sub-meshes flagged collidable (per Milestone 4's model-tool-authored
  flag) contribute `CollisionTriangle`s to `TrackCollisionSurface`;
  non-collidable sub-meshes still compile to renderable geometry
  (`GeometryKind::MeshObjectSurface`, new) but are excluded from
  `Map.cpp`'s BVH build. `gameplayKind()`'s existing kind-based filter gets
  an additional flag check specifically for this kind — not a new enum
  value per drivable/decorative combination.
- Test: `ctest`; a fixture with one collidable and one decorative sub-mesh
  in the same model confirms only the flagged one lands in
  `collisionTriangles`. Commit.

**3.4 — Export lock-step**
- File: `cpp/editor/src/MppModelExport.cpp`.
- Add `MeshObjectSurface` to the `<TrackMeshes>` selection set, keeping
  lock-step with 3.3's `gameplayKind()` change per the existing "these two
  must stay in lock-step" invariant.
- Test: `ctest`; export/reload round trip. Commit.

**3.5 — Host-surface support**
- Files: wherever 2.4 removed the Mesh-region host-surface variant.
- Add a drivable-mesh-object-placement host-surface variant so
  Zones/Triggers can attach to a placement by id, restoring the capability
  Mesh regions provided.
- Test: `ctest`; a zone hosted on a drivable mesh object placement loads
  and resolves its host correctly. Commit.

**3.6 — Loader fixtures**
- File: `cpp/test-data/fixtures`.
- New fixture(s): a track referencing a drivable mesh object placement
  (validates 3.1–3.4's schema/compile path end to end).
- Test: `ctest`. Commit.

---

## Milestone 4 — `model-tool` track-aware export

**4.1 — Track-aware export mode**
- Files: `cpp/model-tool/src/AssImpImport.cpp`, `ModelResourceExport.cpp`,
  `MppSave.cpp` (per `docs/model-tool.md`).
- Add a mode (flag or subcommand) that, alongside today's render-only
  `.mppmodel` output, tags each named sub-mesh with the orthogonal
  collidable flag Milestone 3.3 consumes.
- Test: `model-tool`'s own test coverage if any (check its CMake target);
  otherwise a manual export + core-loader smoke test. `ctest`. Commit.

**4.2 — Normal recomputation**
- Same files as 4.1.
- Recompute vertex normals from winding order at export, smoothly *across*
  a model's sub-meshes (shared vertices at sub-mesh boundaries get averaged
  normals spanning both), rather than trusting Assimp's imported normals or
  computing per-sub-mesh in isolation.
- Test: export a multi-sub-mesh asset with a shared edge, confirm normals
  are continuous across the seam (visual or numeric check). `ctest`. Commit.

**4.3 — Flag authoring surface**
- Same files, plus whatever CLI argument parsing `model-tool` already has.
- Expose a way to mark named sub-meshes collidable vs. decorative at
  export time (source: either a convention in the input file — e.g. a
  naming pattern Assimp preserves — or explicit CLI arguments per
  sub-mesh name). Pick whichever fits `model-tool`'s existing CLI
  conventions; document the choice in `docs/model-tool.md`.
- Test: `ctest`. Commit.

---

## Milestone 5 — Editor placement UI

**5.1 — Asset library / picker**
- Files: new panel replacing `MeshPanel.cpp`'s role, `EditorState.*`.
- Browse previously imported `.mppmodel` drivable-mesh-object sources by
  `modelId`; pick one to begin placement.
- Test: `ctest`; manual browse/pick smoke test. Commit.

**5.2 — Placement via canvas projection modes**
- File: `cpp/editor/src/TopDownCanvas.cpp`.
- Wire drivable mesh object placements into Milestone 1's generalized
  drag-to-move (1.2) and shift+drag-to-rotate (1.3): Top-down drag moves
  X/Z and rotates yaw, Front drag moves X/Y and rotates pitch, Side drag
  moves Y/Z and rotates roll.
- Test: `ctest`; manual placement in all three modes. Commit.

**5.3 — Selected-mesh rendering in Front/Side modes**
- File: `cpp/editor/src/TopDownCanvas.cpp` (or a Front/Side-specific render
  path).
- When a drivable mesh object is selected, render it (or its bounding box,
  if full-mesh rendering there is too costly) alongside any shown path,
  scaling with the view's Y axis.
- Test: `ctest`; manual visual check. Commit.

**5.4 — Properties panel fields**
- File: `cpp/editor/src/PropertiesPanel.cpp`.
- Numeric fields for position/rotation/scale, `modelId` reference display,
  name/id.
- Test: `ctest`. Commit.

**5.5 — Undo/redo integration**
- File: wherever 5.2's drag/rotate gestures live.
- `pushUndo()` once per gesture start, per `cpp/editor/CLAUDE.md`'s existing
  convention (continuous drag = one undo step).
- Test: `ctest`; manual undo/redo check across a drag and a rotate. Commit.

---

## Milestone 6 — Mesh-mode physics robustness for arbitrary geometry

**6.0 — Permanent headless physics diagnostic tool**
- Files: new, e.g. `cpp/app/tools/mesh_physics_diag.cpp` (revive/rebuild the
  removed `mesh-based-ship-physics`-branch tool of the same name, permanent
  this time), wired into `cpp/app/CMakeLists.txt`.
- Loads a real track resource (`.mppmodel` + JSON), builds
  `TrackCollisionSurface` exactly as `Map::load` does (note: `track_runner`
  deliberately leaves `Track::collisionSurface` null and stays
  analytical-only, per `docs/core.md`'s "Limitations" section — this tool
  cannot just be `track_runner` as it stands today; either extend
  `track_runner` itself to optionally populate the collision surface, or
  keep this as a separate `cpp/app/tools` binary that links the same
  `Map.cpp` BVH-building code path), drives a ship through `GameSession`
  with scripted throttle/steer input (a simple fixed or file-driven input
  script is sufficient — no interactive input, no window), and logs
  position/velocity/normal/airborne-state every frame to stdout or a file
  for diffing.
- Test: run it against an existing track with no drivable mesh objects in
  mesh mode, confirm output is stable/reproducible run-to-run. `ctest`
  (build only — this tool has no ctest target of its own unless folded into
  7.2). Commit.

**6.1 — Build a genuinely 3D validation asset**
- Author (or source) a test mesh with real 3D topology a flat Mesh region
  never exercised — a tunnel, a loop, or an overhang — and place it in a
  scratch track via the now-working Milestones 1–5 pipeline.
- No code change; this step produces the asset/track used by 6.2–6.3.

**6.2 — Fix ground/wall/airborne issues found**
- File: `cpp/core/src/Ship.cpp` (`stepMeshPhysics`).
- Drive the 6.1 asset through 6.0's headless tool with a scripted
  input pattern that exercises the new topology (steer across the tunnel/
  loop/overhang, not just straight through); `nearestAlongAxis`'s
  single-hit assumption and `sweepWall`'s floor-vs-wall
  `|dot(normal, UP)| > 0.5` threshold are the likeliest failure points on
  genuinely non-planar-local geometry (per the prior requirements
  analysis, PR2). Fix what's found; this step's scope is discovered from
  6.1's logged output, not fully specified up front.
- Test: `ctest` (existing golden traces still pass — this is mesh-mode-only
  behavior); re-run 6.0's tool against the 6.1 asset and confirm
  ground/wall/airborne state stays sane frame-to-frame (no NaN, no
  discontinuous teleport, no stuck-airborne loop). Commit.

**6.3 — Non-contiguous entry validation**
- Confirm a ship can transition onto a drivable mesh object not contiguous
  with the path network (e.g. reached only via jump/ramp) using the
  existing airborne/landing BVH raycast path — no new state expected, but
  confirm against 6.1's asset via 6.0's headless tool (scripted input that
  launches the ship at the gap) rather than assuming.
- Test: headless check via 6.0's tool; `ctest`. Commit if any fix was
  needed.

---

## Milestone 7 — Mesh-mode regression coverage

**7.1 — Golden trace fixture(s)**
- File: `cpp/test-data/traces`.
- Capture at least one trace covering a track with a drivable mesh object
  placement, driven in mesh mode (mandatory per the force-mesh-mode
  decision — Milestone 8.1 makes this the only mode such a track can run
  in anyway), using 6.0's headless tool with a fixed scripted input script
  (not interactive play) so the capture is deterministic and re-runnable —
  same reasoning as the existing golden-trace corpus, which is likewise
  captured/replayed headlessly.
- Test: the new trace must be reproducible — run the capture twice via
  6.0's tool, confirm byte-identical output, before committing it as
  golden.

**7.2 — Wire mesh-mode into the trace-replay test target**
- File: `cpp/core/CMakeLists.txt` (`add_test()` entries).
- Today's trace tests replay in whatever mode the trace was captured in;
  confirm mesh-mode traces are actually exercised (not skipped/ignored by
  a mode-specific filter) and fail loudly on divergence, same as analytic
  traces.
- Test: `ctest --test-dir cpp/build -C Release --output-on-failure`. Commit.

---

## Milestone 8 — Force-mesh-mode enforcement & end-to-end validation

**8.1 — Force mesh mode for tracks with drivable mesh objects**
- Files: `cpp/core/src/Simulation.cpp`/`GameSession.cpp` (load path),
  `cpp/tungsten-monoxide/src/StatePlayTungstenMonoxide.cpp` (debug toggle).
- At load, if `Track` contains any drivable mesh object placement, force
  `meshPhysicsEnabled = true` and disable/hide the debug checkbox for that
  session (per the FR5 decision — no silent fall-through, no reject-at-load).
- Test: `ctest`; a headless load-only check (via 6.0's tool or a small unit
  test) confirming `meshPhysicsEnabled` reads `true` after loading such a
  track. The debug-checkbox-hidden UI state itself is the one piece of this
  step that isn't physics and can reasonably get a quick manual glance in
  the editor/game UI, since it's a visibility check, not input-driven
  simulation. Commit.

**8.2 — Full validation pass (headless)**
- Author a track (in the editor, as normal) using drivable mesh objects
  for: a replacement central reservation, a standalone platform/ramp, and
  a zone/trigger hosted on one of them. Export it, then drive it end to
  end via 6.0's headless tool with a scripted input script covering a full
  lap. Confirm from the logged output: continuous ground contact where
  expected, correct wall/rail bounce, at least one airborne moment that
  lands cleanly, and the hosted zone/trigger firing at the expected frame.
- Fix anything broken; re-run headless; `ctest`; commit.

**8.3 — Full regression pass**
- Full `ctest` run: analytic-mode golden traces for tracks with no drivable
  mesh objects unaffected; new mesh-mode traces (Milestone 7) green.
- Commit (likely doc/plan cleanup only, unless 8.2 surfaced fixes not yet
  committed).

---

## Deferred / explicitly out of scope

- **Migration tooling** for old Mesh-region tracks — hard break, by
  decision; not revisited unless real content turns out to need it.
- **Triangle budget / BVH performance limits** — measure against real
  authored content once it exists; no enforcement lands on this branch.
- **Non-uniform scale, mesh deformation/animation, per-instance material
  overrides** — not requested; not built.
- **Manifold/topology validation gate at import** — normals are
  auto-recomputed, but malformed topology is otherwise accepted as-is.
