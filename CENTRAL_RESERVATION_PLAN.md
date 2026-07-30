# Central Reservation Plan — median strip on a track path

Status: **M0-M4 complete, M5 partial (core tests done, editor tests
deferred). Post-M5 bugfixes in §4–§4e: wall/hole alignment, three separate
reverse-gear/corridor-wall physics bugs (§4b–§4d, the last being the actual
"ship gets blocked" cause), and the carved hole's resolution (§4e) and edge
smoothness (§4f).**
This document is updated after each milestone lands.
Native C++ only (`cpp/core` + `cpp/editor`). The former `web/` JS reference implementation has
since been retired and removed from the repository entirely.

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
  - The loader now accepts schema **10 or 11** (not just 11): the committed golden
    fixture corpus is permanently version 10 with no `reservations`
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
    across each reservation's span into the render mesh. **Superseded by §4e:**
    a fixed count snapped to the nearest raw sample was both too coarse and
    never landed on t0/t1; the span is now subdivided against the taper itself,
    at exact `t`.
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
- [x] **M3 — Native editor UI (panel only).** `EditorState` mutation helpers
  (add/remove/update with clamp + non-overlap validation — undo/redo is free
  via existing whole-document snapshots); new `ReservationsPanel` (list +
  t0/t1/width fields, mirrors `TriggersPanel`); wired into `main.cpp`.
  - `TopDownCanvas`'s on-canvas preview of the gap/wall footprint (deferred at
    the time) is now done, in `drawBakedPath`/`drawReservationWalls`: the
    road ribbon carves the gap out of its own quads per centerline segment
    (reusing `Frame::reservationHalfGap`, corner-wise per sub-quad exactly
    like `TrackBake.cpp`'s §4f fix, just at physics-sample density rather
    than the render mesh's finer subdivision), and the two tapered boundary
    curves draw from the synthetic region's own baked `rails` — no separate
    evaluator or placement transform needed, since both are already in world
    space. Still read-only, matching every other aux-point/zone/trigger
    preview in this file: no on-canvas click-to-place or drag, values are
    still authored through the panel only.
  - `clampReservation`'s non-overlap logic clamps into the free `[lo,hi]` gap
    around the edited entry's own midpoint (bounded by whichever other
    reservations are nearest on each side) — guarantees non-overlap without
    ever touching another entry.
  - **Per-end cap style (Joined/Mitred/Rounded).** Each end (t0, t1) now picks
    independently between the original taper-to-a-point (Joined), a taper
    that flattens into a given full-width cap with a visible corner (Mitred),
    or the same cap with that corner smoothed off instead (Rounded).
    `ReservationEndCap{style, width}` lives on `ReservationDefinition`/
    editor's `Reservation` as `endCap0`/`endCap1`; JSON keys of the same name,
    omitted (defaulting to Joined/0) for backward compatibility with every
    track authored before this existed. `TrackBake.cpp`'s
    `reservationHalfGapAt` first bakes `baseWidth`, the original 0 → width →
    0 taper (via `hermitePair`, a two-segment cubic Hermite reproducing the
    old three-point `scalar()`/Catmull-Rom output bit-for-bit — so an
    all-Joined reservation still bakes identically to before), then floors
    each half of the span against that end's cap. Mitred uses a hard
    `max(baseWidth, capWidth)`: a flat shelf ending in a blunt cut of exactly
    `capWidth`. Rounded uses that same shelf, except its last `noseLength`
    **metres** are replaced by `roundedNoseWidth`, a half-ellipse (`capWidth`
    across, `noseLength` along the path) closing the void to a point.
  - **Why the nose is measured in metres, and why that matters.** The
    vertical tangent at the tip is the entire visual signature of a rounded
    end — it is what makes the boundary flare outward like a dome instead of
    stopping square. Two earlier attempts got this wrong by shaping the
    profile in *t*-space: forcing a zero slope at the cap via Hermite
    tangents, then a `smoothstep` crossfade across the whole half-span. Both
    were measured (scratch script replicating the bake) and both produced a
    profile that was **flat at the tip with exactly zero slope** — which is
    precisely what Mitred's shelf looks like, so the two styles were
    indistinguishable; their peak difference was ~0.7 m of width nearly
    1 km from the end, nowhere near the cap. Sizing the dome in world metres
    is what actually separates them: at `capWidth` = 4 m, Mitred is 4.000 m
    wide at the tip with zero slope, Rounded is 0.000 m flaring at ~18 m per
    metre.
  - **`noseLength` is decoupled from `capWidth`.** A geometrically-honest
    dome is a half-*circle* of radius `capWidth/2` — only ~2 m long on a
    reservation running hundreds, which is below the ~6 m physics-centerline
    spacing the editor preview's road fill walks. Measured: at a 2 m nose
    exactly one sample lands inside the dome, so the fill jumps straight to
    the full cap width and renders identically to Mitred; at 40 m, seven
    samples land inside it and the curve shows. So the dome is a half-
    *ellipse* — `capWidth` across, `noseLength` along the path — with
    `noseLength` authored per end. `noseLength <= 0` falls back to the
    circular `capWidth/2` (also what a file predating the field means), and
    `ReservationsPanel` seeds `kDefaultNoseLength` = 40 m the first time an
    end is switched to Rounded so it is visible without hand-tuning. No upper
    clamp is needed: the bake maxes the dome against the base taper, so an
    over-long nose is simply swallowed by it.
  - Sizing the nose in metres means the bake needs the path's driven length,
    which `length()` measures off the centerline that `center()` produces —
    a cycle. Broken by splitting out `centerRaw()` (centerline without the
    reservation carve applied); positions never depended on the carve
    (`applyReservationGap` only writes `reservationHalfGap`/
    `reservationIndex`), so this is bit-identical, as the parity corpus
    confirms.
  - `adaptiveRenderBake` also places `kNoseRings` forced rings across each
    Rounded dome explicitly. Its `splitSpan` bisects the whole reservation,
    so a short dome inside a 1600 m half-span would exhaust the depth-10
    budget before resolving one and round off to the same blunt cut as
    Mitred. The rings are spaced by the ellipse's own parameter
    (`d = noseLength * (1 - cos θ)`) rather than uniformly, putting most of
    them at the tip where it turns hardest and none along the near-flat run
    where it meets the shelf.
  - Cap width is clamped to at most the reservation's own midpoint `width`.
    `reservationGeometry` gained a `capWall` closer: a Joined end's
    zero-width taper self-seals as before, and so does a Rounded end (its
    dome closes to a point), but a Mitred end leaves the void open at its
    cap's full width, so a rail + wall triangle pair is added across any end
    whose left/right boundary points don't already coincide. No
    `TopDownCanvas` changes needed — its preview reads purely off
    `Frame::reservationHalfGap` and the region's baked `rails`, so the cap
    shapes render automatically. The preview's *road fill* walks the physics
    centerline (~6 m spacing) and so only resolves a dome longer than that —
    the reason `noseLength` defaults to 40 m rather than the circular
    `capWidth/2`; the violet boundary drawn from the region's `rails` comes
    from the fine render bake and shows the dome at any length.
    `ReservationsPanel` gained a style combo, a cap-width field, and a
    nose-length field (Rounded only) per end.
  - **`wallHeight` — configurable visual and physical barrier height.**
    `ReservationDefinition`/editor's `Reservation` gained `wallHeight`
    (metres, `<= 0` means "use `TrackCore::DEFAULT_RAIL_HEIGHT`", also what a
    file predating the field means — same convention as the end-cap fields
    above). `reservationGeometry` sets the synthetic region's
    `MeshRegion::railHeight` from it instead of the hardcoded default, which
    is enough on its own to make it both a visual and a physical setting: the
    wall's render triangles are already extruded by `region.railHeight`
    (unchanged), and Ship.cpp's ground-clearance check
    (`p.groundPos.y >= region.elevation + region.railHeight`) already reads
    the same field for every `MeshRegion` including this one — no separate
    physics wiring needed. `ReservationsPanel` gained a "Wall height (0 =
    default)" field alongside Width.
  - **`widthMode` — Fixed metres vs Percent of the road.** `width` can now be
    authored either as an absolute metres value (`Fixed`, the original and
    default behavior) or as a percentage in [0,100] of the road's own width
    (`Percent`) — and Percent tracks the road continuously across the span,
    not just at one sampled point: if the road itself narrows or widens
    partway through the reservation, the void's peak narrows or widens with
    it. End-cap widths are unaffected either way; they stay an absolute
    metres value regardless of the reservation's own mode (kept simple since
    it wasn't what was asked for — the cap floor already clamps against the
    reservation's own local peak, so it works correctly under either mode).
  - Implemented by splitting the old width-scaled Hermite taper into a
    unitless 0 → 1 → 0 shape (`taper`, peak of 1 rather than `r.width`) times
    a `peakHere` evaluated *at the queried t*: `r.width` in Fixed mode
    (constant, reproducing the old curve exactly — Hermite interpolation is
    linear in its value/derivative parameters, so scaling all of them by a
    constant scales the whole curve by that constant), or
    `(r.width / 100) * roadWidth` in Percent mode, where `roadWidth` is a
    parameter `reservationHalfGapAt` already received (the frame's own,
    already-baked, un-carved width at this exact t — no new plumbing needed).
    Mitred/Rounded cap floors clamp against `peakHere` rather than the raw
    `r.width` field, since in Percent mode that field is a 0-100 number, not
    metres. The nose-ring placement heuristic in `adaptiveRenderBake`
    (`kNoseRings`) had the same bug (re-deriving a metres cap width from
    `reservation.width`, wrong once that's a percentage) — fixed by reusing
    the anchor's own already-computed, already-percent-aware half-gap instead
    of re-deriving it.
  - `ReservationsPanel` gained a "Width mode" combo; picking it flips the
    Width field's label and unit, and auto-converts the current number
    (via `PropertiesPanel.hpp`'s `widthAtT`, sampled at the reservation's own
    midpoint) so the reservation's actual size stays roughly put across the
    toggle rather than being silently reinterpreted under the new unit —
    "8" suddenly meaning 8% instead of 8 m would shrink an authored
    reservation to a sliver. Percent mode also shows the resolved metres
    value alongside the field. `DrawReservationsPanel` gained a `baked`
    parameter for this (same baked track `TriggersPanel.cpp` already samples
    for its own auto-width preview).
- [x] **M4 — Debug overlay.** F1 overlay toggle for
  `GeometryKind::ReservationWall` in `StatePlayTungstenMonoxide.cpp`,
  alongside the existing `TriggerSurface`/`PathRail`/`MeshRail` toggles.
  - Unlike those (debug-only, default hidden), `mShowReservationWallsDebug`
    defaults to **true** — the wall is real gameplay geometry a player must
    see, not a debug aid; the checkbox exists to let it be hidden for
    inspecting the physics wall's alignment without the mesh in the way.
- [~] **M5 — Tests (core done, editor deferred).** `track_tests.cpp` covers
  schema version acceptance (10 and 11, rejects other), reservation parsing/
  clamping/dropping degenerate entries, and baking correctness (synthetic
  `MeshRegion` shape, `ReservationWall` batch present, no surface vertex
  lands in the void at full taper width). Not yet covered: a `Ship`-level
  physics scenario actually driving into the M2 wall (would need a
  `Simulation`/`Ship` harness scenario, not just loader/bake assertions),
  and `EditorState`'s reservation add/edit/delete/clamp methods have no
  dedicated test alongside `cpp/editor/tests/track_resource_tests.cpp`.
  Both are reasonable follow-ups, not done here given time/resource limits
  on this pass (this machine is memory-constrained for parallel MSVC builds
  — full-solution builds must run target-by-target, not `-m` all-core).

- [x] **M6 — Interior cap mode (Capped/Uncapped), from a 2026-07-30 grilling
  session.** Today a reservation's void is a half-measure: the top surface has
  a hole (M1), but the underside/`PathShell` mesh (built in `pathGeometry`
  whenever any frame has `crossSectionThickness > 0`) stays solid and
  uncarved underneath it, with no wall connecting the two — a naked,
  unintentionally non-manifold seam. This milestone makes the choice
  explicit and deliberate instead.

  **Data model.** New `ReservationInteriorMode { Capped, Uncapped }` and
  `interiorMode` field on `ReservationDefinition`/editor's `Reservation`.
  JSON key `"interiorMode"`, values `"capped"`/`"uncapped"`; missing/malformed
  defaults to **Capped** (the user's explicit call, not the safer-seeming
  Uncapped, despite Capped being the bigger behavior change for every
  existing track — see Testing below for the fallout). Also new
  `railClearanceHeight` (double, `<=0` means engine default, same convention
  as `wallHeight`): decouples the *visual* wall height (`wallHeight`, already
  shipped) from the *physics* jump-clearance height (today both read the one
  `wallHeight` value baked into `MeshRegion::railHeight`) — a tall visible
  wall with an easy clearance, or an invisible wall with a strict one, are
  both now expressible.

  **Render geometry (`TrackBake.cpp`).**
  - *Capped*: add a wall from the void's rim (the same tapered boundary
    curve `reservationGeometry` already builds for the above-road barrier)
    down to the shell's underside depth (`under(frame, point)`, i.e. offset
    by `-crossSectionThickness` along the frame's normal) at the same
    footprint, sealing the pit's sides. Material: `Tracks/DefaultShellMaterial`
    (the shell's own, not the reservation wall's `DefaultRailMaterial`) — the
    user's explicit requirement. The shell's own underside surface is
    *unchanged*: it's already solid and already serves as the floor once the
    sides are sealed, so no new floor render geometry is needed.
  - *Uncapped*: carve a matching hole in the shell's underside instead, using
    the exact same corner-wise gap-band algorithm the top surface's strip
    already uses (§4f) — mirrored in `v` and offset down by thickness. No
    connecting wall between the top rim and this new bottom rim: a genuine
    open shaft, deliberately non-manifold, per the user's own framing.
  - No-op wherever the path has no shell at all (`crossSectionThickness`
    zero everywhere in the reservation's span) — nothing to cap or carve.
  - Multiple reservations can share one path's single shell mesh, so the
    shell-building code becomes per-ring cap-mode-aware via the existing
    `Frame::reservationIndex` (already how the top surface knows which
    reservation, if any, owns a given ring).

  **Physics (`TrackBake.cpp`'s `reservationGeometry`, `TrackMesh.cpp`,
  `Ship.cpp`).**
  - *Uncapped*: unchanged from today — rails-only synthetic `MeshRegion`
    (`polygons` empty), no floor, a car falls through exactly as it does now.
  - *Capped*: the synthetic region additionally gets real `polygons`/
    `triangles` — a flat floor matching the void's tapered footprint
    (reusing the same boundary curve again), at **one** elevation (every
    `MeshRegion` carries a single scalar `elevation`; this isn't a
    reservation-specific limitation) sampled at the reservation's own
    midpoint t, mirroring the precedent the width-mode feature's editor
    preview already set for "the one representative point on a reservation."
    This makes a Capped region behave like a real placed mesh region for
    landing/ownership purposes, reusing Ship.cpp's existing landing code
    paths rather than adding new ones.
  - **Rails become one-directional for every reservation, Capped or
    Uncapped** (harmless for Uncapped, since nothing can ever be "inside"
    one to notice the asymmetry): block a car crossing from the track into
    the void, never the reverse. Needed so a car that has landed on a Capped
    floor can still drive back off it — today's bidirectional
    `slideAlongRails` would trap it there permanently. Implemented as an
    opt-in (a new `slideAlongRails` parameter, or a `MeshRegion` flag) that
    only reservations set; real placed mesh assets (ramps, platforms) keep
    today's bidirectional collision unchanged. Scoped to "is this a
    reservation" via the existing stable `region.id.rfind("reservation-", 0)`
    prefix match, **not** `polygons.empty()` — that trait stops reliably
    identifying every reservation the moment Capped ones carry polygons too,
    so Ship.cpp's existing `polygons.empty()` gate (the M2 "no floor, skip
    ownership/airborne checks" branch) needs to switch to checking
    `interiorMode`/floor-presence directly rather than emptiness once this
    ships.

  **Editor UI (`ReservationsPanel.cpp`).** A Capped/Uncapped combo (same
  pattern as the end-cap style combo) and a "Rail clearance height (0 =
  default)" field alongside the existing "Wall height (0 = default)" field.

  **Implementation notes (what actually shipped, vs. the plan above).**
  - The render/physics/UI plan above was built essentially as written: a
    shared `carveQuad` helper was pulled out of the top surface's existing
    corner-wise carve so the shell's new Uncapped carve (mirrored `v`,
    reversed triangle winding to face downward) could reuse the identical
    rule rather than risk the two hole shapes drifting apart. The Capped
    seal and physics floor both live in `reservationGeometry`, reusing the
    same `bounds` array (now carrying each ring's `crossSectionThickness`
    too) the above-road wall already builds from.
  - `MeshRegion` gained `railClearanceHeight` and `oneWayRails`.
    `compilePlacement` (real placed mesh assets) sets
    `railClearanceHeight = railHeight` unconditionally, so nothing changes
    for them; only `reservationGeometry` sets the two independently.
    `oneWayRails` defaults `false` everywhere except reservations. Ship.cpp's
    airborne-wall-clearance check now reads `railClearanceHeight` instead of
    `railHeight`, and its M2 corridor-wall loop's gate switched from
    `!region.polygons.empty()` to `!region.oneWayRails`, exactly as planned.
  - `slideAlongRails` (`TrackMesh.cpp`) gained the one-directional check: a
    crossing is only a "hit" when the movement direction's dot product with
    the rail's own normal has a particular sign. *Which* sign blocks entry
    vs. exit isn't derivable from reading the code — `MeshRail::nx/nz` is
    built per-boundary-curve, direction-of-travel-relative, with no obvious
    a-priori "into the void" convention. Resolved empirically: implemented
    one sign, ran the full suite (which already includes two physics
    diagnostics that drive straight at a reservation wall from the track
    side and assert a bounce), got a passing landing but a failing "drive
    back off the floor" case, flipped the comparison, reran — both the new
    exit case and the two pre-existing entry-blocking diagnostics passed.
    Recorded as `>= 0` skip (not `<= 0`) in the final code; treat that sign
    as load-bearing, not incidental, if this is ever touched again.
  - **A real, previously-latent gap surfaced while writing the landing
    test, not anticipated in the plan above**: the analytical corridor
    surface (`curvedSurfaceFrame`/`corridorContains`, Ship.cpp's airborne-
    landing fallback) is built purely from the road's lateral `sLeft`/
    `sRight` and has no notion of a reservation's void at all — it reads as
    solid ground running straight through the middle of one. Previously
    harmless (a reservation never had a floor to prefer over it), this
    directly broke a Capped floor: a ship falling toward it got caught by
    the phantom corridor surface first, since that surface sits *above*
    the Capped floor (which is lower by `crossSectionThickness`) and the
    landing code fell back to the corridor whenever `meshRegionAt` found
    the reservation's region but the ship hadn't yet reached its exact
    elevation *this frame*. Fixed by changing that fallback's condition
    from `else` to `else if (!landing)` — only ever consider the analytical
    corridor when no mesh region's footprint claims this (x,z) at all,
    letting gravity keep pulling the ship toward the region it's already
    over instead of snapping it to the wrong, higher surface. Verified via
    the new landing test *and* the full parity/raw_parity suites (this
    touches every `MeshRegion`'s airborne landing path, not just
    reservations) — both still pass, so real placed mesh assets are
    unaffected.
  - Tests: `track_tests.cpp`'s pre-existing M1 bake-correctness fixture
    (asserting a reservation region has no floor) got an explicit
    `"interiorMode": "uncapped"` added, since that assertion is now only
    true for Uncapped. New: a bake-level block checking a Capped region has
    non-empty `polygons`, `oneWayRails` true, `wallHeight`/
    `railClearanceHeight` independently readable, an `-interior-seal` batch
    present; and the matching Uncapped region has none of those (still no
    floor, no seal batch) plus confirms the shell batch exists to have been
    carved from. A physics block drops a ship onto a Capped floor, confirms
    it lands at the region's own (lower) elevation rather than the road's,
    then drives it sideways back out past the reservation's half-width and
    confirms it isn't trapped. The pre-existing M2/reverse-gear physics
    diagnostics (drive-into-wall-from-outside) needed no changes and still
    pass unmodified, confirming one-directional rails didn't disturb the
    entry-blocking behavior they test.

