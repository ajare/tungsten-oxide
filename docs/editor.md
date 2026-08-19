# src/editor — the native track editor (`track_editor`)

`src/editor` is a native ImGui/SDL2/OpenGL application (Windows/MSVC only) for authoring the tracks `src/core` loads. It links `core` and reuses it as a black box for baking and validation — the editor never re-implements spline evaluation or physics itself; where it needs baked data (e.g. to draw a ribbon), it either reads `core`'s output or, where core keeps something private (like the spline `Evaluator`), accepts a documented, editor-zoom-appropriate approximation.

Domain vocabulary as in `UBIQUITOUS_LANGUAGE.md`/`glossary.md` (both in this directory). See `src/editor/CLAUDE.md` for line-level UI-gesture conventions and `src/editor/main.cpp:1-34` for a milestone-by-milestone feature history (M0–M10).

## Intended use

Author and edit complete tracks — paths, reservations, zones, triggers, texture/material assignment — then save them as a Track Resource (a schema-12 JSON + generated `.mppmodel` pair registered in a Resources XML) that `src/tungsten-monoxide` can load and play. It is an authoring tool only: it never runs gameplay simulation, and there is no in-editor 3D preview (see [Limitations](#limitations)).

## Architecture

- **`EditorState`** (`include/EditorState.hpp`, header-only) wraps one mutable `EditorTrackDefinition` (`track_`), an undo/redo `History`, the current `EditMode`, and independent selection state for points, zones, triggers, and reservations. `EditorTrackDefinition` (`include/EditorTrackDefinition.hpp`) is a deliberately separate type from `tox::TrackDefinition` — the editor owns both JSON directions independently so `core`'s loader strictness (e.g. requiring 4+ position points per path) never blocks a half-drawn track.
- **`History`** (`include/EditorHistory.hpp`) — whole-track deep-clone snapshots, not diffs, capped at 30 (`kMaxHistory`). Every discrete mutation pushes before mutating; continuous gestures (dragging, typing) push once at gesture start, so one drag = one undo step.
- **Edit modes** (`EditMode { Edit, Create }`) — all mode changes go through `EditorState::setMode()`, so the `E`/`C` shortcuts and the toolbar dropdown can't drift apart. **Edit** is where everything is selectable/draggable. **Create** appends spline points on left-click, closes/finishes on clicking near the first/last point. A third mode, **Rails**, existed for placed-mesh rail-edge flagging; it was removed along with `MeshRegion`/`MeshAsset` (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2).

## Track-design authoring features

### Paths and control points

- **Creation**: Create mode; finishing a draft (≥4 points) auto-seeds two Roll, two Width (36 m), and two CrossSection points at `t=0`/`t=0.5` (closed) or `t=1.0` (open) — `EditorState::appendDefaultAuxPoints`.
- **Dragging**: position points drag directly (`dragSelectedTo`); roll/width/cross-section points have their own on-canvas handles projected onto the baked centerline at each point's `t` (`drawAuxPoints`/`auxHandleAtLocal`, `TopDownCanvas.cpp`) — dragging one simultaneously slides it *along* the curve and changes its value.
- **Adding a point** via the right-click context menu seeds the new point from the **nearest baked centerline frame's current value**, not a schema default.
- **Point-type conversion** re-seeds a converted aux point from its neighbors via a non-uniform Catmull-Rom matching `core`'s own scalar evaluation.
- **Path topology**: `joinPathEndpoints`, `extendOpenPathFromEndpoint`, `makeDisjoint`/`reconnectDisjoint`, `splitSelectedPoint`. Two on-canvas gestures: plain drag-to-weld (drag one open endpoint onto another), and shift-drag rubber-band (never moves the source point; releasing on a target joins, releasing in empty space extends the path).
- Panel: `src/CurvesPanel.cpp` (curve list, join form, junctions/disjoint-seam tables); typed-field panel: `src/PropertiesPanel.cpp` (per-point-kind fields, a live cross-section-profile preview drawn with `core`'s own `crossSectionHeight`).
- **Elevation editing**: `ProjectionMode { TopDown, Front, Side }` picks which plane a canvas drag projects into — dragging a position point in Front mode moves its `(x, y)` directly, replacing the retired separate elevation-profile side view.

### Reservations

Panel: `src/ReservationsPanel.cpp` — `t0`/`t1` (%), width mode (Fixed m / Percent, converting the stored number in place when you switch), a resolved "~N m at midpoint" readout, and per-end style/cap-width/nose-length controls. Switching an end to Rounded seeds a visible default nose length (40 m) rather than the bake's geometric default (`width/2`), which would be imperceptible at authoring zoom. A reservation only carves the road void now — the wall-height/interior-mode/rail-clearance-height controls (and the mesh-rail preview line they configured) were removed along with `MeshRegion` (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2), with no interim replacement; the canvas draws no reservation-wall preview anymore either.

### Zones and triggers

Zones (`velocityChange`/boost, `jump`, `startGrid`) and triggers (dummy/checkpoint, with finish-role promotion enforcing at-most-one-finish) are add/edit/delete via `src/ZonesPanel.cpp`/`src/TriggersPanel.cpp` plus canvas right-click submenus, all path-hosted. A checkpoint's `width` can auto-track the host path's baked road width at its `t`. Mesh-hosted zones/triggers (the mesh-region host-surface variant) were removed along with `MeshRegion` (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2); `host.kind` stays a string discriminator (always `"path"` now) for a future drivable-mesh-object-hosted variant to reuse.

### Texture assets and materials

The texture-*asset* authoring UI (add/browse/tile-size) has been removed; `EditorState`'s texture-asset methods survive with no UI driving them. What remains is `src/MaterialsPanel.cpp` — a read-only list of `TrackMaterial`s loaded from the Resources YAML named in `editor.ini`, each shown with its first texture's thumbnail; clicking one assigns it to the current path. `src/editor/src/TextureCache.cpp` resolves a relative texture path against the repo root (found by walking up from the working directory for `assets/track/manifest.json`), so the exe can be launched from anywhere and still find the same on-disk texture.

### Self-intersection detection

Requested per-bake from `core` (skipped mid-drag, reusing the last result so markers don't flicker). Markers cycle **none → keep → collapse → none** on click (`EditorState::cycleCrossingOverride`), colored to distinguish the automatic decision from a user override, and are drawn before authored points so they never occlude editable handles.

### Render modes

`TopDownView::RenderMode { Banked, Flat, Elevation, Camber }`, picked from either the View menu or a toolbar combobox (both driven by one shared table so they can't drift):

- **Banked** (default) — offsets the ribbon by each frame's actual banked `edgeRight`.
- **Flat** — offsets by the unrolled plan-view axis, filled by interpolated roll magnitude.
- **Elevation** — unrolled, filled by elevation (compared across the *whole track*, not just the drawn path, so colors are comparable path-to-path).
- **Camber** — unrolled, filled white at zero roll/curvature, green when the road banks *into* the turn (on-camber), red when it banks *away* (off-camber). Uses `core`'s analytical `TrackCore::pathSignedCurvatureAt`, not curvature derived from baked frames — see `docs/core.md`.

### Start point and grid

`Start{path, point, reverse}` — set via the Properties panel or Track Properties panel; re-anchored by point id across edits. The `startGrid` zone effect is a separate, purely visual marker mechanism.

### Random track generation

`include/RandomTrack.hpp`/`src/RandomTrack.cpp` — a deterministic generator (seed + complexity + tunable ranges, all editable in `src/RandomRangesPanel.cpp`) producing a single N-turn loop with rolling hills, curvature-based banking, and boost zones. Used both as a content generator and as the basis for several of the editor's own startup self-checks. A second, mesh-section code path (splitting the loop into open paths joined by generated jump platforms/ramps) was removed along with `MeshAsset`/`MeshPlacement` (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2); the generator is single-loop-only now.

## Save / load workflow

Design of record: `docs/adr/0002-track-resource-save-load.md` ("Accepted and implemented"). Document state (the save binding, dirty-check baseline) lives outside `TrackDefinition`, in `main.cpp`.

- **Load** (`File ▸ Open Resources XML…`, Ctrl+O) scans the logical `Tracks` namespace (`TrackResourceDocument.hpp`) and presents every candidate — Ready, Warning, or Invalid — in a modal chooser; invalid entries stay visible with their validation error shown rather than being hidden.
- **Save** (`TrackResourceSave.hpp`) has two hard preconditions: the track must currently bake, and every path's material must resolve in the catalog. It builds a `TrackSavePlan` performing no writes (all three destination byte-contents computed up front), then commits XML + JSON + `.mppmodel` as one rollback-capable transaction — on any failure, pre-existing files are restored and nothing is left half-written.
- **Safe relative paths**: a stored `<ModelFile>`/`<TrackData>` reference must be non-absolute and stay under the XML's own directory after normalization (no `..` escape); an unsafe or missing reference is repaired to a resource-name-derived sidecar name on save.
- **Fingerprints** detect out-of-editor changes to the bound resource and JSON sidecar, surfacing a Save Conflict modal (Reload / Save As / Cancel) rather than silently overwriting.
- **`.mppmodel` export** (`src/MppModelExport.cpp`) is a from-scratch native writer of the format documented in `MPPMODEL_EXPORT_SPEC.md` — it deliberately avoids linking `mpp::ModelSerializer` (which would pull in GLEW alongside the editor's own gl3w loader) and, as a side effect, avoids a documented upstream offset bug in that serializer's directory-entry update path. Materials are referenced by name only; the target MPP project must define the fixed `Tracks/Default*` material names the exporter writes (kept in sync with `src/core/src/TrackBake.cpp`'s hardcoded material keys).
- Also available: JSON export/import, USD export (`src/USDExport.cpp`, walks `core`'s baked renderer-neutral geometry batches — not a from-scratch derivation).

## Limitations

- **No 3D preview.** Only one canvas exists, in one of three projection modes (top-down, front, side). Visual validation of the actual baked surface happens by exporting to USD or `.mppmodel` and opening it elsewhere.
- **The top-down ribbon is an explicit approximation**, not the real render mesh: it walks ~6 m physics-sample rings rather than the finely-subdivided adaptive render mesh, and several interactions (tangent-projected dragging, segment-index reconstruction from `t`) are documented in-code as "close enough at editor zoom," not exact.
- **No placed-object authoring of any kind.** Mesh regions (placed 2D `{vertices, edges, polygons}` assets, with per-edge rail flags and per-placement transform) were removed entirely (`DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2), with no interim replacement; `DRIVABLE_MESH_OBJECTS_PLAN.md`'s later milestones plan a 3D drivable-mesh-object system in their place.
- **Shared/disjoint point-id aliasing is unimplemented** in the editor — that's `core`'s loader's job.
- **Undo is coarse**: whole-track snapshots capped at 30 steps; view state (zoom, pan, grid, render mode, point filters) is deliberately outside history and not undoable.
- **No layout persistence** — the dock layout rebuilds identically every launch.
- **Save is blocked**, not degraded, when the track fails to bake or any path references an unresolvable material.
- **Startup exits** on structural resource failures (missing/malformed `editor.ini` or Resources.yaml) rather than continuing with an empty catalog.
- **Windows-only in practice** — native file dialogs and clipboard access are Win32-specific.
- Central-reservation editor-side testing is documented as partial (`CENTRAL_RESERVATION_PLAN.md`: "M5 partial — core tests done, editor tests deferred").
