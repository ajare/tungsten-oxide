# Central Reservation Plan — median strip on a track path

Status: **M0-M4 complete, M5 partial (core tests done, editor tests
deferred). Post-M5: fixed two bugs reported as "ship physics breaks near
the central reservation" — a render/physics wall-alignment mismatch, and
(the actual reported cause) a reverse-gear wall collision bug — see §4.**
This document is updated after each milestone lands.
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
- [x] **M3 — Native editor UI (panel only).** `EditorState` mutation helpers
  (add/remove/update with clamp + non-overlap validation — undo/redo is free
  via existing whole-document snapshots); new `ReservationsPanel` (list +
  t0/t1/width fields, mirrors `TriggersPanel`); wired into `main.cpp`.
  - `TopDownCanvas` on-canvas preview of the gap/wall footprint is **not yet
    done** — deferred, since it's cosmetic (the panel/table already show
    every reservation's exact t0/t1/width) and every other file this
    milestone touches (`EditorState.hpp`, `main.cpp`, `CMakeLists.txt`) had
    substantial pre-existing uncommitted work in it, making an additional
    non-essential change riskier to land cleanly.
  - `clampReservation`'s non-overlap logic clamps into the free `[lo,hi]` gap
    around the edited entry's own midpoint (bounded by whichever other
    reservations are nearest on each side) — guarantees non-overlap without
    ever touching another entry.
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
Applied to both `cpp/core/src/Ship.cpp` and its JS reference oracle
`web/js/track-physics.js`; the mesh-platform `slideAlongRails` response in the
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
`raw_session_step_parity` failed with C++ firing the lap one frame before JS
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
accumulates beyond one frame of travel. Applied to `cpp/core/src/Ship.cpp` and
its JS oracle `web/js/track-physics.js`.

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

## 5. Notes for implementers

- Pre-existing uncommitted work in this working tree (`TrackResourceDocument`/
  `TrackResourceSave`, `docs/adr/0002-...`, editor main.cpp changes, etc.) is
  **not part of this feature** — commits for this plan stage only the files
  each milestone actually touches, never a blanket `git add -A`.
- `cpp/core/src/TrackBake.cpp`'s `scalar()` (Catmull-Rom, closed/open aware)
  is the existing helper to reuse for the width taper — construct a 3-point
  synthetic list `{t0: 0, mid: width, t1: 0}` and evaluate through it, same
  pattern already used for roll/width/cross-section.