## 3b. Bugfix: half the reservation wall was non-collidable (post-M6)

**Report:** "central reservation collisions sometimes fail, especially when
the ship is travelling fast."

**Cause — a direct regression from M6's one-directional rails.** M6 made
`slideAlongRails` skip any rail whose normal doesn't oppose the direction of
travel. That promoted rail normal *orientation* from cosmetic to load-bearing:
before M6 rails blocked both ways, so which side a normal faced never mattered.
`reservationGeometry` (TrackBake.cpp) was deriving each rail normal as a bare
90° rotation of its own segment direction, `(dz, -dx)`. Both flanks of the void
are emitted in the same along-path direction (`addRail(bi.left, bj.left)` and
`addRail(bi.right, bj.right)` in one loop), so the identical rotation lands
*outward* on one flank and *inward* on the other. Measured on a straight test
track: right flank 92 outward / 0 inward, left flank 0 outward / 92 inward. The
one-way test therefore skipped every left-flank rail, and the entire left wall
was non-collidable — a car could drive straight into the median from that side
and keep going. The two end caps had the same defect against the path axis (both
emitted `left -> right`, so one of the pair faced inward).

The "at high speed" framing in the report is a symptom, not the mechanism: the
dead flank is always dead. Speed just governs how far across the road a car
travels, and so whether it reaches the median at all. Shallow approach angles
(tracking nearly parallel to the taper, which is how a car actually clips a
median at racing speed) were the first to expose it.

