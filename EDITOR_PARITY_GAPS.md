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

## 3. Mesh region property editor

A selected mesh region has no panel at all in C++.

| Field | JS (`renderProps`, `js/editor.js:2192-2199`) | C++ |
| --- | --- | --- |
| X, Z | number inputs, step 0.5 | canvas drag only |
| **Elevation** | number input **and** the elevation-view drag (gap 4) | **unreachable** |
| Rotation° | number input, step 5 | shift+drag on canvas only |
| **Rail height** | number input, per-**asset** (affects every placement) | **unreachable** |
| Rail count hint | `N of M edges railed` | — |
| Delete | `#delMeshBtn` | Delete key only |

`EditorState` has `findMeshPlacement()` (`:109`) and `deleteSelectedMesh()` (`:1008`) but **no
setter for elevation, rotation, or rail height**, so those three need adding alongside the panel.

### Implementation

New `cpp/editor/src/MeshPanel.cpp` + `include/MeshPanel.hpp`, registered as a `CollapsingHeader` in
`main.cpp`'s `Panels` window (`:2154-2195`) — put it after "Point Properties". Follow `ZonesPanel.cpp`
exactly: `InputDouble` with `kCommitOnEnter`, read-modify-commit through an `editX()` mutator that
pushes undo once.

Rail height lives on the **asset**, not the placement (`track().meshAssets`, `EditorState.hpp:918`) —
label it as applying to every placement, as JS does via its `title` attribute.

---

## 4. Elevation view: draggable mesh elevation line

The only way to set a mesh region's elevation in JS, which makes this and gap 3 a natural pair.

- Draw: `js/editor.js:1479-1489` — a full-width horizontal line at the placement's elevation,
  labelled `<asset>  y <elevation>`, brightened while dragging.
- Drag: `dragging === 'meshElev'`, picked on vertical proximity alone (`MESH_ELEV_PICK_PX`,
  `js/editor.js:3562-3571`), applied at `:3416`.
- The plotted Y range is expanded to include the placement's elevation (`js/editor.js:1410-1415`) so
  the line stays on-panel even when the region sits far above or below the curve.

### Implementation

In `cpp/editor/src/ElevationView.cpp`:

- Extend `rawYRange()` (`:57`) to fold in `state.findMeshPlacement(*state.selectedMeshId())->elevation`
  when a mesh is selected — this mirrors JS's `:1410` and matters for the same reason.
- Draw the line + label after `drawBakedProfile` (`:185`).
- Add a hit test before the existing position-point test in the mousedown block (`:194`), and a drag
  branch alongside the existing one (`:201`).

The `frozenLayout` mechanism (`:160-172`) already handles drag-time scale stability and should wrap
this drag too — same runaway-sensitivity failure mode otherwise.

---

## 5. Elevation view: right-click to insert a position point

`insertPositionAtSide(sx, sy)` (`js/editor.js:2806-2826`, wired at `:3557`) inserts a new position
control point at the clicked arc position, taking X/Z from the curve there and **Y from the click
height**. C++'s elevation view only selects and drags existing points.

### Implementation

`ElevationView.cpp` currently declares its `InvisibleButton` with `ImGuiButtonFlags_MouseButtonLeft`
only (`:173`) — add `| ImGuiButtonFlags_MouseButtonRight` first, or the click never arrives.

Reuse `EditorState::insertPositionOnSegment(...)`, which the top-down context menu already calls
(`TopDownCanvas.cpp:1424`). Deriving the insertion index from the clicked x is the same
`g = t * gMax; insertAt = floor(g) + 1` arithmetic used there and in JS's `:2817-2819`.

Guarded in JS by `pointFilters.position` and by the click being inside the plot gutter
(`x >= padX && x <= w - padX`) — worth keeping both.

---

## 6. Shift-drag endpoint: rubber band → connect, or extend the curve

JS shift-drags an open curve's endpoint with a live rubber-band line — yellow `#ffd23c` while
free, green `#31d66b` and snapped to the node when over a valid drop target
(`js/editor.js:1207-1220`). On release:

- over a target → `joinSel = [from, target]` then `performJoin()` (`:3536-3538`);
- in empty space, past `JOIN_DRAG_MIN_PX` → **`extendCurveFromDrag(from, screenPos)`** (`:2862-2880`),
  which appends a brand-new point extending that curve, inheriting the dragged endpoint's elevation.

C++ has plain drag-to-weld (drag a selected endpoint onto another endpoint, `TopDownCanvas.cpp`
`weldTarget` machinery) — no shift modifier, no rubber band, and **no extend-into-empty-space**.

Extend-on-release is the valuable half: it is the only way in JS to grow an existing open curve
without re-entering Create mode. `EditorState::insertPositionOnSegment` plus the existing
`hitTestOpenEndpoint`/`weldTarget` state gets most of the way there; the new part is the
release-in-empty-space branch and the minimum-drag-distance guard.

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

## 9. Elevation view fidelity

| Feature | JS | C++ |
| --- | --- | --- |
| Y axis with `niceAxisStep` ticks + labels | `drawElevYAxis` `:1360`, `niceAxisStep` `:1342` | min/max text only |
| Labelled dashed zero line | `:1464-1477` | none |
| X axis = true cumulative XZ **arc length** | `:1441-1451` | authored **order** index |
| Closed-loop echo node (`0↺`, faded) | `:1510-1526` | none |
| Per-node index labels | `:1525-1526` | none |
| `heightColor` node fill | `:1520` | flat colour |
| Disjoint amber ring | `:1521-1523` | none |
| Respects the Position point filter | `:1508`, `handleAtElev:2694` | ignored entirely |

The arc-length x-axis is the substantive one: `ElevationView.hpp:5-13` documents the order-based
spacing as a deliberate, reversible simplification that holds "for any path that hasn't had points
reordered independently of their spline placement". That assumption is still true today, so this is
fidelity work rather than a correctness bug — but it is what makes the JS profile line up with the
curve underneath it.

The point-filter gap is a real inconsistency: `DrawElevationView` doesn't receive a `TopDownView`, so
it cannot see the filters that the toolbar checkboxes and the top-down canvas both honour.

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
