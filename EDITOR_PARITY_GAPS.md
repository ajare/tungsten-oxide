# Editor parity gaps: `js/editor.js` → `cpp/editor`

A feature-by-feature audit of the browser editor (`editor.html` + `js/editor.js`, 4696 lines)
against the native C++/ImGui editor (`cpp/editor/`, ~8000 lines excluding vendored ImGui), listing
everything the JS editor does that the C++ one does not, with implementation notes.

Audit method: every control in `editor.html`, every function definition and every
`addEventListener` target in `js/editor.js`, cross-checked against the C++ menus, panels, canvas
draw/input paths, and `EditorState`'s public API.

Ordered by how much each gap blocks authoring work. Gaps 1–5 block work; 6–10 cost fidelity and
readability. See "Confirmed non-gaps" at the end for things that look missing but aren't — check
that list before starting anything, it is the reason several obvious-looking items are absent here.

---

## 1. Self-intersection crossing markers & override cycling

**The largest gap.** A track that overlaps itself needs the author to say, per crossing, whether the
inner loop is collapsed away or kept as real geometry. JS exposes this; C++ has no way to reach it.

### What JS does

| Piece | Location |
| --- | --- |
| `detectPathCrossings(controlPoints, closed, edges, wrapOpen)` | `js/editor.js:823` |
| `crossingState(cr)` — resolves override vs. default `span <= 100` rule | `js/editor.js:806` |
| `crossingOverrideFor(a, b, side)` | `js/editor.js:801` |
| `CROSSING_COLORS` | `js/editor.js:812` |
| `crossingMarkerAtTop(sx, sy)` — hit test, `CROSSING_HIT_RADIUS = 11` | `js/editor.js:837` |
| `cycleCrossingOverride(cr)` — `auto → keep → collapse → auto` | `js/editor.js:849` |
| Marker draw loop (filled disc = collapsed, hollow ring = kept) | `js/editor.js:1106-1130` |
| Mousedown dispatch (before `nodeAtTop`, skipped when shift held) | `js/editor.js:3278-3281` |

Colour code: grey `#b9c2d0` auto-collapse, amber `#ffb020` auto-keep, red `#ff3355` forced-collapse,
green `#37d17a` forced-keep. Crossings are cached in `crossingCache` and only recomputed when idle
(`if (!dragging)`, `js/editor.js:978`) because detection is O(N²).

### What C++ has

`selfIntersectionOverrides` round-trips through JSON (`EditorTrackDefinition.cpp:329-334` read,
`:527-529` write), is pruned when stale (`EditorState.hpp:1784-1787`), and is consumed at bake time.
**There is no detection, no marker, and no way to create an override.**

### Implementation

The whole algorithm already exists in C++ — inside `removeSelfLoops`
(`cpp/core/src/TrackBake.cpp:224-275`), which sits in an **anonymous namespace** and so is
unreachable from the editor. It contains, in order:

- `segmentsCross(a, b, c, d)` (`TrackBake.cpp:219`) — the crossing predicate.
- `controlId(frameIndex)` (`:231`) — the exact equivalent of JS's `TrackCore.crossingKey`, mapping a
  frame index back to the nearest control point's id.
- `decision(i, j, span)` (`:238`) — override lookup with the `span <= 100` default. Note `100` is
  hardcoded here where JS names it `TrackCore.DEFAULT_SELF_INTERSECTION_SPAN`.
- `lineX(...)` — intersection point, which is the marker's world position.

**Blocker to resolve first:** `tox::Path` (`cpp/core/include/Track.hpp`) retains only
`{closed, endpointIds, anchors, centerline}` — the **collapsed** rails. Crossings are detected on
*pre-collapse* edges, which are local to `bakeTrack` and discarded. The editor therefore cannot
recover them from the baked output; detection must be surfaced from core.

Suggested shape:

1. In `cpp/core/include/Track.hpp` (or a new `TrackCrossings.hpp`), add:
   ```cpp
   struct SelfIntersection {
     std::string side;      // "left" | "right"
     std::string a, b;      // control-point ids, the stable key
     int span{0};
     Vec3 point;            // world-space intersection
   };
   ```
   and either a free function `std::vector<SelfIntersection> findSelfIntersections(const Track&)`,
   or (cheaper) a `std::vector<SelfIntersection> selfIntersections` field on `tox::Track` populated
   during the bake pass that already runs the detection.

   The second option is strongly preferred: `removeSelfLoops` already computes every field of
   `SelfIntersection` as a side effect. Emitting them costs nothing, while a standalone function
   would duplicate `split()`/`controlId()` and risk drifting from the collapse rule it must agree
   with. Split the loop body so the detection half fills the vector and the mutation half consumes
   it, and factor the hardcoded `100` into a named constant shared by both.