**Fix.** `addRail` now takes an explicit outward reference and flips the
computed normal to agree with it, rather than trusting segment direction:
flanks pass the across-void vector (negated for the left), and each end cap
passes the along-path vector away from its neighbouring interior ring. This is
orientation-, winding- and path-direction-independent.

**Note on a false start.** The first hypothesis was broad-phase tunnelling, and
`MeshRegion::withinBounds` was given a swept-segment overload (both Ship.cpp
collision gates passed only the destination point, so a fast enough ship could
in principle straddle a region's bounds in one frame without either endpoint
testing inside). That is a genuine latent bug and the overload was kept, but it
is *not* this report's cause and could not have been: a reservation's bounds
span tens to hundreds of metres while one frame at 140 m/s is ~2.3 m. The
narrow phase was being called all along and, once measured directly, was found
to block correctly — from the right flank only. Measuring the two phases
separately is what turned this up.

**Tests.** A new `track_tests` block asserts, on a Capped reservation: no rail
normal faces into the void (probing either side of each rail midpoint against
the region's own floor polygon — flank-agnostic, with a coverage assertion so
taper-tip rails that legitimately can't resolve don't hollow the check out);
`slideAlongRails` blocks a boundary crossing from *both* flanks and leaves the
ship outside; and a 32-case full-physics sweep (both sides × 8 approach angles ×
2 speeds, 400 steps each) never enters the void footprint. Verified to have
teeth by disabling only the orientation flip: 4 assertions fail, including 593
frames inside the void. The pre-existing M6 drive-off-the-floor test still
passes, confirming rails remain one-directional rather than silently reverted
to bidirectional blocking.

