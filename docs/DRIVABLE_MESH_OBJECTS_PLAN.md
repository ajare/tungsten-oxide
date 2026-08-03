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
  new enum value per combination. *(Superseded by the "`.mppmodel` loading
  is host-only" architecture note below Milestone 2: since `core` never
  produces geometry batches for a placement at all, the originally-planned
  `GeometryKind::MeshObjectSurface` doesn't apply — the flag lives directly
  in the referenced `.mppmodel`'s own per-sub-mesh metadata instead, read
  by the host. See Milestone 3.3.)*
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

## Architecture note added when starting Milestone 3: `.mppmodel` loading is host-only, never in `core`

`cpp/core` has no renderer, no image loader, and (confirmed while starting
Milestone 3.2) no model-loading dependency of any kind — the only existing
way to read a `.mppmodel` file is `mpp::ModelSerializer`, a MassivePolyPusher
engine class coupled to `mpp::ResourceManager`; linking that into `core`
would contradict its documented design. There is also no lightweight
from-scratch `.mppmodel` *reader* anywhere in the repo to reuse instead
(`cpp/editor/src/MppModelExport.cpp` is a from-scratch *writer* only).

Rather than build a new native reader, this branch keeps `.mppmodel` loading
exactly where it already lives: `cpp/tungsten-monoxide/src/Map.cpp`, the only
place any `.mppmodel` is loaded today, and the same place that already
populates `Track::collisionSurface` for every gameplay-relevant triangle on
the track — every other consumer (`core`, the editor, `track_runner`, this
library's own tests) already runs with `collisionSurface` null and no
geometry for the road's own collision mesh, so extending that same
"host-supplied, everyone else does without" split to drivable mesh objects
adds no new precedent, just more content going through it.

Concretely: `core` (`TrackDefinition.hpp`/`Track.hpp`) carries a drivable
mesh object placement as pure authored data — `modelId` plus its 6-DOF
transform — through loading/normalization/round-trip, and nothing else.
`Track::definition` already retains the normalized `TrackDefinition`
unmodified (`Track.hpp:90`), so no new compiled representation is needed for
the host to read placements back out. `Map.cpp` is the only place that ever
opens a placement's referenced `.mppmodel`, transforms its sub-meshes by the
placement's transform, and merges the result into its existing
`buildCollisionTriangles`/rendering pipeline. Zone/Trigger hosting on a
placement (3.5) stays core-side despite this: it only ever needed the
placement's own transform (exactly like the old flat 2D Mesh-region host did
with `x`/`z`/`rotation`), never the referenced model's actual triangles, so
it composes with host-supplied grounding the same way it always did — the
zone's footprint math is core's job, whether the ship is actually grounded
there comes from the BVH the host built.

One casualty of this: the `GeometryKind::MeshObjectSurface` render/export
kind named in the "Collision flag representation" decision above assumed
`core`/`TrackBake.cpp` would produce classified geometry batches for
placements the way it does for the road. Since `core` now never produces any
geometry for a placement at all, that kind doesn't apply as originally
scoped — the collidable/decorative flag lives entirely in the *referenced*
`.mppmodel`'s own per-sub-mesh metadata (written by `model-tool`, Milestone
4) for `Map.cpp` to read directly; Milestone 3.3 below reflects this instead
of introducing an unused `GeometryKind` value.

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

**1.2 follow-up — Full canvas generalization** — done
- User-reported gap after 1.2 landed: Front/Side still rendered the road,
  grid-fit bounds, zones/triggers/physics points/crossings/start marker/
  roll-width-cross-section handles in the XZ plane — only position points
  had moved, so they floated disconnected from everything else. Explicitly
  scoped up to "full canvas generalization" (vs. road-only or render-only)
  after asking the user.
- Files: `cpp/editor/src/TopDownCanvas.cpp` (the bulk of it), `cpp/editor/include/EditorState.hpp`
  (`createModeClick`/`extendOpenPathFromEndpoint`/`hitTestOpenEndpoint` now
  route through `planeCoords`/`setPlaneCoords` too, closing the gap 1.2 left
  in weld/join-drag and Create-mode point placement).
