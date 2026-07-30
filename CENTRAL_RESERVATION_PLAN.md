# Central Reservation Plan — median strip on a track path

Status: **in progress.** This document is updated after each milestone lands.
Native C++ only (`cpp/core` + `cpp/editor`); the JS reference oracle (`web/`) is
deliberately NOT touched — JS is slated for retirement and is allowed to diverge.

## 1. Goal

Let a track author split a path's road surface between two `t` values (`t0`,
`t1`) to carve out a central reservation: a void running down the middle of
the road, walled off on both sides, splitting the road into a left and right
lane for that span.

## 2. Decisions (from grilling session, 2026-07-30)

- **Scope.** Extends the procedurally generated road corridor itself (paths +
  width/cross-section baking), not a placed mesh asset.
- **Data model.** New `PathDefinition::reservations` (authored side) /
  `editor::Path::reservations` (editor mirror): `{ id, t0, t1, width }`.
  Multiple non-overlapping reservations per path are allowed.
- **Width semantics.** The reservation carves out of the existing per-t road
  `width` (footprint unchanged). Each lane's width = `(width - reservationWidth) / 2`.
- **Taper.** `[t0, t1]` is the entire affected span. Reservation width ramps
  0 → full → 0 across it (three synthetic control points at `t0`, midpoint,
  `t1`, evaluated with the same Catmull-Rom `scalar()` helper TrackBake.cpp
  already uses for width/roll/cross-section). The two lane edges meet at
  `t0`/`t1`, so the gap is a closed "lens" shape with no separate end caps.
- **Floor.** The gap is a true void — no surface mesh is generated inside it.
  A car that ends up there falls through to the existing respawn-floor
  handling. (Only guaranteed for tracks with a baked `collisionSurface`, i.e.
  ones saved through the native editor — see M2 note.)
- **Rails + wall.** One boundary curve (the tapered lane edge) drives both a
  physics rail and a render wall — not independent systems.
- **Wall height.** Reuses `TrackCore::DEFAULT_RAIL_HEIGHT`; no new authored
  field.
- **Wall collision mechanism.** Reuses `slideAlongRails`/`MeshRail` (proven,
  already has bounce/slide + restitution) via a **synthetic `MeshRegion`**
  per reservation (no real mesh asset/placement, just `rails` + `bounds`).
  Rejected: feeding wall triangles into `TrackCollisionSurface` — that BVH
  has zero runtime response wiring today (see M2 note for what this costs).
- **Corridor lateral physics untouched.** `Simulation.cpp`'s frame-based
  `sLeft`/`sRight` clamp stays full outer width; the hard wall alone
  gatekeeps the gap. No lane-splitting in the corridor's soft-clamp math.
- **Checkpoints/triggers.** No change — they keep spanning the original full
  width regardless of the gap.
- **Validation.** Auto-clamp at edit time (span ordering, width vs available
  road width, non-overlap) — no reachable invalid state, no warning UI.
- **Schema.** Bump `TRACK_SCHEMA_VERSION` 10 → 11 in both `cpp/core` and the
  editor's own `kSchemaVersion`; `reservations` defaults to empty so v10
  files still load.
- **Editor UX.** Panel-only authoring (numeric `t0`/`t1`/`width` fields),
  matching the existing precedent that roll/width/cross-section points are
  not yet draggable on-canvas either (`PropertiesPanel`'s explicit
  path/t/Add flow). `TopDownCanvas` renders the resulting gap/wall read-only.
- **Rendering.** New dedicated `GeometryKind::ReservationWall` (own F1
  debug-overlay toggle), not folded into `PathRail`.

## 3. Milestones

- [x] **M0 — Schema & data model.** `ReservationDefinition` in
  `TrackDefinition.hpp` and the editor mirror; schema version bump;
  JSON load/save (`TrackLoader.cpp`, `EditorTrackDefinition.cpp`) with
  clamping/validation; round-trip unit tests.
  - The loader now accepts schema **10 or 11** (not just 11): the JS oracle's
    entire fixture corpus is permanently version 10 with no `reservations`
    field, so a hard `==11` requirement would have broken every existing
    fixture load. `TrackCore::TRACK_SCHEMA_VERSION_MIN_SUPPORTED = 10` /
    `TRACK_SCHEMA_VERSION = 11` (and the editor's matching
    `kSchemaVersionMinSupported`/`kSchemaVersion`) bound the accepted range;
    both loaders still normalize `version` to 11 on load and the editor only
    ever writes 11.
  - Core's loader clamps and silently drops degenerate reservations
    (`t1-t0<=0` or `width<=0`); the editor's mirror keeps them as authored
    (mid-edit tolerance, same policy as its other fields) — non-overlap and
    "fits within the road width" validation is EditorState's job (M3), not
    the parser's.