## 4. Bugfix: wall/hole alignment (post-M5)

**Report:** "ship physics breaks when near the central reservation."

**Root cause:** the reservation's physics wall (`reservationGeometry` in
TrackBake.cpp) was built from the fine physics centerline (`path.centerline`,
hundreds of uniform samples), while the *visible/exported* hole carved out of
`PathSurface` was built from the coarse, adaptively-resampled render mesh
(only ~9 forced anchor points across a reservation's span, per M1's
over-tessellation fix). These two curves could disagree right at the gap
edge: the analytical corridor surface (which has no notion of the
reservation at all — `sLeft`/`sRight` stay full-width by design) plus a wall
that didn't quite reach where the *exported* collision mesh's hole actually
was could leave a sliver where the game's `TrackCollisionSurface` (built
from that same coarse exported mesh, as every real native Track resource
has) finds no nearby triangle, kicking the ship spuriously airborne right
near the boundary.

**Fix:** `Frame` gained `reservationIndex` (which reservation, if any, is
active at this frame — needed because adaptive render frames, unlike the
physics centerline, aren't uniformly spaced in t, so it can't be recovered
from array index alone the way `reservationHalfGap` originally was).
`reservationGeometry` now takes the *same* `frames`/`edges` arrays
`pathGeometry` just carved the surface hole from, instead of the physics
centerline — the wall and the hole are now built from identical points by
construction, not just numerically close. This required reordering
`bakeTrack()`: `compileTrackMeshes` (which clears `track.meshRegions`) now
runs *before* the per-path loop instead of after, so `pathGeometry` can
append each reservation's synthetic region to the already-compiled list.

