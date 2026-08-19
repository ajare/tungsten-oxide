#include "Ship.hpp"

#include "Obb.hpp"
#include "Simulation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace tox {
namespace {

// A flat road keeps a ship's moveDir tangent to (0,1,0) exactly, so vel.y is exactly 0 there every
// frame; a genuinely sloped surface (a ramp) produces a real, nonzero vel.y proportional to its
// grade. This threshold only needs to clear ordinary floating-point noise on an ostensibly-flat
// surface, not any real ramp grade -- it is far below the vertical launch speed even a shallow
// (~4 degree) ramp imparts at typical driving speed. See its one call site's comment for why this
// distinguishes a ramp-crest launch from an ordinary flat-road-edge scrape.
constexpr double MESH_RAMP_LAUNCH_VERTICAL_SPEED = 0.05;
// --- OBB wall collision (docs/OBB_SHIP_COLLISION_PLAN.md) ------------------------------------
// Above this |dot(planeNormal, UP)| a contacted surface counts as road/floor rather than wall, and
// the wall resolver leaves it to the ground probe. Resolving the hull "out of" the road it is
// driving on would shove it sideways off the track; a barrier sits near 0 and a road near 1, so the
// cutoff is nowhere near either.
constexpr double MESH_OBB_WALL_MAX_UP_DOT = 0.5;
// How many push-out/reflect passes one substep gets. A single pass resolves a flat wall; a corner
// needs one per surface, and the second push can reintroduce a shallow overlap with the first. Four
// is the plan's starting guess and is generous for the 2-3 distinct planes a hull can realistically
// touch at once -- passes stop early the moment nothing is penetrating.
constexpr int MESH_OBB_SOLVER_PASSES = 4;
// Discrete overlap tests see only the poses they are actually evaluated at, so the hull's motion is
// diced until each piece is short enough that it cannot straddle a wall without some pose reporting
// an overlap. Half the smallest half-extent is the plan's bound; at the 140 m/s top speed and a
// 1/120 s physics step that works out to 6 pieces, so the cap only ever binds if a ship somehow
// exceeds its own top speed by a wide margin.
constexpr double MESH_OBB_SUBSTEP_HALF_EXTENT_FRACTION = 0.5;
constexpr int MESH_OBB_MAX_SUBSTEPS = 8;
// Standoff left between hull and wall after a push-out, purely so the next frame's test doesn't
// re-report the same contact through floating-point noise. Deliberately small: the hull's centre is
// already a half-width clear of any wall its flank rests against, so there is nothing here for a
// full TrackCore::COLLISION_WALL_MARGIN-sized standoff to buy.
constexpr double MESH_OBB_WALL_SKIN = 0.01;

// Resolves whatever walls the ship's hull currently overlaps at `position`, pushing the hull back
// out and reflecting the into-wall part of `velocity`. Both are updated in place. Returns true if
// any impulse was applied -- i.e. the ship actually hit something rather than merely resting
// against it. `contacts` is the caller's reusable query buffer.
bool resolveObbWallContacts(Physics& p, const TrackCollisionSurface& bvh, const Vec3& up,
                            Vec3& position, Vec3& velocity, std::vector<ObbContact>& contacts) {
  bool struck = false;
  for (int pass = 0; pass < MESH_OBB_SOLVER_PASSES; ++pass) {
    bvh.queryObb(hullObb(p, position, up), contacts);
    // Resolve the deepest wall first and re-test: pushing out of it usually clears the shallower
    // contacts outright (adjacent triangles of the same wall), and where it doesn't -- a real
    // corner -- the next pass sees the remaining overlap at its corrected pose rather than
    // stacking two pushes computed against a pose that no longer exists.
    const ObbContact* deepest = nullptr;
    for (const ObbContact& contact : contacts) {
      if (std::fabs(glm::dot(contact.planeNormal, UP)) > MESH_OBB_WALL_MAX_UP_DOT) continue;
      if (!deepest || contact.planeDepth > deepest->planeDepth) deepest = &contact;
    }
    if (!deepest) break;

    position += deepest->planeNormal * (deepest->planeDepth + MESH_OBB_WALL_SKIN);
    const double into = glm::dot(velocity, deepest->planeNormal);
    // Only velocity actually heading into the surface earns an impulse -- a hull resting against
    // a wall while steering away is still overlapping it every frame, and reflecting there would
    // fire an impact jolt per frame for as long as it scraped along.
    if (into < 0.0) {
      velocity += deepest->planeNormal * (-into * (1 + weightRestitution(p)));
      addImpactJolt(p, -into);
      struck = true;
    }
  }
  return struck;
}

// How many pieces this step's motion has to be diced into for a discrete overlap test to be sound:
// see MESH_OBB_SUBSTEP_HALF_EXTENT_FRACTION.
int obbSubstepCount(const Physics& p, double travel) {
  const double smallestHalfExtent = std::min({p.hullHalfLength, p.hullHalfWidth, p.hullHalfHeight});
  const double maxAdvance = std::max(MESH_OBB_SUBSTEP_HALF_EXTENT_FRACTION * smallestHalfExtent, 1e-6);
  return std::clamp(static_cast<int>(std::ceil(travel / maxAdvance)), 1, MESH_OBB_MAX_SUBSTEPS);
}

// Mesh-mode wall collision with the ship's whole hull. Advances this step's horizontal motion in
// short pieces, resolving contacts after each and carrying the corrected velocity into the next.
// Returns the resolved ground-plane position and leaves p.speed/p.moveDir describing the
// post-bounce travel.
//
// This replaced a centreline point sweep, which structurally could not catch two things: a corner or
// flank clipping geometry the centreline passes beside, and a yaw carrying a corner through a wall
// the centreline path avoids.
Vec3 resolveObbWalls(Ship& ship, const TrackCollisionSurface& bvh, const Vec3& initialVelocity,
                     double dt, const Vec3& up) {
  Physics& p = ship.physics;
  Vec3 velocity = initialVelocity;
  Vec3 position = p.groundPos;

  const int substeps = obbSubstepCount(p, std::hypot(velocity.x, velocity.z) * dt);
  const double substepDt = dt / substeps;

  std::vector<ObbContact> contacts;
  bool struck = false;
  for (int substep = 0; substep < substeps; ++substep) {
    position += Vec3(velocity.x, 0.0, velocity.z) * substepDt;
    if (resolveObbWallContacts(p, bvh, up, position, velocity, contacts)) struck = true;
  }

  if (struck) {
    // Gear-preserving, exactly as the point-probe path and every other wall bounce in this file: a
    // plain length/normalize decomposition is always non-negative and would force a reversing car
    // into forward gear on contact.
    const double gear = p.speed < 0.0 ? -1.0 : 1.0;
    const double magnitude = glm::length(velocity);
    p.speed = gear * magnitude * weightSpeedRetain(p);
    if (magnitude > 1e-6) p.moveDir = normalizeSafe(velocity * gear);
  }
  return position;
}

struct ObbFlightStep {
  Vec3 position;
  // Set when the arc met a road surface: the ship has landed, and `position` is that contact point.
  std::optional<CollisionHit> landing;
};

// The airborne counterpart of resolveObbWalls: flies the hull along this step's ballistic arc in
// the same short pieces, hitting walls with the whole box, and carrying the corrected velocity
// (vertical included) into the next piece. A mid-air wall bounce leaves the ship airborne rather
// than "landing" sideways on a barrier's horizontal normal, which would be an obviously wrong
// grounded state.
//
// Landing itself stays a point sweep along each piece of the arc: ground contact is deliberately
// out of the OBB path's scope (docs/OBB_SHIP_COLLISION_PLAN.md), and it ends the step wherever it
// happens, so the pieces after it are the next step's problem.
ObbFlightStep flyWithObbWalls(Ship& ship, const TrackCollisionSurface& bvh, const Vec3& horizontalVel,
                              double dt, const Vec3& up) {
  Physics& p = ship.physics;
  Vec3 velocity(horizontalVel.x, p.verticalVel, horizontalVel.z);
  Vec3 position = p.groundPos;

  const int substeps = obbSubstepCount(p, glm::length(velocity) * dt);
  const double substepDt = dt / substeps;

  std::vector<ObbContact> contacts;
  ObbFlightStep result{position, std::nullopt};
  bool struck = false;
  for (int substep = 0; substep < substeps; ++substep) {
    const Vec3 target = position + velocity * substepDt;
    if (auto landing = bvh.sweep(position, target)) {
      result.position = landing->position;
      result.landing = std::move(landing);
      break;
    }
    position = target;
    if (resolveObbWallContacts(p, bvh, up, position, velocity, contacts)) struck = true;
    result.position = position;
  }

  if (struck) {
    // Same decomposition the point-probe path's mid-air bounce uses: vertical back to verticalVel,
    // the horizontal remainder back to speed/moveDir as a flat direction.
    p.verticalVel = velocity.y;
    const double horizontalMagnitude = std::hypot(velocity.x, velocity.z);
    p.speed = horizontalMagnitude;
    if (horizontalMagnitude > 1e-6)
      p.moveDir = Vec3(velocity.x / horizontalMagnitude, 0.0, velocity.z / horizontalMagnitude);
  }
  return result;
}

void integrateSpeed(Physics& p, double dt, double throttle, double brake) {
  if (throttle) {
    p.speed += p.accel * dt;
  } else if (brake) {
    p.speed -= p.brakeDecel * dt;
  } else {
    const double decay = p.friction * dt;
    if (p.speed > 0)
      p.speed = std::max(0.0, p.speed - decay);
    else
      p.speed = std::min(0.0, p.speed + decay);
  }
  p.speed = TrackCore::clamp(p.speed, p.maxReverse, effectiveMaxSpeed(p));
}

// Debug/experimental alternate physics mode (Simulation::meshPhysicsEnabled): ground contact, wall
// collision and airborne/landing all come directly from the baked collision BVH instead of the
// analytic corridor math the rest of this file uses. No corridor sampling for ship positioning --
// see docs/MESH_PHYSICS_PLAN.md. Zone *hosting* still needs a corridor Sample
// (Simulation::detectZoneTriggers' signature), so this still computes one as a cheap read-only
// input to that gameplay-trigger system; its result is never used for ship positioning here, only
// Simulation::detectTriggers (checkpoints/finish) is purely position-based and needs nothing from
// this mode.
StepResult stepMeshPhysics(Ship& ship, const Simulation& simulation, double dt, double throttle,
                           double brake, double steer) {
  Physics& p = ship.physics;
  const bool hasTranslation = (throttle != 0.0) || (brake != 0.0) || std::fabs(p.speed) > 0.001;
  const bool startedAirborne = p.airborne;
  const TrackCollisionSurface& bvh = *simulation.track().collisionSurface;

  integrateSpeed(p, dt, throttle, brake);
  const double speedRatio = std::min(1.0, std::fabs(p.speed) / p.maxSpeed);

  // p.up is frozen at spawn/respawn for the rest of a run (see Ship.hpp) -- fine for the analytic
  // path, which never uses it as a probe axis, but wrong here: on a banked/rolled section, probing
  // straight along a stale spawn-time "up" increasingly misses the actual (tilted) road surface as
  // the ship goes around the bank, so it falsely reads as airborne. ship.renderNormal is the field
  // that exists precisely for "a live, correct-every-frame value to chase instead" (Ship.hpp) --
  // GameSession updates it from the previous frame's resolved StepResult::surfaceNormal, so it's
  // the best available estimate of the ship's actual current orientation for this frame's probes.
  // Defensive: renderNormal is maintained outside this function, so refuse anything that isn't
  // plausibly a drivable surface's "up". A probe axis with no meaningful vertical component can
  // never find the road below the ship, and silently turns every subsequent frame airborne.
  const Vec3 probeAxis =
      glm::dot(ship.renderNormal, UP) > 0.1 ? normalizeSafe(ship.renderNormal) : UP;
  Vec3 surfaceNormal = probeAxis;
  Vec3 surfaceRenderPos = p.groundPos;
  bool railHit = false;  // mesh mode has no rail concept -- walls are ordinary BVH geometry

  const Vec3 steerAxis = p.airborne ? UP : surfaceNormal;
  const double sgn = p.speed > 0 ? 1.0 : (p.speed < 0 ? -1.0 : 1.0);
  const double effectiveTurn = p.turnRate * (1 - 0.35 * speedRatio) * sgn;
  p.forward = applyAxisAngle(p.forward, steerAxis, steer * effectiveTurn * dt);
  tangentize(p.forward, steerAxis, p.forward);

  const double gripThisFrame = p.grip * (0.5 + 0.5 * (1 - std::min(std::fabs(steer) * speedRatio, 1.0)));
  const double toForward = signedAngleAbout(p.moveDir, p.forward, steerAxis);
  p.moveDir = applyAxisAngle(p.moveDir, steerAxis, toForward * std::min(gripThisFrame * dt, 1.0));
  tangentize(p.moveDir, steerAxis, p.forward);

  const Vec3 vel = p.moveDir * p.speed;

  if (p.airborne) {
    // Wall contact in flight is the whole hull, flown along this step's arc (see flyWithObbWalls).
    // Landing is still a one-sided road sweep: ground contact is a point probe by design
    // (docs/OBB_SHIP_COLLISION_PLAN.md), only wall/obstacle collision is shape-based.
    p.verticalVel -= p.gravity * dt;
    const ObbFlightStep flight = flyWithObbWalls(ship, bvh, vel, dt, probeAxis);
    p.groundPos = flight.position;
    surfaceRenderPos = p.groundPos;
    if (flight.landing) {
      const double impactSpeed = std::max(0.0, -p.verticalVel);
      landOnSurface(ship, flight.landing->normal);
      applyLandingImpact(ship, impactSpeed);
      surfaceNormal = flight.landing->normal;
    } else {
      // Deliberately never a wall's normal: this value is what GameSession feeds back into
      // ship.renderNormal, which is this function's ground-probe axis. Reporting a wall's
      // (horizontal) normal here left the ship probing for ground sideways on every subsequent
      // frame, so it never found ground again, fell, and respawned -- once per lap in the headless
      // drive test.
      surfaceNormal = UP;
    }
  } else if (hasTranslation) {
    // Wall collision is the ship's whole hull as an oriented box, resolved against the collision
    // BVH (see resolveObbWalls). Ground contact below is a separate concern and stays a vertical
    // point probe.
    Vec3 intended = resolveObbWalls(ship, bvh, vel, dt, probeAxis);
    auto groundAt = [&](const Vec3& at) {
      return bvh.nearestAlongAxis(Vec3(at.x, p.groundPos.y, at.z), probeAxis, 4.0);
    };
    auto ground = groundAt(intended);
    // vel.y is nonzero only when the ship was climbing/descending a genuinely sloped surface --
    // moveDir only ever picks up a y component via tangentize() against a non-flat surfaceNormal; a
    // flat road keeps vel.y exactly 0 every frame. If the step also leaves the drivable surface
    // entirely while that's true, this is a ramp crest, not a flat road's edge, and the ship should
    // launch into real airborne flight -- skip the edge-scrape recovery below entirely (it
    // unconditionally zeros the outward velocity component to keep the ship pinned at the edge,
    // which is exactly wrong here) and fall through to this block's existing final `else`, which
    // already does exactly the right thing (beginAirborne(ship, vel)) once `ground` stays unset.
    // Verified headlessly with DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.1's ramp/platform
    // validation asset: without this check, the ship's speed collapsed to a dead stop right at the
    // ramp's crest on every run instead of launching over the gap beyond it.
    if (!ground && vel.y <= MESH_RAMP_LAUNCH_VERTICAL_SPEED) {
      // The step would leave the drivable surface. A rail is *supposed* to have stopped this, but
      // rails stand exactly at the road's outer boundary, so the last road triangle and the first
      // rail triangle meet in a razor-thin seam that a fast ship can slip through without the wall
      // probe ever registering a crossing (reproduced headlessly: the ship escaped at one exact
      // spot every lap, with the probe segment passing within rounding distance of the rail).
      // Rather than chase that floating-point edge, treat "walked off the road" as track-edge
      // contact in its own right: bisect back to the last point that still has ground under it,
      // and remove the outward velocity so the ship scrapes along the edge instead of flying off.
      if (auto here = groundAt(p.groundPos)) {
        Vec3 inside = p.groundPos, outside = intended;
        auto insideHit = here;
        for (int i = 0; i < 6; ++i) {
          const Vec3 mid = (inside + outside) * 0.5;
          if (auto midHit = groundAt(mid)) {
            inside = mid;
            insideHit = midHit;
          } else {
            outside = mid;
          }
        }
        ground = insideHit;
        intended = inside;
        // Estimate which way "off the track" actually points by sampling around the contact point:
        // the directions with no ground under them are off-road. The bisection direction is NOT
        // usable for this -- it converges along the direction of travel, so subtracting it would
        // remove essentially all velocity and pin the ship dead against the edge instead of letting
        // it scrape along.
        Vec3 outward(0.0, 0.0, 0.0);
        constexpr int EDGE_SAMPLES = 12;
        constexpr double EDGE_SAMPLE_RADIUS = 1.0;
        for (int i = 0; i < EDGE_SAMPLES; ++i) {
          const double angle = (2.0 * 3.14159265358979323846 * i) / EDGE_SAMPLES;
          const Vec3 dir(std::cos(angle), 0.0, std::sin(angle));
          if (!groundAt(inside + dir * EDGE_SAMPLE_RADIUS)) outward += dir;
        }
        outward = glm::dot(outward, outward) > 1e-12
                      ? normalizeSafe(outward)
                      : normalizeSafe(Vec3(outside.x - inside.x, 0.0, outside.z - inside.z));
        const Vec3 flat(vel.x, 0.0, vel.z);
        const double outSpeed = glm::dot(flat, outward);
        if (outSpeed > 0.0) {
          const Vec3 slid = flat - outward * outSpeed;
          addImpactJolt(p, outSpeed);
          const double gear = p.speed < 0.0 ? -1.0 : 1.0;
          const double mag = glm::length(slid);
          p.speed = gear * mag * weightSpeedRetain(p);
          if (mag > 1e-6) p.moveDir = normalizeSafe(slid * gear);
          // Actually travel along the corrected, edge-parallel velocity this frame. Without this
          // the ship keeps a healthy tangential speed while its position stays pinned at the
          // bisected contact point -- a car reading 27 m/s that never moves, which is precisely
          // the "gets stuck" symptom.
          const Vec3 slidTarget = p.groundPos + Vec3(slid.x, 0.0, slid.z) * dt;
          if (auto slidHit = groundAt(slidTarget)) {
            ground = slidHit;
            intended = slidTarget;
          }
        }
      }
    }
    if (ground) {
      p.groundPos = ground->position;
      surfaceRenderPos = ground->position;
      surfaceNormal = ground->normal;
      tangentize(p.moveDir, surfaceNormal, p.forward);
      tangentize(p.forward, surfaceNormal, p.moveDir);
    } else {
      beginAirborne(ship, vel);
      p.groundPos = Vec3(intended.x, p.groundPos.y, intended.z);
      surfaceRenderPos = p.groundPos;
    }
  } else {
    if (const auto ground = bvh.nearestAlongAxis(p.groundPos, probeAxis, 4.0)) {
      p.groundPos = ground->position;
      surfaceRenderPos = ground->position;
      surfaceNormal = ground->normal;
    } else {
      beginAirborne(ship, Vec3(0, 0, 0));
      surfaceRenderPos = p.groundPos;
      surfaceNormal = probeAxis;
    }
  }

  tickBoost(ship, dt);
  tickBob(ship, dt);
  tickHoverBounce(ship, dt, startedAirborne && !p.airborne);
  tickLean(ship, dt, steer);
  const Sample zoneSample = simulation.sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
  if (!p.airborne) simulation.detectZoneTriggers(ship, zoneSample);

  simulation.detectTriggers(ship, ship.prevTriggerPos, p.groundPos);
  ship.prevTriggerPos = p.groundPos;

  if (p.airborne && p.groundPos.y < simulation.track().trackFloorY) {
    simulation.respawn(ship);
    return {surfaceNormal, surfaceRenderPos, true, railHit};
  }
  return {surfaceNormal, surfaceRenderPos, false, railHit};
}

}  // namespace