- [x] **M1 — Baking.** Taper evaluation; carve the gap out of
  `PathSurface`/`PathShell` (two lane strips instead of one, tapering to a
  point at `t0`/`t1`); synthetic `MeshRegion` (bounds + tapered-boundary
  rails) appended to `track.meshRegions`; `GeometryKind::ReservationWall`
  render batch (vertical quad strip along the same boundary,
  `DEFAULT_RAIL_HEIGHT` tall).
  - `Frame::reservationHalfGap` (new field) carries the carved half-width per
    baked centerline sample, computed once in `center()`/`adaptiveRenderBake`
    via the existing Catmull-Rom `scalar()` helper against a synthetic
    3-point `{t0:0, mid:width, t1:0}` control list.
  - `PathShell` (the cross-section-thickness underside) is **not** carved —
    scope-limited to `PathSurface`. Most tracks don't use cross-section
    thickness; revisit if that turns out to matter.
  - `adaptiveRenderBake`'s chord-tolerance subdivision doesn't know about
    reservations (it only looks at centerline position), so a straight span
    containing one would otherwise collapse to its two endpoints with
    nothing to carve. Fixed by forcing a fixed 9-point (not the full physics
    sample density — that over-tessellated badly) evenly-spaced anchor set
    across each reservation's span into the render mesh.
  - The synthetic `MeshRegion` has **no `polygons`/`triangles`** — only
    `bounds` + `rails` — so `meshRegionAt`/`surfaceOwnerAt` (which gate on
    `region.contains()`) can never treat it as a standing surface. This is
    also how M2 tells a reservation's region apart from a real placed mesh
    asset's (always-populated) region.
- [x] **M2 — Physics wiring.** `Ship.cpp`'s grounded-corridor branch checks
  reservation regions via `slideAlongRails` (new small loop; the existing
  mesh-region loops only fire while airborne-near or already "on" a mesh
  region, neither of which covers "driving the main corridor near a gap").
  Known limitation: the "true void" fall-through depends on a baked
  `collisionSurface` (native-editor-saved tracks); raw-JSON-only physics
  without one still treats the gap's lateral span as within `sLeft`/`sRight`
  and won't rebuild a floor gap on its own — the wall still blocks entry.
  - New loop lives in the `hasTranslation` grounded branch, gated on
    `region.polygons.empty()` — the trait that tells a reservation's
    synthetic region apart from a real placed mesh asset's region (always
    populated). A real platform's edge is unaffected: it still only collides
    via the pre-existing ownership/airborne-proximity paths.
- [ ] **M3 — Native editor UI.** `EditorState` mutation helpers (add/
  remove/update with clamp + non-overlap validation — undo/redo is free via
  existing whole-document snapshots); new `ReservationsPanel` (list + t0/t1/
  width fields, mirrors `TriggersPanel`); wire into `main.cpp`;
  `TopDownCanvas` read-only preview of the gap/wall footprint.
- [ ] **M4 — Debug overlay.** F1 overlay toggle for
  `GeometryKind::ReservationWall` in `StatePlayTungstenMonoxide.cpp`,
  alongside the existing `TriggerSurface`/`PathRail`/`MeshRail` toggles.
- [ ] **M5 — Tests.** `track_tests.cpp`: schema round-trip, baked gap/rail
  geometry shape, a physics scenario driving into the wall. Editor-side
  mutation/validation tests alongside the existing `cpp/editor/tests/`.

## 4. Notes for implementers

- Pre-existing uncommitted work in this working tree (`TrackResourceDocument`/
  `TrackResourceSave`, `docs/adr/0002-...`, editor main.cpp changes, etc.) is
  **not part of this feature** — commits for this plan stage only the files
  each milestone actually touches, never a blanket `git add -A`.
- `cpp/core/src/TrackBake.cpp`'s `scalar()` (Catmull-Rom, closed/open aware)
  is the existing helper to reuse for the width taper — construct a 3-point
  synthetic list `{t0: 0, mid: width, t1: 0}` and evaluate through it, same
  pattern already used for roll/width/cross-section.