**Verification:** `track_tests.cpp` gained a regression test driving a ship
into a reservation's taper, both with and without a baked
`TrackCollisionSurface` (built from the actual carved surface batch, the
same way the real game runtime always has one). Asserts: position/speed
stay finite, a wall hit actually fires, speed recovers afterward (not a
grind-to-a-halt), and — the specific regression — airborne toggling near the
gap edge stays bounded (≤2 transitions) once a collision surface is
involved, rather than spuriously repeating.

**Not reproduced, noted as a separate pre-existing issue:** while
bisecting, an unrelated *flaky* segfault surfaced in
`cpp/editor/tests/track_resource_tests.cpp` (pre-existing, uncommitted work
this plan doesn't own) — passed 100% on ~10 repeated runs both before and
after this fix, so it isn't caused by this feature, but it's real and
intermittent; worth a look independently of this plan.

**Not reproduced, unrelated:** an unsteered car driving straight through a
sharp curve (no steering input at all) can grind to a permanent halt against
the *outer* corridor wall, bleeding speed to zero — reproduced identically
with zero reservations involved, so it's a latent issue in the pre-existing
outer-wall `hitSign`/`loS,hiS` restitution logic in `Ship.cpp`, not something
this feature introduced or is positioned to fix.

> **Diagnosed and fixed in 4d.** 4c (gear/speed) left this byte-for-byte
> unchanged — it is a *positional* defect, not a restitution one, and it turned
> out to be the same root cause as the reported reverse-gear blocking. See 4d.

## 4b. Bugfix: reverse-gear wall collision (the actual reported cause)

**Report (follow-up):** happens most when going backwards, in the area near
the reservation rather than exactly on it; user suspected the width being
different was a factor.

**Root cause:** M2's `slideAlongRails` collision response in `Ship.cpp`'s
grounded branch set `p.speed = std::hypot(vel.x, vel.z) * weightSpeedRetain(p)`
— always non-negative — and re-pointed `moveDir` to match, gated on
`p.speed > 1e-6`. A car backing (reverse gear, negative `speed`) into the
wall got flipped into forward gear on impact; holding reverse the whole
time then decelerated that new positive speed back down through zero and
into reverse again along the *rotated* post-bounce heading, driving the car
straight back into the same wall from a slightly different angle each
time — an unbounded bounce/reverse/rebounce loop. (The pre-existing outer
`sLeft`/`sRight` wall code has the identical pattern, so this likely isn't
new to reservations in principle — but backing into a median in the middle
of the road is a far more everyday maneuver than backing off the track's
outer edge, so reservations make it dramatically easier to trigger.)

**Fix:** preserve gear across the bounce — `speed = gear * hypot(vel) *
weightSpeedRetain(p)` where `gear = sign(pre-collision speed)`, and
`moveDir` reoriented to `unitVel * gear` (so `moveDir * speed` still
reproduces the actual post-bounce velocity vector) instead of `unitVel`
unconditionally. The `moveDir` update is now gated on the velocity
*magnitude* being nonzero, not on `speed > 1e-6` (which was never true
while reversing, leaving `moveDir` stale in the original bug regardless of
the sign fix).

**Verification:** a new `track_tests.cpp` regression backs a ship through a
reservation's tapered area for 1600 steps. Asserts: finite throughout, at
least one real wall hit occurs, speed never flips positive after a hit,
total hits stay bounded (≤3, not an unbounded loop), and the car makes
~700+ units of real net backward progress rather than stalling in place.
Confirmed by hand first: before the fix, 34-37 step bounce/rebounce cycles
repeated indefinitely near the wall; after, two hits total and the car
proceeds straight through in reverse at (still-negative) top speed.

## 4c. Bugfix: reverse-gear on the outer corridor wall, where width varies

**Report (follow-up to 4b):** reversing in the launcher + tungsten-monoxide
module still stutters and can get blocked "at certain place where the track
width is not constant".

**Root cause:** exactly the pattern 4b predicted would still bite — the outer
`sLeft`/`sRight` wall branch in `Ship.cpp`, untouched by 4b. Two distinct
defects in one statement pair:

1. **Speed/`moveDir` were rewritten even with no impulse.** `p.speed =
   vel.length() * weightSpeedRetain(p)` sat *outside* the `if (into > 0)`
   guard. Crossing `loS`/`hiS` is primarily a positional constraint, and it
   fires on a car that merely brushed a limit which moved *under* it — which
   is precisely what a narrowing section does, every frame, to anything
   tracking near its edge. So a car with literally zero wall contact still
   paid `weightSpeedRetain` (2%) per frame for as long as the road kept
   narrowing.
2. **Gear was discarded**, as in 4b: `vel.length()` is unconditionally
   non-negative and `moveDir.copy(vel).normalize()` re-points along travel.

The measured trigger, on a 1400m path narrowing 46m→16m with the car reversing
at the cap 4m inside the wide section's edge: for 348 frames it reverses dead
straight at -33 m/s while `hiS` shrinks past it; at frame 349 `hiS` (19.975)
crosses under the car's lateral offset (19.983). `into` is **exactly 0** — no
bounce whatsoever — yet `speed` flipped `-33` → `+32.34` and `moveDir`
inverted 180°. Held brake then decelerated that positive speed back through
zero while grip swung `moveDir` around to meet `forward` again: the car slewed
sideways and made almost no headway. Over 20s it covered 311m instead of 655m
and ended at 7.5 m/s.

**Fix:** move the `speed`/`moveDir` recomputation inside `if (into > 0)`, and
preserve gear there the same way 4b does (`speed = gear * mag *
weightSpeedRetain`, `moveDir = unitVel * gear`, gated on `mag`, not `speed`).
Applied to `cpp/core/src/Ship.cpp` (and, at the time, its since-retired JS reference
oracle); the mesh-platform `slideAlongRails` response in the
grounded branch got the same gear treatment, so reversing into a platform's
own rail behaves like reversing into a reservation or the outer wall.

**Verification:** after the fix the same run holds a steady -33 m/s straight
through the narrowing, sliding inward with `lateral` tracking `hiS` exactly,
and covers 655m of the ideal 660m. A new `track_tests.cpp` regression pins
this: finite throughout, speed never goes positive, never rises above -30
(i.e. no speed bled off a car that never drove into anything), and >640m of
net backward progress.

**Insufficient on its own — see 4d.** 4c is a real fix (the traces prove the
no-contact speed drain was happening) but it did *not* clear the reported
symptom. Verifying against the reporter's own track exposed a second, deeper
defect that 4c does not touch.

**Parity impact (deliberate, reviewable — see CLAUDE.md):** this changes
physics, so `npm run gen-traces` was rerun. Only 2 of ~26 committed traces
moved: `boost-circuit.json` and `raw-session/steps/raw-session-step-lap.json`.
The `boost-circuit` divergence is a clean demonstration of defect 1 — step 622
`speed` 134.97 → 137.72, a ratio of exactly 1/0.98, i.e. one frame's worth of
the spurious no-contact penalty. All parity gates pass with the worst ratios
*unchanged* from the values documented in CLAUDE.md (7.322e-5 baked-world,
0.01051 raw-track).

**Latent corpus landmine found and removed while doing this.** Regenerating
exposed a pre-existing knife-edge in the raw-session lap scenario:
`raw_session_step_parity` failed with C++ firing the lap one frame before the recorded reference
despite the two agreeing on position to ~1e-14. Cause: the finish trigger was
hosted at `t: 0.1` and that path bakes to exactly 400 centerline frames, so
`0.1 * 400 = 40` put the gate's plane *bit-exactly* on frame 40 — and a ship's
ground position is always `curvedSurfaceFrame(sample, s)`, which has zero
tangential offset from its own sample's `pos`. Whenever the ship snapped to
frame 40, `(pos - center).fwd` was therefore exactly zero in exact arithmetic,
leaving `detectTriggers`' strict `d1 > 0` decided by the last bit. The old
committed trace sat on this too (its closest approach to the plane was
**exactly 0.0**), having merely been lucky enough that both languages rounded
the same way. Fixed structurally by moving the host to `t: 0.10125`, midway
between frames 40 and 41 (~0.71m of a ~1.42m spacing); closest approach is now
3.1e-2 m. Any `t` that is an exact multiple of the frame spacing re-creates it.
Note this was only ever a *parity* fragility, not a gameplay bug — the trigger
still fires, just one frame later.

## 4d. Bugfix: the ship locks onto the corridor wall (the real blocking cause)

**Report:** "the fix did not work" — reversing in the launcher +
tungsten-monoxide module still stutters and blocks where the width changes.

**What 4c missed.** 4c was verified only against the *analytical* corridor.
tungsten-monoxide's `Map.cpp` always installs a `collisionSurface` built from
the exported `.mppmodel` triangles, so the shipped game runs a code path 4c
never exercised. Reproducing on the reporter's own `NewTrack.json` — whose
`starter-path` carries a violent width profile, 36m → 140.5m → 157m → 36m over
t ∈ [0.096, 0.179] — with a collision surface built exactly as `Map.cpp` builds
it, the ship reverses cleanly for ~300 frames and then **freezes permanently**
at one point while still indicating −32.34 m/s. 145m travelled of a 495m ideal.
No gear flips (4c holds), no airborne transitions.

**Root cause.** In the `forceCurrentWall` case, `Ship::step` deliberately keeps
`current` — the sample taken at the ship's **old** position — rather than
re-sampling at `newPos`, so that a ship laterally outside the corridor is
clamped by a wall it was actually next to. But the position is then rebuilt as

```
curvedSurfaceFrame(c, finalS) = c.pos + c.edgeRight*finalS + c.normal*lift
```

which carries **no tangential term at all**, and `s` — the only channel
`newPos` had into that expression — has just been clamped away by
`finalS = clamp(s, loS, hiS)`. So `groundPos_{n+1}` is a pure function of
`groundPos_n`, with velocity contributing *exactly nothing*. A ship already on
the wall maps to itself: a fixed point it can never leave, at any speed. It is
a *narrowing* section that latches it, because the shrinking `hiS` re-clamps
the ship every frame and so holds `forceCurrentWall` true — which is precisely
why the reporter saw it only "where the track width is not constant". The
instrumented trace shows the deadlock forming: stuck on sample 150, `s` pinned
to a shrinking `hiS`, and the along-track offset decaying to `2.13e-14` —
exactly zero — and staying there.

The non-forced branch has the same tangential-discard but never deadlocks,
because it re-samples at `newPos` and so advances the station regardless.

**Fix:** in the `forceCurrentWall` case only, add back this step's along-track
motion after rebuilding the position:

```
along = (newPos - c.pos) · c.tangent
surface.pos += c.tangent * along
```

Velocity now reaches the result, so the ship slides along the wall and
`sampleTrack` advances the station next frame. The term is self-limiting:
`sampleTrack` re-projects onto the centerline each frame, so `along` never
accumulates beyond one frame of travel. Applied to `cpp/core/src/Ship.cpp` (and, at the time, its
since-retired JS reference oracle).

**This also fixes the section-4 "unsteered car grinds to a halt"** logged above
as a separate issue — same root cause. That fixture went from freezing after
151m to driving 2082m (multiple continuous laps) in 30s.

**Verification:** on the reporter's own track with a game-equivalent collision
surface, the reverse run now covers **496.6m of the 495m ideal** (slightly over,
since it slides along a wall not quite parallel to the tangent), station
advancing 148 → 146 → … → 100, no pinning. Two new `track_tests.cpp`
regressions: a 36 → 157 → 36 bulge reversed out of (asserts never frozen while
indicating speed, >450m of ~495m travelled) and the unsteered-curve case
(>1500m in 30s, speed never below 20 m/s). All six assertions across 4c+4d fail
without the fixes and pass with them.

**Parity impact:** `npm run gen-traces` rerun; 3 of ~26 traces moved in total
across 4c+4d (`boost-circuit`, `raw-session-step-lap`,
`raw-curved-banked-native-bake`). All gates pass. Baked-world worst ratio
unchanged at `7.322e-5`; raw-track worst moved `0.01051` → `0.01129` against a
`0.1` gate (same `1.42e-14` absolute delta) — CLAUDE.md updated. The browser
smoke suite's mesh-free lap now reaches 140 m/s / 504 km/h where it previously
read 138 / 493, a visible consequence of no longer bleeding speed against walls.

## 4e. Bugfix: the carved hole is low-resolution and inaccurate

**Report:** "the hole created in the middle of the central reservation is low
resolution and not accurate."

**Root cause:** M1's over-tessellation guard, quoted verbatim in §4's root
cause above as "~9 forced anchor points across a reservation's span". It was
wrong in two independent ways:

1. **Fixed count.** `kSubdivisions = 8` gave nine anchors across the span
   *regardless of how long the span was*. The gap boundary is a Catmull-Rom
   taper, so its shape needs rings where the *taper* varies, not a constant
   number of them. On `NewTrack.json`'s own `res1` (t 0.10→0.13, width 24 —
   240 m of arc) that worked out to 18 m between rings.