double hullHoverOffset(const Physics& physics) {
  const double bob = physics.airborne ? 0.0 : std::sin(physics.bobTime * SHIP_BOB_RATE) * SHIP_BOB_AMPLITUDE;
  return physics.hullHoverHeight + bob + physics.hoverBounce;
}

void tickBob(Ship& ship, double dt) {
  if (!ship.physics.airborne) ship.physics.bobTime += dt;
}

void tickHoverBounce(Ship& ship, double dt, bool landedThisStep) {
  if (landedThisStep) return;
  Physics& p = ship.physics;
  p.hoverBounceVel += -HOVER_BOUNCE_STIFFNESS * p.hoverBounce * dt;
  p.hoverBounceVel *= std::exp(-HOVER_BOUNCE_DAMPING * dt);
  p.hoverBounce += p.hoverBounceVel * dt;
}

void tickLean(Ship& ship, double dt, double steer) {
  Physics& p = ship.physics;
  const double speedRatio = std::min(1.0, std::fabs(p.speed) / p.maxSpeed);
  const double targetBank =
      TrackCore::clamp(-steer * speedRatio * SHIP_BANK_PER_STEER, -SHIP_MAX_BANK, SHIP_MAX_BANK);
  const double response = std::min(1.0, dt * SHIP_LEAN_RESPONSE_RATE);
  p.visualBank += (targetBank - p.visualBank) * response;
  p.visualPitch += (p.speed * SHIP_PITCH_PER_SPEED - p.visualPitch) * response;
}