- `WorldFrame2D`/`sampleCenterlineAtG` now carry every field (`pos`,
  `edgeRight`, `h`, `tangent`) pre-projected into the active plane rather
  than always XZ; because plane projection is linear, offsetting by a
  projected axis (width/roll handle offsets, zone lateral offsets) gives the
  same result as projecting a full-3D offset would — no separate per-caller
  handling needed. A new `rotateAngleDeg`-style helper, `worldToScreenPlane`,
  is the one conversion every render call site now uses instead of
  `view.worldToScreen(v.x, v.z)` directly. Generalized: road ribbon
  (`drawBakedPath`), view auto-fit bounds (`computeViewBounds`), zones (both
  path- and mesh-hosted outlines), triggers (full 3D gate frame, trivial),
  physics-sample overlay, self-intersection crossing markers, start marker,
  roll/width/cross-section handles and their on-canvas drag math, road-click
  hit-testing, weld/join-drag endpoint search, the right-click "Add control
  point" context menu (now composes the new point's position via
  `setPlaneCoords` instead of assuming X/Z), and Create mode's draft points.
- Deliberately NOT generalized, and gated to TopDown-only: mesh region
  rendering/hit-testing/drag/rotate and mesh rail rendering/picking
  (`drawMeshRegions`/`drawReservationWalls`/`drawMeshRails`/`meshRegionAt`/
  `meshVertexWorld`/`meshEdgeAtWorld`, the mesh-drag branch in
  `handleEditModeInput`). Two reasons, both already true before this
  follow-up and unchanged by it: `MeshRegion`'s polygon/rail data is a flat
  X/Z representation with no stored Y at all, so there's no sound projection
  to invent; and the whole type is deleted outright in Milestone 2, the very
  next step, so investing in it now is pure throwaway work. A stale mesh
  selection surviving a mode switch is explicitly guarded against (mesh drag/
  rotate only fires in TopDown).