2. **Snapping.** Each anchor's exact `t` was immediately thrown away —
   `affected[lround(t * n)] = true` marked the nearest *raw physics sample*
   instead. So no ring ever landed on `t0`/`t1`, and the taper could not close
   on a point: the void began at whatever half-gap the nearest interior sample
   happened to carry, and the hole came out shorter than authored.

Both compound through the surface strip, which emits sub-quads spanning ring
`i` to ring `i+1` at fixed cross-section `v` and skipped one only where it was
inside the gap at *both* rings. The void's edge was therefore a staircase whose
tread is exactly the per-segment half-gap change — i.e. how far solid road
juts into the hole. Coarse rings mean a coarse staircase. (§4f removes the
staircase itself; this section only made its steps small.)

**Fix:** `adaptiveRenderBake` now resolves each span on its own terms, into a
`forced` list of path parameters that the walker emits *in addition to*
whatever the existing chord-tolerance `breaks()` asks for (so centerline
curvature is still handled by the code that already handled it). Subdivision
recurses until consecutive rings differ by at most `kGapStep` = 0.25 m in
half-gap, the lane-boundary curve is within 0.1 m of its chord, and the chord
is at most 40 m — the latter two matching `breaks()`'s own tolerances, which is
what gives the gap boundary the same fidelity standard the road's outer edges
get. `kGapStep` covers what the deviation test structurally cannot: near the
tips the boundary is nearly straight, so its chord deviation is ~0 while the
gap width still ramps fast. With `kGapStep` disabled, `res1`'s first rendered
tip is a 2.6 m-wide blunt end 20 m from the true tip; at 0.25 m it is 0.38 m.
Samples are taken at exact `t`, and `t0`/`t1` are always emitted, so the lens
closes on a true point. `flushForced` merges them into the walk in ascending
parameter order and drops any that coincide with a ring already being emitted
(a zero-length segment would give degenerate strip quads and a rail of
undefined normal). Reservations no longer set `affected[]` at all.