void applyLandingImpact(Ship& ship, double impactSpeed) {
  Physics& p = ship.physics;
  // The spring is *set*, not added to: an impact starts a fresh bounce from the surface, and the
  // ship cannot be more landed than landed however many contacts a frame resolves.
  p.hoverBounce = 0.0;
  p.hoverBounceVel = std::min(HOVER_BOUNCE_MAX_LANDING_VEL, impactSpeed * HOVER_BOUNCE_LANDING_GAIN);
  // Legacy accumulators, written exactly as they always were -- see Physics (Ship.hpp).
  p.landingBounce += std::min(3.2, impactSpeed * 0.09);
  p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
}

Obb hullObb(const Physics& physics, const Vec3& groundPos, const Vec3& up) {
  Obb hull;
  Vec3 hullUp = normalizeSafe(up);
  if (glm::dot(hullUp, hullUp) < 0.5) hullUp = UP;
  Vec3 right = glm::cross(hullUp, physics.forward);
  // forward parallel to up leaves no lateral direction to derive. Physics never actually produces
  // that (forward is tangentized against the surface every step), but a caller assembling a pose by
  // hand can, and a NaN basis here would silently poison every contact.
  if (glm::dot(right, right) < 1e-12)
    right = glm::cross(hullUp, std::fabs(hullUp.y) < 0.9 ? UP : Vec3(1, 0, 0));
  right = normalizeSafe(right);
  const Vec3 forward = normalizeSafe(glm::cross(right, hullUp));

  // Lean the box the same way the model is leaned when it is drawn: pitch about its own right axis,
  // bank about its own forward axis, composed in the ship's local frame. Built as the identical
  // Euler quaternion the render transform uses, applied to the same (right, up, forward) basis, so
  // hull and model can't come apart on the composition order.
  const glm::dquat lean(Vec3(physics.visualPitch, 0.0, physics.visualBank));
  const auto toWorld = [&](const Vec3& local) {
    const Vec3 leaned = lean * local;
    return right * leaned.x + hullUp * leaned.y + forward * leaned.z;
  };
  hull.axes[0] = toWorld(Vec3(1, 0, 0));
  hull.axes[1] = toWorld(Vec3(0, 1, 0));
  hull.axes[2] = toWorld(Vec3(0, 0, 1));
  hull.halfExtents = Vec3(physics.hullHalfWidth, physics.hullHalfHeight, physics.hullHalfLength);
  // The hover lift itself follows the *surface* normal, not the leaned-up axis: leaning is the ship
  // rolling about its own centre, which doesn't change how high above the road that centre floats.
  hull.center = groundPos + hullUp * hullHoverOffset(physics);
  return hull;
}

