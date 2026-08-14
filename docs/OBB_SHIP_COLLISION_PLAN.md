# OBB ship collision — implementation plan

## Goal

Replace the point/segment probes mesh-mode wall collision uses today
(`sweepWall`, `nearestAlongAxis` in `Ship::stepMeshPhysics`) with a proper
oriented bounding box (OBB) representing the ship's hull, tested against the
existing triangle-mesh BVH (`TrackCollisionSurface`). This targets the class
of bug a point probe structurally can't catch: corner/glancing hits where
part of the hull clips through geometry the single probe point/segment
missed, and rotation-dependent tunneling where the ship's yaw carries a
corner through a wall the centerline path avoids.

Ground contact is explicitly **out of scope** — it stays a vertical/surface-
normal point probe (`nearestAlongAxis`). The ship's underbody-to-road contact
is a different problem (ride height, banking, landing) with its own working
model; only wall/obstacle collision is point-probe-shaped in a way that
actually causes clipping bugs.

## Current state (for orientation — see `cpp/core/CLAUDE.md` for the physics
core overview)

- `Ship::Physics` (`Ship.hpp:17-51`) has no width/length/height — collision
  treats the ship as the single point `groundPos`. `up`/`forward`/`right`
  (`Ship.hpp:44-46`) are already a live, orthonormal per-frame basis (used
  for rendering/orientation), which is exactly what an OBB's axes need — no
  new orientation state has to be derived.
- `StartGrid::SHIP_HALF_WIDTH = 1.2` (`StartGrid.hpp:20`) exists today only
  to space grid start positions; it's a reasonable starting value for the
  OBB's half-width but isn't wired to collision.
- Wall collision in mesh-physics mode (`Ship::stepMeshPhysics`,
  `Ship.cpp:59-307`): a single horizontal segment is swept from
  `groundPos` (offset back by `COLLISION_WALL_MARGIN` along the direction of
  travel, lifted by `MESH_WALL_PROBE_HEIGHT = 0.5`, `Ship.cpp:19,150-164`) to
  the intended next position, via `TrackCollisionSurface::sweepWall()`
  (`Ship.cpp:166-195`). Penetration is corrected by pushing the point out
  along the hit normal by `MESH_WALL_CLEARANCE` (`Ship.cpp:27,195`).
- `TrackCollisionSurface` (`TrackCollision.hpp`) is a median-split BVH over
  `CollisionTriangle`s (`TrackCollision.hpp:12-16`), with per-node AABBs
  already stored (`Bounds`/`Node`, `TrackCollision.hpp:60-67`). All four
  public queries (`nearestAlongAxis`, `nearestAcrossAxis`, `sweep`,
  `sweepWall`) are point/segment-vs-triangle (Möller–Trumbore). There is no
  shape-vs-triangle test anywhere in the codebase, and no OBB type anywhere
  in `cpp/core` or `cpp/willpower` (the latter's `BoundingBox`/
  `BoundingCircle` are 2D and axis-aligned, and aren't linked into
  `cpp/core`'s CMake target — not worth pulling in for this).
- Analytic corridor mode's wall handling (`Simulation.cpp:49-81,188-189`,
  clamping a lateral offset `s` to `[sLeft, sRight] ± COLLISION_WALL_MARGIN`)
  is untouched by this plan — it's a different, still-valid abstraction for
  closed-course spline tracks, and isn't in the mesh-mode code path this plan
  targets.

## Decisions locked in

- **Ground contact stays point-probe.** Only mesh-mode wall/obstacle
  collision moves to OBB.
- **Discrete OBB + substepping, not continuous (swept) OBB.** Each physics
  substep tests the OBB at a fixed pose (no shape held in a continuous
  intermediate state), with substep count chosen so max per-substep travel
  stays under roughly half the OBB's smallest half-extent. This bounds
  tunneling without the complexity of a true swept-shape (Minkowski-sum /
  conservative-advancement) algorithm. Revisit only if tunneling is actually
  observed at top speed (140 m/s) after this ships.