2. Editor side, `cpp/editor/src/TopDownCanvas.cpp`:
   - `drawCrossings(...)` after `drawPhysicsPoints` (`:1547`) and before the authored point draw, so
     markers never occlude editable handles. Mirror JS's disc-vs-ring convention and the four
     colours above.
   - `crossingAtWorld(...)` alongside `physicsPointAtWorld` (`:463`), same `kPickRadiusPx` idiom.
   - Dispatch in `handleEditModeInput` (`:878`) *before* the position-point hit test, matching JS's
     ordering at `:3278`.
3. `EditorState::cycleCrossingOverride(side, a, b)` — push undo, then insert `{side, a, b, "keep"}` /
   promote to `"collapse"` / erase, mirroring `js/editor.js:849-861` exactly (note the
   order-insensitive `(a,b) == (b,a)` match on both sides).

Cache the detection result per bake, not per frame — JS's `!dragging` guard exists for a reason.

---

## 2. Point type conversion ("Type" dropdown)

Convert a selected control point between `position` / `roll` / `width` / `crossSection`, taking its
new value by evaluating the *remaining* points' splines at the removed point's `t` so nothing jumps.

- JS: `convertSelectedPoint(newType)` at `js/editor.js:1540-1612`; the dropdown is built by
  `typeSelectRow`/`wireTypeSelect` inside `renderProps` (`:2135-2143`).
- C++: nothing.

### Implementation

Add `EditorState::convertSelectedPoint(PointKind newKind)` and a `Type` combo at the top of
`DrawPropertiesPanel` (`cpp/editor/src/PropertiesPanel.cpp:287`).

Port the guards verbatim — they are what keeps the track bakeable:

| Condition | JS line | Behaviour |
| --- | --- | --- |
| Shared/disjoint position point (`countPointOccurrences > 1`) | `:1559` | refuse |
| `position` and `controlPoints.length <= 4` | `:1562` | refuse |
| `roll`/`width`/`crossSection` and that kind has `<= 2` points | `:1565-1573` | refuse |

C++ already has the pieces: `positionCount()` (`EditorState.hpp:1184`), the same 4-point floor in
`deleteSelectedPoint` (`:1156`), `addAuxPoint(pathIndex, kind, t)` (`:1354`), and
`insertPositionOnSegment(...)`. JS surfaces refusals via `alert()`; prefer a disabled combo entry
with a tooltip giving the reason (the editor has no modal path, and `BeginDisabled` + `SetTooltip`
is the established idiom here).

Order of operations matters: JS removes the old point **first**, then re-splits (`remaining`,
`:1576`) so the interpolation reads the neighbours *without* the point being converted. Converting to
`position` also needs the insertion index derived from `t`
(`Math.round(t * (closed ? n : n-1))`, `:1587`), not an append.

The 4 → `position` case needs an evaluator to get the new XYZ. C++ has no authored-spline evaluator
in the editor, but `sampleCenterlineAtG` (`TopDownCanvas.cpp:269`) samples the baked centerline and
is what the context-menu "Position" item already uses for the same purpose
(`TopDownCanvas.cpp:1424-1432`) — reuse that, and accept the same documented approximation.

---

## 3. Mesh region property editor — ✅ Implemented

A selected mesh region previously had no panel at all in C++.

| Field | JS (`renderProps`, `js/editor.js:2192-2199`) | C++ |
| --- | --- | --- |
| X, Z | number inputs, step 0.5 | canvas drag, **and now the panel** |
| **Elevation** | number input **and** the elevation-view drag (gap 4) | **panel field added; the elevation-view drag (gap 4) is still open** |
| Rotation° | number input, step 5 | shift+drag on canvas, **and now the panel** |
| **Rail height** | number input, per-**asset** (affects every placement) | **panel field added** |
| Rail count hint | `N of M edges railed` | **added** |
| Delete | `#delMeshBtn` | Delete key, **and now the panel** |

### Implementation (done)