StepResult Ship::step(const Simulation& simulation, double dt, double throttle, double brake, double steer,
                      std::optional<bool> meshModeOverride) {
  Ship& ship = *this;
  const bool useMeshPhysics = meshModeOverride.value_or(simulation.meshPhysicsEnabled());
  if (useMeshPhysics && simulation.track().collisionSurface)
    return stepMeshPhysics(ship, simulation, dt, throttle, brake, steer);

  Physics& p = ship.physics;
  const Vec3 previousPosition = p.groundPos;
  const bool startedAirborne = p.airborne;
  const bool hasTranslation = (throttle != 0.0) || (brake != 0.0) || std::fabs(p.speed) > 0.001;

  integrateSpeed(p, dt, throttle, brake);

  const double speedRatio = std::min(1.0, std::fabs(p.speed) / p.maxSpeed);

  Sample c = simulation.sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
  Vec3 surfaceNormal = c.normal;
  Vec3 surfaceRenderPos = p.groundPos;
  if (simulation.track().collisionSurface && !p.airborne) {
    if (const auto contact = simulation.track().collisionSurface->nearestAlongAxis(p.groundPos, p.up, 4.0)) {
      surfaceNormal = contact->normal;
    }
  }
  bool railHit = false;

  const Vec3 steerAxis = p.airborne ? UP : surfaceNormal;

  const double sgn = p.speed > 0 ? 1.0 : (p.speed < 0 ? -1.0 : 1.0);  // Math.sign(speed || 1)
  const double effectiveTurn = p.turnRate * (1 - 0.35 * speedRatio) * sgn;
  p.forward = applyAxisAngle(p.forward, steerAxis, steer * effectiveTurn * dt);
  tangentize(p.forward, steerAxis, p.forward);

  const double gripThisFrame = p.grip * (0.5 + 0.5 * (1 - std::min(std::fabs(steer) * speedRatio, 1.0)));
  const double toForward = signedAngleAbout(p.moveDir, p.forward, steerAxis);
  p.moveDir = applyAxisAngle(p.moveDir, steerAxis, toForward * std::min(gripThisFrame * dt, 1.0));
  tangentize(p.moveDir, steerAxis, p.forward);

  Vec3 vel = p.moveDir * p.speed;
  const double vx = vel.x, vz = vel.z;

  if (p.airborne) {
    double ax = vx, az = vz;
    double px = p.groundPos.x + ax * dt;
    double pz = p.groundPos.z + az * dt;

    p.verticalVel -= p.gravity * dt;
    p.groundPos = Vec3(px, p.groundPos.y + p.verticalVel * dt, pz);

    // Mesh regions (placed assets, reservation walls) were removed with no interim replacement
    // (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2) -- airborne landing always falls back to the
    // analytical corridor surface now.
    c = simulation.sampleTrack(px, p.groundPos.y, pz);
    const Projection proj = projectToSurface(c, px, p.groundPos.y, pz);
    const SurfaceFrame surface = curvedSurfaceFrame(c, proj.s);
    if (corridorContains(c, px, p.groundPos.y, pz, proj) && p.groundPos.y <= surface.pos.y) {
      const double impactSpeed = std::max(0.0, -p.verticalVel);
      landOnSurface(ship, surface.normal);
      applyLandingImpact(ship, impactSpeed);
      p.groundPos = surface.pos;
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  } else if (hasTranslation) {
    Vec3 newPos = p.groundPos + vel * dt;

    const Sample current = c;
    Projection projection = projectToSurface(current, newPos.x, newPos.y, newPos.z);
    const bool forceCurrentWall = !current.offEnd && (projection.s > projection.hiS || projection.s < projection.loS);

    if (!forceCurrentWall) {
      c = simulation.sampleTrack(newPos.x, newPos.y, newPos.z);
      projection = projectToSurface(c, newPos.x, newPos.y, newPos.z);
    }

    if (!forceCurrentWall && c.offEnd) {
      beginAirborne(ship, vel);
      p.groundPos = newPos;
    } else {
      const Vec3 er = projection.er;
      const double s = projection.s, loS = projection.loS, hiS = projection.hiS;

      int hitSign = 0;
      if (s > hiS)
        hitSign = 1;
      else if (s < loS)
        hitSign = -1;
      double finalS = s;
      if (hitSign) {
        finalS = TrackCore::clamp(s, loS, hiS);
        Vec3 wallN = er * (double)hitSign;
        const double into = glm::dot(vel, wallN);
        // Crossing loS/hiS is first of all a *positional* constraint (finalS, above): it also fires
        // on a car that merely brushed a limit which moved under it, which is what a narrowing
        // section does every frame to anything tracking near its edge. Only an actually
        // into-the-wall velocity earns an impulse -- and only then may speed/moveDir be rewritten.
        // Rewriting them unconditionally (as this did) drained weightSpeedRetain() off the speed of
        // a car with zero wall contact, once per frame for as long as the road kept narrowing.
        if (into > 0) {
          vel += wallN * (-into * (1 + weightRestitution(p)));
          addImpactJolt(p, into);
          // Preserve gear, as the reservation wall above does and for the same reason: a plain
          // `vel.length()`/`vel.normalize()` decomposition is always non-negative and re-points
          // moveDir along the post-bounce travel direction, flipping a reversing car into forward
          // gear with moveDir ~180 degrees off its heading. Held brake then decelerates that
          // positive speed back through zero while grip swings moveDir around to meet forward
          // again -- the car judders, slews sideways and makes almost no headway.
          const double gear = p.speed < 0.0 ? -1.0 : 1.0;
          const double mag = glm::length(vel);
          p.speed = gear * mag * weightSpeedRetain(p);
          if (mag > 1e-6) p.moveDir = normalizeSafe(vel * gear);
        }
      }

      SurfaceFrame surface = curvedSurfaceFrame(c, finalS);
      if (forceCurrentWall) {
        // Restore the along-track component of this step's motion. curvedSurfaceFrame() builds a
        // position purely as pos + edgeRight*sOff + normal*lift, i.e. with NO tangential component,
        // so it lands the ship exactly in `c`'s own station plane. That is harmless in the branch
        // above, where `c` was re-sampled at newPos and therefore already advanced along the track.
        // Here `c` is deliberately the sample taken at the ship's OLD position, and the only channel
        // newPos had into the result -- `s` -- was just clamped away by finalS. Without this term
        // groundPos becomes a pure function of the old groundPos, with velocity contributing exactly
        // nothing: a ship pressed onto the wall maps to itself and locks there permanently at full
        // indicated speed. A narrowing section is what makes it stick, because the shrinking hiS
        // re-clamps the ship every frame and so keeps latching forceCurrentWall on.
        const double along = (newPos.x - c.pos.x) * c.tangent.x + (newPos.y - c.pos.y) * c.tangent.y +
                             (newPos.z - c.pos.z) * c.tangent.z;
        surface.pos += c.tangent * along;
      }
      p.groundPos = surface.pos;
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  }

  if (!p.airborne && !hasTranslation) {
    c = simulation.sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
    const Projection parkedProjection = projectToSurface(c, p.groundPos.x, p.groundPos.y, p.groundPos.z);
    if (!corridorContains(c, p.groundPos.x, p.groundPos.y, p.groundPos.z, parkedProjection)) {
      Vec3 zero(0, 0, 0);
      beginAirborne(ship, zero);
      surfaceRenderPos = p.groundPos;
      surfaceNormal = UP;
    } else {
      surfaceRenderPos = p.groundPos;
      surfaceNormal = p.up;
    }
  }

  // A native Track resource may make its selected exported triangles
  // authoritative for road contact. The analytical branches above continue to
  // provide rails, path ownership, zones and edge behavior; this final contact
  // pass replaces their surface position/normal with the actual rendered road.
  if (simulation.track().collisionSurface) {
    std::optional<CollisionHit> contact;
    if (p.airborne) contact = simulation.track().collisionSurface->sweep(previousPosition, p.groundPos);
    if (!contact && (!p.airborne || !startedAirborne))
      contact = simulation.track().collisionSurface->nearestAlongAxis(p.groundPos, surfaceNormal, 4.0);

    if (contact) {
      if (p.airborne) {
        const double impactSpeed = std::max(0.0, -p.verticalVel);
        landOnSurface(ship, contact->normal);
        applyLandingImpact(ship, impactSpeed);
      }
      p.groundPos = contact->position;
      surfaceRenderPos = contact->position;
      surfaceNormal = contact->normal;
      tangentize(p.moveDir, surfaceNormal, p.forward);
      tangentize(p.forward, surfaceNormal, p.moveDir);
    } else if (!p.airborne) {
      beginAirborne(ship, p.moveDir * p.speed);
      surfaceRenderPos = p.groundPos;
    }
  }

  tickBoost(ship, dt);
  tickBob(ship, dt);
  tickHoverBounce(ship, dt, startedAirborne && !p.airborne);
  tickLean(ship, dt, steer);
  if (!p.airborne) simulation.detectZoneTriggers(ship, c);

  simulation.detectTriggers(ship, ship.prevTriggerPos, p.groundPos);
  ship.prevTriggerPos = p.groundPos;

  if (p.airborne && p.groundPos.y < simulation.track().trackFloorY) {
    simulation.respawn(ship);
    return {surfaceNormal, surfaceRenderPos, true, railHit};
  }
  return {surfaceNormal, surfaceRenderPos, false, railHit};
}

void Ship::respawn(const Simulation& simulation) { simulation.respawn(*this); }

void Ship::placeAt(const Simulation& simulation, const Pose& pose, const std::string& disarmedId) {
  simulation.placeShipAtPose(*this, pose, disarmedId);
}

}  // namespace tox
