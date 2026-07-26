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

## 1. Self-intersection crossing markers & override cycling — ✅ Implemented

**Was the largest gap.** A track that overlaps itself needs the author to say, per crossing,
whether the inner loop is collapsed away or kept as real geometry.

| Piece | JS | C++ |
| --- | --- | --- |
| `detectPathCrossings(controlPoints, closed, edges, wrapOpen)` | `js/editor.js:823` | **ported** |
| `crossingState(cr)` — resolves override vs. default `span <= 100` rule | `js/editor.js:806` | **ported** |
| `crossingOverrideFor(a, b, side)` | `js/editor.js:801` | **ported** |
| `CROSSING_COLORS` | `js/editor.js:812` | **ported** |
| `crossingMarkerAtTop(sx, sy)` — hit test, `CROSSING_HIT_RADIUS = 11` | `js/editor.js:837` | **ported** |
| `cycleCrossingOverride(cr)` — `auto → keep → collapse → auto` | `js/editor.js:849` | **ported** |
| Marker draw loop (filled disc = collapsed, hollow ring = kept) | `js/editor.js:1106-1130` | **ported** |
| Mousedown dispatch (before `nodeAtTop`, skipped when shift held) | `js/editor.js:3278-3281` | **ported** |

### Implementation (done)

**Core side.** `removeSelfLoops` (`cpp/core/src/TrackBake.cpp`) sat in an anonymous namespace and
only ever found ONE crossing per pass (bounded to `span <= DEFAULT_SELF_INTERSECTION_SPAN` unless an
override forces a farther one), which isn't enough to show every crossing including far ("auto-keep")
ones. Rather than surfacing that bounded/iterative search directly, it now also runs a separate,
**unbounded** full pairwise scan over the same pre-collapse `points` (mirroring `js/track-core.js`'s
own `findSelfIntersections`, which is likewise a distinct, unbounded scan from
`removeLocalSelfIntersectionLoops`'s bounded one) before its existing collapse loop begins, filling a
new out-parameter:

- `tox::SelfIntersection{side, a, b, span, point}` (`Track.hpp`) — exactly the shape this doc
  originally sketched. Does **not** store forced/auto state: like JS's `crossingState()`, that's
  re-derived at draw time from `span` plus `TrackDefinition::selfIntersectionOverrides`, so cycling
  an override never needs re-detection.
- `tox::Track::selfIntersections` — every crossing across every path/side, populated by `bakeTrack`
  (`TrackBake.cpp`) into the new out-param on each `removeSelfLoops` call, skipped (matching JS's
  `hasBranchConnection` skip) for branch-connected paths exactly as before.
- The hardcoded `100` moved out of `TrackBake.cpp`'s anonymous namespace into a new public
  `tox::TrackCore::DEFAULT_SELF_INTERSECTION_SPAN` (`TrackCore.hpp`), shared by the collapse pass's
  own bound check, `decision()`'s default rule, and the editor's `crossingStateFor` (see below) — one
  constant instead of two independently-hardcoded `100`s.
- `Track::fromJson`/`fromFile`/`bakeTrack` gained a `detectSelfIntersections = true` default
  parameter gating the (expensive, O(N²)) unbounded scan — on by default for every existing call site
  (game, tests, parity harnesses: a one-time cost per load), off only for the editor's own live-preview
  rebake while a drag is in progress (see below). This is additive to every prior signature via the
  default argument, not a breaking change.

**Editor side**, `cpp/editor/src/TopDownCanvas.cpp`:
- `drawCrossings(...)`, called right after `drawPhysicsPoints` and before `drawStartMarker` (matching
  JS's own draw order — physics dots, then crossings, then the start marker), draws JS's disc-vs-ring
  convention and four colours exactly, plus the dark contrast halo.
- `crossingAtLocal(...)`, alongside `physicsPointAtWorld`, same nearest-within-radius idiom
  (`kCrossingHitRadiusPx = 11`, matching `CROSSING_HIT_RADIUS`).
- Dispatch in `handleEditModeInput`'s mousedown branch, checked after the roll/width/cross-section
  handle hit-test (matching JS's priority order) but before the position-point hit test, and skipped
  when shift is held (shift is reserved for the rubber-band gesture, gap 6).

`EditorState::cycleCrossingOverride(side, a, b)` — unconditional undo push, then insert
`{side, a, b, "keep"}` / promote to `"collapse"` / erase, mirroring `js/editor.js:849-861` exactly,
including the order-insensitive `(a,b) == (b,a)` match on both sides.

**Caching**, `cpp/editor/main.cpp`: the unbounded scan only runs when `!editorState.dragging()`
(mirroring JS's `if (!dragging)` guard around `detectPathCrossings`) — `rebake()`'s per-frame call
during an active drag passes `detectSelfIntersections = false` and copies the last good result
(`cachedCrossings`, updated only on a non-dragging bake) back onto the fresh `Track`, so markers stay
visible (briefly stale) mid-drag instead of flickering empty, without paying the O(N²) cost every
dragged frame.

---

## 2. Point type conversion ("Type" dropdown) — ✅ Implemented

Convert a selected control point between `position` / `roll` / `width` / `crossSection`, taking its
new value by evaluating the points' splines of the target kind at the removed point's `t` so nothing
jumps.

- JS: `convertSelectedPoint(newType)` at `js/editor.js:1540-1612`; the dropdown is built by
  `typeSelectRow`/`wireTypeSelect` inside `renderProps` (`:2135-2143`).

### Implementation (done)

`EditorState::convertBlockedReason(PointKind)`/`convertSelectedPoint(PointKind, positionXYZ)`
(`cpp/editor/include/EditorState.hpp`) plus a `Type` combo at the top of `DrawPropertiesPanel`
(`cpp/editor/src/PropertiesPanel.cpp`), shown for every selected point kind, matching where JS's
`typeSelectRow` appears in each of its four props branches.

- `convertBlockedReason` ports the three guards verbatim: shared/disjoint Position point
  (`sharedPositionOccurrences(id) > 1`, the id-occurrence-across-paths equivalent of
  `countPointOccurrences`), a Position path at the 4-point floor, and an aux kind at its own
  2-point floor. Returns a reason string (or `nullptr` if allowed) instead of JS's `alert()`; the
  combo disables the entry (`BeginDisabled`) and shows the reason as a tooltip on hover, the
  established idiom here (mirrors `MeshPanel.cpp`'s rail-height tooltip).
- Realized that JS's "remove first, then evaluate against `remaining`" ordering never actually
  changes anything the evaluation reads: the guard above already forces `curKind != newKind`, so
  removing the point being converted can never affect the target kind's own point list (a Position
  point's removal doesn't touch Roll/Width/CrossSection lists, and vice versa). `convertSelectedPoint`
  therefore evaluates directly against the path's current points of the target kind, with no
  remaining-points recomputation step.
- A pure port of `track-core.js`'s `evalScalarSpline` (the non-uniform Catmull-Rom/Hermite spline
  `TrackCore.evalRoll`/`evalWidth`/`evalCrossSection*` all wrap — a separate, simpler per-attribute
  spline, unrelated to the rational position spline core's baker uses) lives as a private static
  helper on `EditorState`, used only here.
- Converting **to** `position` needs the new XYZ. As anticipated, no authored-spline evaluator
  exists in the editor, so `PropertiesPanel.cpp` samples the baked centerline instead (a new
  file-local `sampleCenterlinePositionAtG`, the same per-file-duplicated-evaluator pattern
  `TopDownCanvas.cpp`'s `sampleCenterlineAtG` and `ElevationView.cpp`'s `sampleCenterlinePosAtG`
  already establish) at `g = t * gMax`, mirroring the context-menu "Position" item's own use of the
  same approximation. Turns out to be exact here rather than merely approximate: only
  Roll/Width/CrossSection points can convert *to* Position, and none of those affect the baked
  centerline's X/Y/Z, only the ribbon's cross-section — so sampling the current bake (computed with
  the about-to-be-removed point still present) is unaffected by its removal.
- The insertion index for a Position conversion is `round(t * (closed ? n : n-1))`, clamped to
  `[0, n]`, matching JS exactly; `positionIndexToRaw` (already used by `currentStartPointId`) maps
  that position-space index back to a raw `Path::points` index.
- `PropertiesPanel.cpp`'s `DrawPropertiesPanel` returns immediately after a conversion mutates the
  track, rather than falling into the (now-stale, since `path.points` was just erased-from/inserted-
  into) per-kind field-drawing switch below it — the next frame redraws the newly-converted point's
  fields fresh instead.

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

## 8. Top-down control-point styling — ✅ Implemented

JS encoded four things in each node (`js/editor.js:1166-1200`) that C++ previously dropped — it drew
uniform yellow circles and made no `AddText` calls on the canvas at all.

| Encoding | JS | C++ |
| --- | --- | --- |
| **Square** node = open-path endpoint (join-eligible) | `ctx.rect` when `isEndpoint`, `:1183` | **ported** |
| Fill = `heightColor(y)`, elevation-coded | `:1186` | **ported** |
| Amber `#ffcc44` ring **+ X cross** = disjoint seam point | `:1188-1197` | **ported** |
| Text label `pi.i (y123)` | `:1199` | **ported** |

### Implementation (done)

`cpp/editor/src/TopDownCanvas.cpp`'s `drawAuthoredPositionPoints(...)`:

- `heightColor(...)` ported 1:1 from JS (a second, file-local copy alongside `rollFillColor`/
  `elevationFillColor` — matches ElevationView.cpp's own independent copy of the same function, per
  this codebase's established per-file duplication of small color helpers) and used as the node fill
  in place of the old flat `kPositionPointColor`.
- `isEndpoint` reuses the already-computed `firstPosRaw`/`lastPosRaw` raw indices (previously only
  used for the weld-target check) to match JS's `i === 0 || i === cps.length - 1` on an open path;
  `AddRectFilled`/`AddRect` draw the square in place of `AddCircleFilled`/`AddCircle` for that case,
  fill/stroke logic otherwise identical to the circle branch.
- `isDisjoint` looks up `state.disjointSeams()` (now threaded into the function, new parameter) by
  matching `Connection::pointId` against the point's id, mirroring `seamForPoint(p)`. When true and
  not selected, the stroke becomes the amber `kDisjointColor` at 3px (matching JS's stroke choice),
  and a separate amber X cross is drawn on top via two `AddLine` calls sized off `radius + 4`,
  matching JS's separate cross stroke pass exactly.
- The index/elevation label (`"pi.i (yNN)"`) is drawn via `AddText` at `screen + (9, -5)`, offset to
  approximate JS's canvas-space `(s.x + 9, s.y + 3)` baseline-anchored text under ImGui's
  top-left-anchored `AddText`.

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
- **Hover rings, zoom-to-selection, D/Z/X hotkeys.** C++ *additions* with no JS counterpart.