New `cpp/editor/src/MeshPanel.cpp` + `include/MeshPanel.hpp` (registered in `CMakeLists.txt` and
`main.cpp`'s `#include` list), added as a `CollapsingHeader("Mesh Region")` in `main.cpp`'s `Panels`
window right after "Point Properties" and before "Zones". Follows `ZonesPanel.cpp`'s exact pattern:
`InputDouble` with `kCommitOnEnter` + `IsItemDeactivatedAfterEdit()`, committed through a
read-modify-write mutator that pushes undo once.

`EditorState` gained two mutators alongside the panel (it previously had `findMeshPlacement()` and
`deleteSelectedMesh()` but no setters at all):

- `editMeshPlacement(id, mutate)` — X/Z/elevation/rotation, unclamped like JS's own plain
  `meshPlacement[inp.dataset.mesh] = val` assignment.
- `setMeshAssetRailHeight(assetId, height)` — rail height lives on the **asset**
  (`track().meshAssets`), not the placement, so this affects every placement of that asset at once
  (mirrors `toggleRailEdge`'s existing asset-level sharing); clamped to `>= 0` like JS's
  `Math.max(0, val)`. The panel labels the field with a tooltip noting this, same as JS's `title`
  attribute.

The railed-edge count hint reads `asset->edges` directly (`std::count_if` on `MeshEdge::rail`).

---

## 4. Elevation view: draggable mesh elevation line — ✅ Implemented

Was the only way to set a mesh region's elevation in JS, pairing naturally with gap 3 (whose panel
now also has an Elevation field — this is the second, on-canvas way to reach the same value).

- Draw: `js/editor.js:1479-1489` — a full-width horizontal line at the placement's elevation,
  labelled `<asset>  y <elevation>`, brightened while dragging.
- Drag: `dragging === 'meshElev'`, picked on vertical proximity alone (`MESH_ELEV_PICK_PX`,
  `js/editor.js:3562-3571`), applied at `:3416`.
- The plotted Y range is expanded to include the placement's elevation (`js/editor.js:1410-1415`) so
  the line stays on-panel even when the region sits far above or below the curve.

### Implementation (done)

`cpp/editor/src/ElevationView.cpp`:

- `rawYRange()`/`computeLayout()` gained an optional `extraY` parameter, folded into the min/max
  before `layoutFromRange()`'s padding is applied — mirrors JS's `:1410-1415`. `DrawElevationView`
  passes the selected mesh placement's elevation, when one is selected, so both the live and
  drag-frozen layouts include it automatically (the freeze just copies whichever layout was live at
  drag-start).
- New `drawMeshElevationLine(...)` draws the full-width line + `"<assetId>  y <elev>"` label,
  called right after `drawBakedProfile`, brightened (`kMeshElevLineDraggingColor`, matching JS's
  `#f0e4ff`) while actively dragging.
- Hit test: `meshElevLineHovered` checks vertical proximity only (`kMeshElevPickPx = 6.0f`, matching
  JS's `MESH_ELEV_PICK_PX`), no x-range restriction, since the line spans the full panel width.
- A new static `meshElevDragArmed` (parallel to the existing `frozenLayout` static) is set on
  mousedown when the click lands on the line, checked *before* the position-point hit test so it
  takes priority — mirroring JS's mousedown handler checking mesh-elevation proximity first
  (`js/editor.js:3560-3576`), ahead of roll/position hit tests.
- `EditorState::dragSelectedMeshElevationTo(y)` — new mutator sharing the existing
  `dragging_`/`dragMutated_` gesture lifecycle with `dragSelectedElevationTo`, operating on
  `mutableSelectedMeshPlacement()` instead of `selection_` (mesh/point selection are mutually
  exclusive, so there's no ambiguity about which one a drag applies to). Rounds to 0.1 like JS.
- The existing `frozenLayout` drag-sensitivity-freeze mechanism covers this drag too (same
  runaway-acceleration failure mode as a position-point elevation drag otherwise).

---

## 5. Elevation view: right-click to insert a position point — ✅ Implemented

`insertPositionAtSide(sx, sy)` (`js/editor.js:2806-2826`, wired at `:3557`) inserts a new position
control point at the clicked arc position, taking X/Z from the curve there and **Y from the click
height**. C++'s elevation view previously only selected and dragged existing points.

### Implementation (done)

`cpp/editor/src/ElevationView.cpp`:

- `InvisibleButton`'s flags gained `| ImGuiButtonFlags_MouseButtonRight` (was left-only).
- New `xAxisFraction(layout, screenXPx)` — the inverse of `screenX`'s own frac computation, clamped
  to `[0, 1]`. Since this view's x-axis is authored ORDER rather than true arc length (see
  `ElevationView.hpp`'s header comment), "fraction across the axis" doubles as this file's stand-in
  for JS's `sampleAtArc`'s arc-length fraction — consistent with the approximation every other
  handle in this view already uses.
- New `sampleCenterlinePosAtG(centerline, closed, g, gMax)` — the position-only counterpart of
  `TopDownCanvas.cpp`'s `sampleCenterlineAtG`, duplicated locally rather than shared (each file
  keeps its own small approximate evaluator, matching this codebase's existing per-file pattern).
- On right-click: `g = xAxisFraction(...) * gMax`, X/Z from `sampleCenterlinePosAtG(...)`, Y from
  `worldYAt(...)`, `insertAt` from the same `floor(g) + 1` (wrapped for closed paths) arithmetic
  `TopDownCanvas.cpp`'s context menu and JS's `:2817-2819` both use, then
  `EditorState::insertPositionOnSegment(...)` — the exact same mutator the top-down context menu's
  "Position" item already calls.
- Guarded by both of JS's conditions: `showPositionPoints` (new parameter, threaded from
  `main.cpp`'s `topDownView.showPositionPoints()` — this view previously had no way to see the
  top-down point-filter checkboxes at all) and the click landing inside the plot gutter
  (`mouseLocal.x` within `[kPadX, layout.w - kPadX]`).

---

## 6. Shift-drag endpoint: rubber band → connect, or extend the curve — ✅ Implemented

JS shift-drags an open curve's endpoint with a live rubber-band line — yellow `#ffd23c` while
free, green `#31d66b` and snapped to the node when over a valid drop target
(`js/editor.js:1207-1220`). On release:

- over a target → `joinSel = [from, target]` then `performJoin()` (`:3536-3538`);
- in empty space, past `JOIN_DRAG_MIN_PX` → **`extendCurveFromDrag(from, screenPos)`** (`:2862-2880`),
  which appends a brand-new point extending that curve, inheriting the dragged endpoint's elevation.

C++ already had plain drag-to-weld (drag a selected endpoint onto another endpoint, `TopDownCanvas.cpp`
`weldTarget` machinery) — no shift modifier, no rubber band, and no extend-into-empty-space. Both
gestures now coexist: plain drag still relocates the point continuously; shift-drag never moves it,
only previews, and mutates once on release.

### Implementation (done)

`EditorState.hpp` gained `extendOpenPathFromEndpoint(pathIndex, atEnd, worldX, worldZ)` —
appends/prepends a new position point at the drop location, inheriting the endpoint's own current Y
(mirrors `extendCurveFromDrag`), delegating to the existing `insertPositionOnSegment`.

`TopDownCanvas.cpp`:

- New `JoinDragPreview{from, target, currentLocal}` struct threads the live gesture state from
  `handleEditModeInput` out to the drawing code, alongside the existing `outWeldTarget` out-param.
- Shift-clicking an open endpoint (`state.hitTestOpenEndpoint(..., -1, false)` — excludePathIndex
  `-1` matches nothing, so it finds the globally nearest one) starts the gesture instead of falling
  into the existing plain-click/select-and-drag branch, mirroring JS's mousedown checking
  `e.shiftKey` and an endpoint hit first.
- The existing `draggingGesture` (which gates every OTHER drag branch: position/width/roll/
  cross-section/trigger/mesh/pan) now also requires `!joinDragFrom.has_value()`, so none of those
  branches can fire while this gesture owns the mouse — otherwise a stale pre-shift-click selection
  could start moving under the same drag.
- While active: `joinDragTarget` is re-hit-tested every frame via `hitTestOpenEndpoint` (excluding
  only the dragged endpoint itself, so closing a curve onto its own other end still works). On
  release: a target calls the existing `joinPathEndpoints` (which already makes the endpoints
  coincide by copying the target point onto the source's slot, so no separate "snap" step is
  needed); no target, past `kJoinDragMinPx` (12px, matching `JOIN_DRAG_MIN_PX`), calls
  `extendOpenPathFromEndpoint` with a grid-snapped drop position.
- New `drawJoinDragLine(...)` draws the rubber band from the endpoint's baked anchor position to
  the cursor (or the target's anchor position when snapped), reusing `kWeldTargetColor` for the
  "valid target" green (same JS hex value) and a new `kJoinDragFreeColor` for the free/yellow state.
  Drawn solid rather than JS's dashed `[6,4]`, matching this file's existing solid-line
  simplification for `drawCreateDraft`'s own dashed-in-JS create-mode draft.

Scope note: mirrors JS's endpoint-to-endpoint connect only, not JS's third case (dropping onto an
INTERIOR point of a different path, which splits the target path there) — the same scope reduction
`joinPathEndpoints` and the Curves panel's Join feature already document and rely on.

---

## 7. Start marker & direction arrow — ✅ Implemented

`js/editor.js:1130-1148` draws a green `#8dff9d` arrow from the start control point along the
(direction-toggle-aware) tangent, with a `START` text label.

C++ previously drew **nothing**. The data was all present — `track.start`
(`EditorTrackDefinition.hpp:164`), `setStartPoint()` (`PropertiesPanel.cpp:157`), `clampStart()`, the
direction toggle in `TrackPropertiesPanel` — but there was no visual indication of where the track
starts or which way it runs, which made the direction toggle effectively unverifiable in the editor.

### Implementation (done)

`cpp/editor/src/TopDownCanvas.cpp`:

- `WorldFrame2D` gained `tangentX`/`tangentZ`; `sampleCenterlineAtG` interpolates them like every
  other field.
- New `drawStartMarker(drawList, canvasOrigin, view, baked, start)` samples the baked centerline at
  `start.point` (treated directly as `g`, matching JS's `evalTrack(track.start.point)`), negates the
  tangent when `start.reverse`, and draws a 22px line + filled triangle arrowhead
  (`ImDrawList::AddTriangleFilled`) + `AddText("START")`, using the same screen-space heading
  convention JS uses (`atan2(dirX, dirZ)`).
- Called from `DrawTopDownCanvas` right after `drawPhysicsPoints`, matching JS's draw order
  (`js/editor.js:1130`, right after the physics-point dots).
- Bounds safety: `drawStartMarker` checks `start.path` against `baked.paths.size()` itself; `g` is
  clamped by `sampleCenterlineAtG` regardless of `start.point`'s value, so no separate
  `EditorState::clampStart()` call is needed (that method is private; `EditorState`'s own mutators
  already call it after every structural edit, so `track().start` is valid by draw time).

As anticipated: JS computes the start frame analytically via the authored evaluator (`startFrame()`,
`js/editor.js:652`), while this samples the baked centerline instead — consistent with every other
on-canvas frame lookup already in this file.

---

## 8. Top-down control-point styling

JS encodes four things in each node (`js/editor.js:1166-1200`) that C++ drops — it draws uniform
yellow circles (`kPositionPointColor`, `TopDownCanvas.cpp:45`, used at `:803`) and makes **no
`AddText` calls on the canvas at all**.

| Encoding | JS | C++ |
| --- | --- | --- |
| **Square** node = open-path endpoint (join-eligible) | `ctx.rect` when `isEndpoint`, `:1183` | always a circle |
| Fill = `heightColor(y)`, elevation-coded | `:1186` | flat yellow |
| Amber `#ffcc44` ring **+ X cross** = disjoint seam point | `:1188-1197` | nothing |
| Text label `pi.i (y123)` | `:1199` | nothing |

C++ already ports `rollColor`/`elevationColor` (`TopDownCanvas.cpp:121-128`) for the ribbon, so the
elevation-coded fill is a near-free addition; `heightColor` is the remaining one to port.
Endpoint-square and seam detection both have C++ equivalents in `EditorState`
(`hitTestOpenEndpoint`, `disjointSeams()`), so this is mostly draw-side work.

---

## 9. Elevation view fidelity — ✅ Implemented

| Feature | JS | C++ |
| --- | --- | --- |
| Y axis with `niceAxisStep` ticks + labels | `drawElevYAxis` `:1360`, `niceAxisStep` `:1342` | **ported** |
| Labelled dashed zero line | `:1464-1477` | **ported** |
| X axis = true cumulative XZ **arc length** | `:1441-1451` | **ported** |
| Closed-loop echo node (`0↺`, faded) | `:1510-1526` | **ported** |
| Per-node index labels | `:1525-1526` | **ported** |
| `heightColor` node fill | `:1520` | **ported** |
| Disjoint amber ring | `:1521-1523` | **ported** |
| Respects the Position point filter | `:1508`, `handleAtElev:2694` | **ported** |

### Implementation (done)

`cpp/editor/src/ElevationView.cpp`:

- New `ArcProfile`/`buildArcProfile(...)` measures cumulative XZ arc length along the baked
  centerline each frame (closed paths get one extra trailing entry for the wrap back to frame 0).
  `xFracForPositionIndex(...)` and its inverse `gAtArcFraction(...)` replace the old order-based
  `screenX`/`xAxisFraction` pair everywhere — plotted points, the baked profile line, hover/click
  hit-testing (`nearestPointIndex`), and right-click-to-insert (gap 5) all now agree on true
  arc-length placement instead of authored order. Falls back to plain order-based spacing when no
  baked centerline is available yet, matching this file's existing "baked may be null" tolerance.
  `ElevationView.hpp`'s header comment, which documented the order-based simplification, is updated
  to describe the arc-length axis instead.
- `niceAxisStep(...)` ported 1:1 from JS; `drawYAxis(...)` replaces `drawAxis(...)`, drawing a full
  gridline + label per tick plus a dashed, distinctly-labelled zero line (`addDashedHLine`), mirroring
  `drawElevYAxis` and its zero-line block.
- `heightColor(...)` ported 1:1 from JS's own function (already used for the top-down ribbon;
  now also used for this view's node fill).
- Node draw loop now iterates `slots = closed ? n + 1 : n` (the extra slot is the closed-loop echo
  of point 0, ~45% alpha, labelled `0↺`), draws a per-node index label under each handle, and a
  thicker amber stroke (no cross, matching JS's ring-only disjoint styling in this view specifically)
  when the point is a disjoint seam (`state.disjointSeams()`).
- Both the height-handle rendering and hit-testing are now gated on `showPositionPoints`, closing
  the point-filter inconsistency gap 5's implementation left partially open (that one only gated
  right-click-to-insert).

---

## 10. Track statistics readout

JS's `#metaHint` (`updateMeta`, `js/editor.js:2526-2546`) shows path count, unique position-point
count, and disjoint-seam / junction counts. C++'s "Diagnostics" panel (`main.cpp:2195`) is
development smoke-test output, not track statistics.

JS also offers a "Remove stale disjoint metadata" button when it detects stale references — **this
half is genuinely not needed**: C++ calls `pruneStaleReferences()` after every structural mutation
(`EditorState.hpp:1743`), so staleness can't accumulate. Only the statistics line is missing.

---

## Confirmed non-gaps

Checked and found to be present, equivalent, or deliberately absent — **read this before starting
work**, it is why several obvious-looking features aren't listed above.

- **Roll line / diamonds in the elevation view.** `editor.html:295`'s badge advertises this, but the
  badge is stale: `rollHandleAtElev` returns `null` unconditionally (`js/editor.js:2721-2723`) and
  `drawElev` states *"Roll is intentionally not drawn in the side view; edit roll control points from
  the top-down view"* (`:1504-1506`). C++ matches actual JS behaviour. Don't implement this.
- **Create mode.** Full parity: `createModeClick` / `finishCreateDraft` / `cancelCreateDraft`, the
  ≥4-point guard, click-first-to-close, click-last-to-finish, right-click-cancel, the dashed draft
  preview, and auto-return to Edit mode. Neither editor has a dedicated "New Curve" button — entering
  Create mode *is* how a new curve is started.
- **Join / Connect.** C++ uses explicit Curve/End dropdowns in the Curves panel
  (`CurvesPanel.cpp:61-103`) instead of JS's shift-click-two-endpoints + toolbar button. Equivalent
  and arguably clearer. (The shift-**drag** variant is separate — see gap 6.)
- **Zones, triggers, handling, random generation + ranges, textures (browse/bundled/tile
  size/assign/clear/delete), USD export, MppModel export, import/export JSON, paste & import mesh,
  rails mode, disjoint/reconnect, set-start-point, segment delete, split point, physics-sample
  overlay + read-only inspector, grid/snap/render-mode/point-filter toggles, cross-section
  preview.** All present.
- **"Open Game ↗" link, `persistEditorTrack` localStorage.** Browser-preview plumbing with no native
  equivalent.
- **`heightColor`-coded elevation nodes, hover rings, zoom-to-selection, D/Z/X hotkeys.** The last
  three are C++ *additions* with no JS counterpart.