- Test: `ctest` (all 7 suites green) and a smoke launch of `track_editor.exe`
  confirming every one of its ~20 built-in self-checks still reports `OK`
  with zero `MISMATCH`. While investigating a first ctest run's total
  failure, found `cpp/test-data/` deleted from the working tree by something
  outside this session's own actions (not a `rm`/`git` command this session
  ran) — restored immediately via `git checkout -- cpp/test-data` before
  re-running; confirmed clean and unmodified afterward. Also noticed an
  untracked, pre-existing (dated before this session) duplicate at
  `cpp/willpower/test-data/` — left alone as out of scope, flagged to the
  user. No manual interactive Front/Side verification was possible in this
  environment (same constraint as 1.2's own note) — this remains the
  highest-value thing to actually eyeball in the real editor before trusting
  it further.

**1.2 follow-up 2 — Side view axis orientation** — done, user-reported bug
  after the above landed
- Files: `cpp/editor/src/TopDownCanvas.cpp` and `cpp/editor/include/EditorState.hpp`
  (both `planeCoords`/`setPlaneCoords` copies).
- Two bugs in one: (1) Side's plane was `(y, z)` — height (Y) landed on the
  HORIZONTAL screen axis, not vertical, so Side didn't read as a side
  elevation view at all. Corrected to `(z, -y)`: Z (the "along the track"
  axis when looking along Side's view direction, X = 1) is horizontal,
  matching X's role in Front/TopDown. (2) Both Front and Side had an
  unnoticed sign bug even where Y *was* already the vertical slot (Front):
  `worldToScreen`'s second argument increases the screen Y pixel coordinate
  DOWNWARD, so feeding it raw (unnegated) world Y meant moving UP in world
  space drew LOWER on screen — backwards from every "elevation view"
  convention. Both modes now negate Y in their second plane slot. TopDown's
  own `(x, z)` (Z down = existing, unreported, left alone) is unchanged.
- `setPlaneCoords` (the write/drag-back direction) updated to match:
  Front now writes `x=u, y=-v`; Side now writes `z=u, y=-v`.
- The M6 smoke check (`main.cpp`, Front-mode height drag) needed its
  expected `v` argument negated to match the new sign convention;
  updated and reconfirmed `OK`.
- Test: `ctest` (all 7 suites green) and a `track_editor.exe` smoke launch
  (zero `MISMATCH` across every self-check). No manual interactive
  verification possible in this environment (same recurring constraint) —
  still the thing to actually look at before trusting Side/Front further.
  Committed.

**1.3 — Generalize shift+drag-to-rotate** — done
- Files: `cpp/editor/src/TopDownCanvas.cpp` (`rotateAngleDeg`, a mode-aware
  wrapper around the existing `angleFromOriginDeg` that feeds it
  `planeCoords()`-projected origin/cursor positions instead of raw x/z),
  `cpp/editor/include/EditorState.hpp` (`rotateGestureActive`/
  `beginRotateGesture`/`dragRotateGestureTo`/`endRotateGesture`: entity-
  agnostic angle bookkeeping, tracked but not yet applied to anything).
- Today's Mesh-region-only rotate gesture (`meshRotating_`,
  `beginMeshRotate`/`dragMeshRotateTo`/`endMeshRotate`, angle convention
  `atan2(dz, dx)` from the placement's `(x, z)`) is untouched here — it's
  removed wholesale with Mesh regions in Milestone 2, so rewriting it in
  place would be wasted work. Instead, a parallel, generic gesture landed:
  `TopDown` → yaw (`atan2(dz, dx)`, matching today's convention via
  `planeCoords`'s TopDown mapping), `Front` → pitch (`atan2(dy, dx)`),
  `Side` → roll (`atan2(dz, dy)`) — same atan2 math as before, just fed
  whichever plane `ProjectionMode` selects. No entity owns a yaw/pitch/roll
  field yet (drivable mesh object placements land in Milestone 3), so
  `beginRotateGesture`/`dragRotateGestureTo`/`endRotateGesture` only track
  the accumulated angle delta — Milestone 5 wires it to a real placement.
  `setMode`/`setProjectionMode` both drop an in-flight rotate gesture (and
  drag), matching the existing "no dangling half-mutation across a mode
  switch" convention.
- Test: `ctest` (all 7 suites pass; new plumbing is unused so no behavior
  change). Committed.

**1.4 — Retire `ElevationView`** — done
- Files: `cpp/editor/src/ElevationView.cpp`/`cpp/editor/include/ElevationView.hpp`
  (deleted), `cpp/editor/CMakeLists.txt` (source list), `cpp/editor/main.cpp`
  (include, header comment, dock layout simplified to left panel + Top-Down
  View filling the rest, `elevationVisible`/the "Elevation Profile" window
  removed, M6 smoke check rewritten), `cpp/editor/src/PropertiesPanel.cpp`
  (stale comment reference only), `cpp/editor/include/EditorState.hpp`
  (`dragSelectedElevationTo`/`dragSelectedMeshElevationTo` removed -- dead
  code once `ElevationView.cpp`, their only callers, is gone).
- Front canvas mode already reaches position-point height editing end to
  end as a side effect of Milestone 1.2: `dragSelectedTo`/rendering are
  generalized per `ProjectionMode`, so a Front-mode drag on a position
  point already moves `(x, y)` together -- no new drag code was needed
  here, just removing the now-redundant panel.
- Right-click-to-insert parity: judged not relevant to carry into
  Front/Side rather than generalized. `ElevationView`'s own right-click
  insert was a convenience duplicate of the top-down canvas's *existing*
  "Add control point" context-menu item (`TopDownCanvas.cpp`, pre-dates
  this plan) -- inserting a new point is inherently an X/Z-topology
  operation (`nearestPathPlacement` searches the centerline by X/Z
  distance, which has no sensible Front/Side analogue), so it stays a
  TopDown-mode-only affordance; Front/Side are for editing an *existing*
  point's other axes, not placing new ones.
- Test: `ctest` (all 7 suites pass). The M6 smoke check (`main.cpp`) was
  rewritten to drive a position point's height through
  `setProjectionMode(Front)` + `dragSelectedTo` instead of the retired
  `dragSelectedElevationTo`, so Front-mode height editing is verified
  headlessly on every run rather than only by a one-off manual pass --
  caught and fixed two real bugs while doing so (drag rounds to a 0.1m
  boundary, so a non-aligned starter-track X needed the same rounding in
  the expected value; comparing a `const tox::Vec3&` across `undo()`/
  `redo()` was a dangling reference once those replace the whole
  `TrackDefinition`). A smoke launch of `track_editor.exe` confirmed no
  startup/dock crash and the rewritten check printing `OK`. Committed.

**1.5 — Update editor conventions doc** — done, Milestone 1 complete
- File: `cpp/editor/CLAUDE.md`.
- Added bullets for `ProjectionMode` (toolbar/shortcuts, default, the
  drop-in-flight-gesture-on-switch behavior), the generalized drag-to-move/
  rotate math (`planeCoords`/`setPlaneCoords`, `rotateAngleDeg`, and what's
  still `TopDown`-only pending Milestone 2), and `ElevationView`'s removal
  (Front mode takes over height editing; right-click-insert not carried
  forward, with the reasoning). Also fixed a pre-existing doc/code drift
  noticed while editing this section: the mode-dropdown bullet named a
  `setEditMode()` that doesn't exist -- the real method is `setMode()`.
- Commit (docs-only; `ctest` reconfirmed green since this file has no build
  dependency).

---

## Milestone 2 — Remove 2D Mesh regions

Removed *before* drivable mesh objects are added, per the "hard break, no
coexistence" decision — this avoids ever having both entities live in the
schema/codebase simultaneously.

**2.1 — Remove core compiled types** — done
- Removed `TrackMesh.hpp`/`TrackMesh.cpp` entirely (`MeshRegion`,
  `MeshBounds`, `MeshPolygon`, `MeshTriangle`, `MeshFloorTriangle`,
  `MeshRail`, `MeshMoveResult`, `segmentCrossing`, `slideAlongRails`,
  `compileTrackMeshes`), `Track::meshRegions`, and every `TrackBake.cpp`/
  `Ship.cpp`/`Simulation.cpp`/`TrackLoader.cpp` call site. Landed together
  with 2.3/2.4/2.5's core-side work in one pass rather than as separate
  plan-ordered commits — the mesh-region removal, reservation
  wall/interior-mode removal, and host-surface removal were too entangled
  in `TrackBake.cpp`/`Ship.cpp` to cleanly separate.
- Central-reservation wall collision was NOT replaced with anything
  interim (user-confirmed scope decision, `AskUserQuestion`): a
  reservation still carves the road void, but produces no collision
  walls at all until Milestone 5 supplies real drivable mesh object
  placements. A car in analytic mode can currently drive off a void's
  edge with nothing to stop it; mesh mode is unaffected (never used
  `MeshRegion`).
- Discovered mid-implementation that this reaches further than the file
  list above named: `Ship.cpp`'s mesh-region rail-clip/landing/walking
  branches, `Simulation.cpp`'s `meshRegionAt`/`surfaceOwnerAt`, and
  `Track.hpp`'s `Zone`/`Reservation` structs all needed rewriting too.
- Core-side `ctest` (`parity`, `track_tests`) passed green before moving
  to the editor side.

**2.2 — Remove authored schema types and editor UI** — done
- Removed `MeshVertex`/`MeshEdge`/`DirectedMeshEdge`/`MeshPolygon`/
  `MeshAsset`/`MeshPlacement` from `EditorTrackDefinition.hpp`/`.cpp`;
  deleted `MeshPanel.cpp`/`.hpp` entirely; removed every mesh-region
  hit-test/drag/rotate/rail-toggle path from `TopDownCanvas.cpp`
  (`drawMeshRegions`, `drawReservationWalls`, `meshEdgeAtWorld`,
  `drawMeshRails`, `handleRailsModeInput`, `meshRegionAt`, the Rails-mode
  `EditMode` case); removed `EditorState`'s mesh
  placement/drag/rotate/rail-selection state and methods
  (`importMeshAsset`/`importMeshFromJsonText`/`placeMeshAsset` included).
  `EditMode` is `Edit | Create` now (was `Edit | Create | Rails`),
  3-way selection now (was 4-way: point/mesh/zone/trigger →
  point/zone/trigger). `main.cpp`'s toolbar Import/Paste Mesh UI, the
  M4/M5/M7c/M9 startup smoke checks, and the mode dropdown were removed
  to match.
- `RandomTrack.cpp`'s mesh-section generation branch (splitting the loop
  into open paths joined by generated jump platforms/ramps) was removed
  along with `MeshAsset`/`MeshPlacement` — `generateRandomTrack` is
  single-loop-only now; `RandomTrackRanges`' mesh-section-only fields
  (`meshChanceMin`/`Max`, `sequenceChance`, `maxMeshSections`,
  `meshLengthMin`/`Max`, `endDropMin`/`Max`) were removed too, along with
  their `RandomRangesPanel.cpp` UI.
- `ZonesPanel.cpp`/`TriggersPanel.cpp` simplified to path-hosted-only
  (see 2.4).
- Test: editor + core build clean, full `ctest` green.

**2.3 — Remove Capped-reservation special-case authoring** — done
- Removed `interiorMode`/`wallHeight`/`railClearanceHeight` from
  `TrackDefinition.hpp`/`EditorTrackDefinition.hpp` (and the
  `ReservationInteriorMode` enum), `TrackLoader.cpp`/
  `EditorTrackDefinition.cpp` parsing, and `ReservationsPanel.cpp`'s UI.
  Landed as part of 2.1's core-side pass (see above) plus this
  editor-side panel fix.
- No interim replacement, per plan: reservations become ordinary
  drivable mesh object placements once Milestone 3/5 exist.

**2.4 — Drop Mesh-region host-surface type** — done
- `Track.hpp`'s `Zone` struct and `TrackDefinition.hpp`/
  `EditorTrackDefinition.hpp`'s `ZoneHostDefinition`/
  `TriggerHostDefinition` (and editor mirrors) dropped every mesh-hosted
  field (`x`/`z`/`rotation`/`meshId`/`hostRegionIndex`); `TrackBake.cpp`'s
  mesh-hosted zone/trigger compile branches removed; `ZonesPanel.cpp`/
  `TriggersPanel.cpp` simplified to their single remaining path-hosted
  branch.
- `host.kind`/`kind` was deliberately KEPT as a string field (always
  `"path"` now) rather than dropped outright, so Milestone 3.5's
  eventual drivable-mesh-object-hosted variant has a discriminator to
  reuse instead of re-adding one from scratch.

**2.5 — Schema version / hard-break loader error** — done
- `TRACK_SCHEMA_VERSION` bumped 11 → 12 (`TrackCore.hpp`);
  `TRACK_SCHEMA_VERSION_MIN_SUPPORTED` unchanged at 10. `TrackLoader.cpp`/
  `EditorTrackDefinition.cpp` both throw an explicit
  `std::runtime_error` naming "Mesh regions (meshAssets/meshes)" when
  either field is present and non-empty, independent of the
  version-number check (mirrors how schema 10→11 kept the version check
  a loose range separate from any specific-field validation).
- Test added per the plan's own instruction: `track_tests.cpp` now loads
  `cpp/test-data/fixtures/mesh/concave-railed-pad.json` (an old fixture
  that authors `meshAssets`/`meshes`) and asserts the load fails with an
  error containing "Mesh regions". This is why `cpp/test-data/fixtures/
  mesh/` was NOT deleted in 2.6 below — it's still load-bearing as the
  hard-break check's input.

**2.6 — Update fixtures and docs** — done
- `cpp/test-data/fixtures/mesh/` and `cpp/test-data/fixtures/
  random-track-mesh/` were left in place rather than deleted: the former
  is now the hard-break test's input fixture (2.5), and both corpora
  feed CTest targets (`raw_parity`/`raw_session_init_parity`/
  `raw_session_step_parity`/`random_geometry_parity`) that are disabled
  (`add_test` commented out in `cpp/core/CMakeLists.txt`, with a comment
  explaining why) rather than deleted — per the user's confirmed
  decision (`AskUserQuestion`) to disable-and-note rather than
  re-author replacement fixtures now. This went beyond `raw_parity`
  alone: `raw_session_init_parity`/`raw_session_step_parity`/
  `random_geometry_parity` turned out to be equally
  `meshAssets`/`meshes`-dependent once checked, so all four were
  disabled under the same rationale. Milestone 7 ("mesh-mode regression
  coverage") is expected to restore this coverage with new traces.
- `docs/UBIQUITOUS_LANGUAGE.md`: removed **Mesh region**, **Region
  rail**, **Ledge**, **Mesh section**, **Platform sequence**, **Launch
  ramp** entries outright (retired with no replacement, since no
  Drivable mesh object entity exists yet to transfer the concept to);
  updated **Host surface**/**Track**/**Track surface**/**Airborne** and
  the "Rail"/"Mesh" flagged-ambiguity notes to drop mesh-region
  references.
- Updated `docs/core.md`, `docs/editor.md`, `cpp/core/CLAUDE.md`,
  `cpp/editor/CLAUDE.md`, `cpp/README.md`, root `CLAUDE.md`, and the
  `cpp/test-data/fixtures/random-track-mesh/README.md`/`traces/raw/
  README.md` fixture READMEs to remove `TrackMesh.hpp`/`MeshRegion`/
  Rails-mode/mesh-section references and note the schema-12 hard break
  and disabled test suites.
- Test: full `ctest` green throughout.

---

## Milestone 3 — Drivable mesh object schema (`core`) & host-side resolution (`tungsten-monoxide`)

Revised per the architecture note above: `core` only ever carries placements
as authored data (3.1/3.2/3.5/3.6); all `.mppmodel` loading/transform/merge
work is host-side (3.3/3.4), inside `cpp/tungsten-monoxide`, not `cpp/core`.

**3.1 — Authored placement type** — done (`99bcaef`)
- Files: `cpp/core/include/TrackDefinition.hpp`,
  `cpp/editor/include/EditorTrackDefinition.hpp`.
- New type, `DrivableMeshObjectPlacementDefinition { id; modelId;
  position{x,y,z}; rotation{yaw,pitch,roll}; scale{x,y,z} }` (mirrored in
  both places, same split every other authored type already uses),
  referencing an external `.mppmodel` by `modelId` (per the storage
  decision) rather than embedding geometry. Added to
  `TrackDefinition::meshObjects`, parsed/serialized by `TrackLoader.cpp`
  and `EditorTrackDefinition.cpp` like every other authored list.
- Test: `ctest` (schema addition only, unused elsewhere yet). Commit.

**3.2 — Confirm the host can reach placements with no compile step** — done, no code change
- File: `cpp/core/include/Track.hpp`.
- No geometry compile happens here by design (see architecture note) —
  confirmed by inspection: `TrackLoader.cpp:458` does `track.definition =
  normalize(data, ...)`, a direct unfiltered assignment of the whole
  normalized `TrackDefinition`, so `Track::definition.meshObjects` is
  already reachable off a loaded `Track` with zero additional code.
- Test: existing `ctest` suite (already green, no new test needed for a
  checkpoint with no code change).

**Reordering note (added when starting 3.3):** attempting 3.3 surfaced that
it depends on infrastructure that doesn't exist yet — resolving `modelId`
into a loadable resource needs a brand-new `Resource` subclass (mirroring
`MaterialResource`) plus editor-side `<DependentResource>` XML emission
(neither exists), and the per-sub-mesh collidable flag 3.3 reads is
Milestone 4's output, not built yet. Rather than build that resource-type
plumbing early or write untestable logic, **Milestone 4 is done first**
(user-confirmed reordering) — it's what actually produces the `.mppmodel`
content and collidable-flag metadata 3.3 needs to point at. 3.3/3.4 resume
after Milestone 4 lands.

**3.3 — Host-side collision-mesh resolution**
- File: `cpp/tungsten-monoxide/src/Map.cpp`.
- For each placement in the loaded track's `definition.meshObjects`, load
  its referenced `.mppmodel` (the same `mpp::ModelSerializer` path
  `Map::load` already uses for the track's own model — cache by `modelId`
  so multiple placements sharing one model parse it once), apply the
  placement's 6-DOF transform to each sub-mesh's vertex positions/normals,
  and merge the collidable sub-meshes' resulting triangles into
  `buildCollisionTriangles`'s output alongside the track's own collision
  triangles. The collidable-vs-decorative flag is read directly off the
  referenced model's own per-sub-mesh metadata (written by `model-tool`,
  Milestone 4) — there is no `core`/`TrackBake.cpp`-produced classification
  to consult, since `core` never touches this geometry (no new
  `GeometryKind` needed here, unlike the original draft of this step).
- Test: `ctest`; a fixture model with one collidable and one decorative
  sub-mesh confirms only the flagged one lands in `collisionTriangles`.
  Commit.

**3.4 — Host-side rendering**
- File: `cpp/tungsten-monoxide/src/Map.cpp` (or wherever it hands
  loaded geometry to the renderer).
- Instantiate each placement's referenced model at its transform through
  the existing MassivePolyPusher rendering pipeline — ordinary model
  instancing, reusing whatever `Map.cpp` already does to get the track's
  own model on screen, just parameterized by the placement's transform.
- Test: manual visual check (this codebase has no headless rendering test).
  `ctest`. Commit.

**3.5 — Host-surface support (core-side, no `.mppmodel` involved)**
- Files: wherever 2.4 removed the Mesh-region host-surface variant
  (`cpp/core/include/Track.hpp`'s `Zone`/`Trigger`,
  `TrackDefinition.hpp`'s `ZoneHostDefinition`/`TriggerHostDefinition`,
  and their editor mirrors).
- Add a drivable-mesh-object-placement host-surface variant (`host.kind ==
  "meshObject"`, referencing a placement id by name) so Zones/Triggers can
  attach to a placement, restoring the capability Mesh regions provided.
  This only ever needs the placement's own transform (position/rotation,
  now 6-DOF instead of the old flat 2D `x`/`z`/`rotation`) to place the
  zone/trigger in world space — never the referenced model's actual
  triangles — so it's ordinary `TrackBake.cpp` compile work, same shape as
  the old mesh-hosted branch 2.4 removed, not something that needs the
  host's `.mppmodel` loading at all. Whether the ship is actually grounded
  there at runtime is separately decided by mesh-mode physics against the
  host-built BVH, exactly as before.
- Test: `ctest`; a zone hosted on a drivable mesh object placement loads
  and resolves its world position from the placement's transform
  correctly. Commit.

**3.6 — Loader fixtures**
- File: `cpp/test-data/fixtures`.
- New fixture(s): a track referencing a drivable mesh object placement,
  validating 3.1/3.5's schema/round-trip and host-surface resolution.
  There is no core-side geometry compile path to validate here (see
  architecture note) — host-side resolution (3.3/3.4) needs a real
  `.mppmodel` and is validated manually/via Milestone 6's headless tool
  instead, not this fixture corpus.
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