Because §4 already made the wall and the hole share these exact frames, the
physics wall, the render hole and any exported collision hole all gain the
resolution together and stay aligned by construction.

**Measured** on `cpp/tungsten-monoxide/resources/New_Track.json` (`res1`):

| | before | after |
| --- | --- | --- |
| rings across the span | 24 | 123 |
| hole length | 234.1 m (authored span 240.1 m) | 240.1 m |
| max ring spacing | 18.12 m | 7.43 m |
| max staircase tread | 2.387 m | 0.250 m |
| half-gap at the lens tips | 0.434 m / 0.177 m | 0.000 m / 0.000 m |

**Cost:** rings scale with total taper variation (`~2 × width / kGapStep`), so
the synthetic region's rail count rose 46 → 244 here. `slideAlongRails` is only
reached after an AABB reject, and a couple of hundred segment tests per frame
is in line with a real placed mesh region.

**Tests:** see §4f, which shares a `track_tests` block with this fix.

## 4f. Bugfix: the hole's edges are jagged (the staircase itself)

**Report:** "the edges are still jagged: they should match the track sides'
smoothness." §4e was an incomplete fix — it made the staircase's steps small
(2.387 m → 0.25 m) without removing the staircase.

**Root cause:** the strip's skip rule, described in M1 and quoted in §4e.
Sub-quads span ring `i` to ring `i+1` at *fixed* cross-section `v`, and one was
dropped only when it fell inside the gap at **both** rings. Where the gap
widens from `i` to `j`, the sub-quad between the two rings' gap edges is solid
at `i` and void at `j` — under "both" it was emitted whole, so its corner at
`(j, gapV[i].first)` sat inside ring `j`'s gap. That corner is the step. The
road's own outer edges have no such problem because they *are* the per-ring
`e.left`/`e.right` polyline: one vertex per ring, no quantization. Making the
steps smaller could only ever approach that, never reach it.