- **Feature-flagged rollout.** OBB wall collision lands behind a toggle
  alongside the existing point-probe path in mesh-physics mode, gets
  validated against the disabled `raw/*-rail` fixtures (`raw-corner-rail`,
  `raw-glancing-rail`, `raw-head-on-rail`, `raw-airborne-clears-rail`,
  `raw-airborne-outside-below-rail` — see `cpp/test-data/traces/raw/`,
  currently disabled per `cpp/core/CLAUDE.md` pending Milestone 7 mesh-mode
  traces) before becoming default. Re-baking the 4 active parity traces
  (`boost-circuit`, `open-curve`, `recovery-run`, `starter-circle`) is
  deferred to the point where OBB collision actually becomes the default —
  don't touch them earlier.

Work one step at a time. After each step: build, run `ctest --test-dir
cpp/build -C Release --output-on-failure`, and commit before moving to the
next step.

---

## Milestone 1 — OBB math primitive (inert, no gameplay wiring)

**1.1 — `Obb` type + SAT overlap/MTV against a triangle**
- New files: `cpp/core/include/Obb.hpp`, `cpp/core/src/Obb.cpp`.
- `struct Obb { Vec3 center; Vec3 axes[3]; Vec3 halfExtents; };` — axes are
  assumed orthonormal (caller's responsibility; `Ship::Physics::right/up/
  -forward` already are).
- `bool overlapsTriangle(const Obb&, const CollisionTriangle&, Vec3* outNormal, double* outDepth)`
  — full 13-axis SAT (3 box face normals, 1 triangle normal, 9 edge
  cross-products), returning the minimum-translation-vector normal/depth on
  overlap. This is the one genuinely new piece of geometry math in the
  codebase; keep it self-contained (`glm::dvec3`-based, matching `Vec3.hpp`)
  rather than reaching for `Willpower.Geometry`, per `cpp/core`'s existing
  "no `Willpower.Geometry` dependency" posture (root `CLAUDE.md`).
- `bool overlapsAabb(const Obb&, const TrackCollisionSurface::Bounds&)` —
  cheaper OBB-vs-AABB SAT (box axes + 3 world axes only, no triangle-normal/
  edge-cross terms), used for Milestone 2's BVH node pruning.
- Test: new unit tests in `cpp/core/tests/` (or extend `track_tests.cpp`)
  covering axis-aligned overlap/no-overlap, corner-only contact depth,
  edge-edge contact (the case face-normal-only SAT gets wrong), and a couple
  of hand-computed depth/normal values. Commit.

## Milestone 2 — BVH OBB query

**2.1 — `TrackCollisionSurface::queryObb`**
- File: `cpp/core/include/TrackCollision.hpp` / `src/TrackCollision.cpp`.
- Add `std::vector<CollisionHit> queryObb(const Obb&) const` (or an
  out-vector to avoid per-call allocation on the hot path — decide based on
  what `stepMeshPhysics` needs in 3.x): descend the BVH pruning by
  `Obb::overlapsAabb` against each `Node::bounds`, run
  `Obb::overlapsTriangle` against leaf triangles, collect all penetrating
  contacts (normal + depth + `surfaceId`/triangle index, reusing the
  existing `CollisionHit` shape or a small sibling struct if `t`/`position`
  don't apply cleanly). Mirror the existing `querySegment`/
  `queryNearestSegment` traversal shape (`TrackCollision.cpp`) rather than
  inventing a new traversal pattern.
- Test: extend the Milestone 1 unit tests to go through the BVH (a small
  hand-built `TrackCollisionSurface` of a few triangles forming a corner/
  wall, OBB placed to overlap 0/1/2 of them). Commit.

## Milestone 3 — Ship hull dimensions

**3.1 — Give `Ship::Physics` real hull half-extents**
- File: `Ship.hpp:17-51`.
- Add `double hullHalfLength`, `hullHalfWidth`, `hullHalfHeight` (defaults:
  reuse `StartGrid::SHIP_HALF_WIDTH = 1.2` for width; pick length/height
  defaults by eyeballing the ship's rendered mesh dimensions in
  `tungsten-monoxide`/the editor — flag this as a value to sanity-check with
  whoever owns ship art, not a physics decision). These are new fields on a
  struct that's part of the golden-trace-serialized state — confirm they're
  additive/gate-inert the way `Race`'s session-time fields are
  (`Ship.hpp:72-78`'s comment) before touching trace serialization code.
- Test: `ctest` (should be a no-op change to existing behavior — the fields
  are unused so far). Commit.

**3.2 — `Obb` construction from `Ship::Physics`**
- File: `Ship.cpp`, a small free function or `Physics` method, e.g.
  `Obb hullObb(const Physics&)`, built from `groundPos` (recentered to the
  hull's actual center — `groundPos` is a ground-contact point, likely
  offset vertically by ride height / `hullHalfHeight`, not the hull center;
  get this right or contacts will be systematically mispositioned) and the
  `right`/`up`/`forward` basis + the new half-extents.
- Test: unit test that the constructed OBB's corners land where expected for
  a known pose. Commit.

## Milestone 4 — Wire into mesh-physics wall collision, feature-flagged

**4.1 — Add the toggle**
- File: likely `Simulation.hpp`/`.cpp` alongside `meshPhysicsEnabled_`
  (`Ship.cpp:311-316` reads it) — add a sibling flag, e.g.
  `obbWallCollisionEnabled_`, same live-toggleable-from-debug-overlay pattern
  `MESH_PHYSICS_PLAN.md` used for `meshPhysicsEnabled_`. Default off.
- Test: `ctest` (no behavior change with the flag off). Commit.

**4.2 — OBB wall resolution path**
- File: `Ship.cpp`, alongside the existing `sweepWall`/penetration-correction
  block (`Ship.cpp:150-195`).
- When the flag is on: build the hull OBB at the ship's *intended* next pose
  (after integrating velocity for this substep, before commit), query
  `TrackCollisionSurface::queryObb`, and for each penetrating contact push
  the ship out along the MTV normal (accumulate/resolve iteratively over the
  contact set — 2-4 solver passes, same spirit as simple discrete-collision
  resolution elsewhere in the industry, not a new architecture) and reflect
  the velocity component along that normal using the existing
  `wallRestitution` (`Ship.hpp:27`) the same way the point-probe path already
  does (`Ship.cpp:~394-424`'s reflection logic is the model to reuse, not
  reinvent).
- Substep the frame's motion (per "Decisions locked in" above) so the OBB
  doesn't jump past thin geometry within one `dt`.
- When the flag is off: existing `sweepWall` point-probe path, unchanged.
- Test: `ctest` (flag still off by default, so this is dead code path
  coverage only at this point — add a direct unit/scenario test that flips
  the flag on and drives a ship into a corner/wall in a small synthetic
  track, asserting it stops/deflects instead of tunneling). Commit.

## Milestone 5 — Validation against rail fixtures

**5.1 — Re-enable and compare against `raw/*-rail` fixtures**
- Files: `cpp/core/CMakeLists.txt` (currently-commented `add_test` calls for
  `raw_parity` etc.), `cpp/test-data/traces/raw/*-rail*.json`.
- These fixtures are disabled today because they predate Milestone 7 of
  `MESH_PHYSICS_PLAN.md` (mesh-mode-appropriate replacement traces), not
  because of this work — re-enabling them is itself a prerequisite this plan
  depends on, not something to redo from scratch. If Milestone 7 hasn't
  landed by the time this milestone starts, coordinate rather than
  duplicating that effort.
- With the OBB flag on, replay `raw-corner-rail`, `raw-glancing-rail`,
  `raw-head-on-rail`, `raw-airborne-clears-rail`,
  `raw-airborne-outside-below-rail` and confirm qualitatively-correct
  behavior (no tunneling, plausible deflection) even if exact trajectories
  won't match the old point-probe-recorded traces bit-for-bit — these will
  likely need re-recording against the new path rather than reused verbatim.
- Commit whatever fixture/test-wiring changes result.

## Milestone 6 — Flip default, re-bake golden traces

**6.1 — Make OBB wall collision the default in mesh-physics mode**
- File: wherever `obbWallCollisionEnabled_` defaults are set (mirrors
  `MESH_PHYSICS_PLAN.md`'s own note that `meshPhysicsEnabled_` flipped
  default from analytic to mesh once validated).
- The 4 active parity traces (`boost-circuit`, `open-curve`, `recovery-run`,
  `starter-circle`) pin their `Simulation` to analytic mode explicitly per
  `cpp/core/CLAUDE.md`, so they should be unaffected by this default flip —
  verify that's still true rather than assuming it.
- If any mesh-mode-default trace/fixture exists by this point (from
  Milestone 5's work or elsewhere), re-bake it now — this is the append-only
  golden-fixture corpus (`cpp/test-data/`), so treat re-baking as a
  deliberate, reviewed step per root `CLAUDE.md`, not an incidental diff.
- Test: full `ctest` suite green. Commit.

---

## Status

- **Milestones 1-4: landed.** `Obb.hpp`/`.cpp` (13-axis SAT + conservative
  OBB/AABB), `TrackCollisionSurface::queryObb`, `Physics::hullHalf*` +
  `hullObb`, and the flagged wall-resolution path in `stepMeshPhysics`, each
  with unit/scenario coverage in `cpp/core/tests/track_tests.cpp`.
  `Simulation::obbWallCollisionEnabled_` defaults **off**, with a debug-overlay
  checkbox beside "Mesh Physics" and a `mesh_physics_diag --obb-walls` switch.
- Two implementation notes worth carrying forward:
  - `ObbContact` reports *both* the true MTV and a push along the contacted
    triangle's own plane, and the wall resolver uses the latter. A hull
    overlapping a wall triangle near an internal seam has a genuinely shorter
    way out sideways across that seam, so the MTV there points along an
    edge-cross axis into the neighbouring triangle (the classic internal-edge
    artifact). A test pins the artifact so the reason for the second field
    can't quietly evaporate.
  - Hull dimensions were resolved from the ship art, not guessed:
    `box.mppmodel` is a unit cube scaled by `(2.4, 0.8, 4.0)` in
    right/up/forward (`StatePlayTungstenMonoxide::applyShipTransform`), so
    half-extents are `(1.2, 0.4, 2.0)` — and the width half independently
    matches the long-standing `StartGrid::SHIP_HALF_WIDTH`.
  - The `groundPos`-to-hull-centre offset (open question 2) is the ship's real
    hovering position, not a hull resting on the road: `Physics::hullHoverHeight`
    (1 m) plus the live bob, via `hullHoverOffset`. Core now owns hover and bob
    — `Ship::step` advances `Physics::bobTime` while grounded and
    `landOnSurface` resets its phase — because collision is built on them and a
    renderer-side copy can't drive physics; the renderer reads core's value
    instead of integrating its own, so the drawn ship and the collided hull
    can't drift apart. The renderer's landing-bounce spring stays visual-only:
    `Physics::landingBounce` is an accumulator nothing decays, so feeding it to
    the hull would ratchet it upward over a run.
- **Milestone 5: blocked, substitute validation done.** The `raw/*-rail`
  fixtures can't be replayed at all — every one references `meshAssets`/`meshes`
  and hard-fails to load under schema 12 (verified: `parity` on
  `raw-corner-rail` errors out before stepping). That is the pre-existing
  blocker this plan anticipated; `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 7
  owns replacing them. Validation instead used the tunnel/ramp validation asset
  (`cpp/test-data/fixtures/mesh-object/tunnel-validation/`), the one mesh-mode
  fixture that still loads and has walls: 900 frames with the flag on gave the
  same four gameplay events and same 22 airborne frames as the point probe, no
  respawn, no tunneling, no NaN, byte-identical reruns, and the ship holding
  station about a hull half-width further from the tunnel wall than the
  centreline probe did.
- **Milestone 6: deliberately not done.** "Decisions locked in" gates the
  default flip on rail-fixture validation, which is blocked above. The flag
  stays off until that coverage exists.
- **Not covered by Milestone 4.2's scope:** the airborne branch's mid-flight
  wall contact (`Ship.cpp`'s `sweepWall` inside the `p.airborne` block) is still
  the point sweep. 4.2 scopes itself to the grounded wall block, and converting
  the airborne branch means interleaving the OBB test with its landing sweep —
  a follow-up, and one the rail fixtures above (`raw-airborne-*-rail`) are the
  natural coverage for.

## Open questions to resolve before/while executing

- **Hull dimensions** (3.1): needs a real number from ship art/design, not a
  guessed default carried forward silently.
- **`groundPos`-to-hull-center offset** (3.2): get this wrong and every
  contact will be subtly mispositioned in a way that's easy to miss in a
  quick playtest but shows up as "corners feel too grippy/too loose."
- **Contact solver iteration count** (4.2): starting guess is 2-4 passes;
  tune against the rail fixtures in Milestone 5 rather than picking a number
  up front.
- **Milestone 7 (`MESH_PHYSICS_PLAN.md`) sequencing**: if that work is
  in flight, Milestone 5 here should build on it rather than duplicate the
  fixture re-recording work.