**Fix:** classify the sub-quad's four corners individually instead of the quad
as a whole. A corner counts as void only when *strictly* inside its own ring's
band — one exactly on a gap edge is a boundary vertex the solid triangle must
keep. Because every gap edge is itself a breakpoint in the merged `v` list, no
sub-quad ever straddles one, which makes "exactly one strictly-void corner"
precisely the case where the boundary cuts the quad diagonally; the three
surviving corners are the solid triangle. Corners are listed as a
positively-oriented cycle `(i,a) (i,z) (j,z) (j,a)` — the same winding the
existing two-triangle split produces — so dropping one leaves the other three
already correctly wound, and both diagonals fall out of the same rule (the
widening and narrowing sides need opposite ones). Two or fewer solid corners
means no solid area, and is skipped.

The result is that the void's edge is the polyline through each ring's gap
boundary: exactly one vertex per ring, exactly like the road's outer edges, and
exactly the points `reservationGeometry` already builds the wall and rails
from — so hole, wall and rails now coincide vertex-for-vertex rather than
being within a tread of each other.

**No parity impact, by construction:** with no reservation active a ring's band
is the degenerate `{0.5, 0.5}`, the strict-interior test is unsatisfiable, all
four corners are solid, and emission is byte-identical to before. Confirmed by
all four parity gates.

This also fixes the tips independently of ring density: at a tip ring the band
is degenerate, so that ring is wholly solid and the void opens as a true point.

**Tests:** the §4e `track_tests` block was rebuilt on a straight, flat,
default-cross-section path, which makes every one of a ring's surface vertices
exactly collinear with that ring's wall boundary pair (and puts nothing else on
that line). It asserts the tips close to a point, the peak reaches the authored
width, ring count follows the taper — and that **no surface vertex lies
strictly between a ring's two boundary points**, which is the exact structural
property the corner-wise rule guarantees and the "both rings" rule violates.
Under the old rule that check reports a 0.247 m intrusion, matching `kGapStep`
as predicted. All 7 ctest targets pass; no trace moved (no fixture in the
parity corpus is schema 11, and reservations were never part of the retired JS reference oracle
by design).

## 5. Notes for implementers

- Pre-existing uncommitted work in this working tree (`TrackResourceDocument`/
  `TrackResourceSave`, `docs/adr/0002-...`, editor main.cpp changes, etc.) is
  **not part of this feature** — commits for this plan stage only the files
  each milestone actually touches, never a blanket `git add -A`.
- `cpp/core/src/TrackBake.cpp`'s `scalar()` (Catmull-Rom, closed/open aware)
  is the existing helper to reuse for the width taper — construct a 3-point
  synthetic list `{t0: 0, mid: width, t1: 0}` and evaluate through it, same
  pattern already used for roll/width/cross-section.
