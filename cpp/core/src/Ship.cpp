#include "Ship.hpp"

#include "Simulation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace tox {

StepResult Ship::step(const Simulation& simulation, double dt, double throttle, double brake, double steer) {
  Ship& ship = *this;

  Physics& p = ship.physics;
  const Vec3 previousPosition = p.groundPos;
  const bool startedAirborne = p.airborne;
  const bool hasTranslation = (throttle != 0.0) || (brake != 0.0) || std::fabs(p.speed) > 0.001;

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

  const MeshRegion* meshRegion = simulation.surfaceOwnerAt(p.groundPos.x, p.groundPos.z, p.groundPos.y, c);

  const Vec3 steerAxis = (p.airborne || meshRegion) ? UP : surfaceNormal;

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
    for (const auto& region : simulation.track().meshRegions) {
      // Bounds first, then the height test: elevationAt is a triangle scan for a curved-floor region
      // (a Capped reservation) and a plain read for every other, so this ordering keeps the common
      // case free while letting the clearance test use the floor height under the ship rather than
      // one scalar for a floor that can vary tens of metres across a single region.
      if (!region.withinBounds(p.groundPos.x, p.groundPos.z, px, pz, TrackCore::COLLISION_WALL_MARGIN)) continue;
      if (p.groundPos.y >= region.elevationAt(p.groundPos.x, p.groundPos.z) + region.railClearanceHeight) continue;
      Vec2d velocity{ax, az};
      const double before = std::hypot(ax, az);
      const MeshMoveResult moved = slideAlongRails(region, {p.groundPos.x, p.groundPos.z}, {px, pz},
                                                   velocity, TrackCore::COLLISION_WALL_MARGIN,
                                                   weightRestitution(p));
      if (!moved.hit) continue;
      railHit = true;
      px = moved.x;
      pz = moved.z;
      ax = velocity.x;
      az = velocity.y;
      p.speed = std::hypot(ax, az) * weightSpeedRetain(p);
      addImpactJolt(p, before - std::hypot(ax, az));
      if (p.speed > 1e-6) p.moveDir = normalizeSafe(Vec3(ax, 0, az));
    }

    p.verticalVel -= p.gravity * dt;
    p.groundPos = Vec3(px, p.groundPos.y + p.verticalVel * dt, pz);

    const MeshRegion* landing = simulation.meshRegionAt(px, pz, p.groundPos.y);
    const double landingY = landing ? landing->elevationAt(px, pz) : 0.0;
    if (landing && p.groundPos.y <= landingY) {
      const double impactSpeed = std::max(0.0, -p.verticalVel);
      landOnSurface(ship, UP);
      p.landingBounce += std::min(3.2, impactSpeed * 0.09);
      p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
      p.groundPos = Vec3(px, landingY, pz);
      surfaceRenderPos = p.groundPos;
      surfaceNormal = UP;
    } else if (!landing) {
      // Only falls back to the analytical corridor surface when no mesh region's footprint claims
      // this (x,z) at all. The corridor is built from the road's lateral sLeft/sRight alone and has
      // no notion of a reservation's void (CENTRAL_RESERVATION_PLAN.md M6) -- it reads as solid
      // ground running straight through the middle of one. A Capped reservation's floor sits lower
      // than that phantom surface, so if `landing` is non-null here (its footprint claims (x,z),
      // just not reached its elevation yet this frame), falling through to the corridor would catch
      // the ship on the wrong, too-high surface before it ever reaches the real floor beneath it.
      c = simulation.sampleTrack(px, p.groundPos.y, pz);
      const Projection proj = projectToSurface(c, px, p.groundPos.y, pz);
      const SurfaceFrame surface = curvedSurfaceFrame(c, proj.s);
      if (corridorContains(c, px, p.groundPos.y, pz, proj) && p.groundPos.y <= surface.pos.y) {
        const double impactSpeed = std::max(0.0, -p.verticalVel);
        landOnSurface(ship, surface.normal);
        p.landingBounce += std::min(3.2, impactSpeed * 0.09);
        p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
        p.groundPos = surface.pos;
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      }
    }
  } else if (meshRegion && hasTranslation) {
    Vec2d velocity{vx, vz};
    const MeshMoveResult moved = slideAlongRails(*meshRegion, {p.groundPos.x, p.groundPos.z},
                                                 {p.groundPos.x + vx * dt, p.groundPos.z + vz * dt},
                                                 velocity, TrackCore::COLLISION_WALL_MARGIN,
                                                 weightRestitution(p));
    if (moved.hit) {
      railHit = true;
      const double before = std::hypot(vx, vz), after = std::hypot(velocity.x, velocity.y);
      // Gear-preserving, as for the reservation and corridor walls: reversing into a platform's
      // own rail otherwise flips the car into forward gear on contact.
      const double gear = p.speed < 0.0 ? -1.0 : 1.0;
      p.speed = gear * after * weightSpeedRetain(p);
      if (after > 1e-6) p.moveDir = normalizeSafe(Vec3(velocity.x * gear, 0, velocity.y * gear));
      addImpactJolt(p, before - after);
    }

    // The height the ship is leaving from, sampled under its *destination* so a curved floor is
    // followed across the move rather than snapped to one scalar for the whole region.
    const double leavingY = meshRegion->elevationAt(moved.x, moved.z);
    const MeshRegion* stillOn = meshRegion->contains(moved.x, moved.z)
                                    ? meshRegion
                                    : simulation.meshRegionAt(moved.x, moved.z, leavingY);
    if (stillOn) {
      p.groundPos = Vec3(moved.x, stillOn->elevationAt(moved.x, moved.z), moved.z);
      surfaceRenderPos = p.groundPos;
      surfaceNormal = UP;
    } else {
      c = simulation.sampleTrack(moved.x, leavingY, moved.z);
      const Projection projection = projectToSurface(c, moved.x, leavingY, moved.z);
      const bool overCorridor = corridorContains(c, moved.x, leavingY, moved.z, projection);
      const SurfaceFrame surface = curvedSurfaceFrame(c, projection.s);
      if (overCorridor && std::fabs(surface.pos.y - leavingY) <= Consts::SURFACE_SNAP_UP) {
        p.groundPos = surface.pos;
        tangentize(p.moveDir, surface.normal, p.forward);
        tangentize(p.forward, surface.normal, p.moveDir);
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      } else {
        beginAirborne(ship, p.moveDir * p.speed);
        p.groundPos = Vec3(moved.x, leavingY, moved.z);
      }
    }
  } else if (hasTranslation) {
    Vec3 newPos = p.groundPos + vel * dt;

    // Central-reservation walls (CENTRAL_RESERVATION_PLAN.md M2, M6): a car driving the main
    // corridor isn't "on" any mesh region yet (an Uncapped reservation never is -- no floor at all;
    // a Capped one only becomes ownable once the car's actually inside its footprint) and isn't
    // airborne, so neither of this function's other mesh-region checks (the ownership branch above,
    // the airborne-loop below) ever runs for it here. `oneWayRails` is what identifies a
    // reservation's synthetic MeshRegion now (M6 gives a Capped one real polygons too, so
    // `polygons.empty()` alone stopped reliably telling reservations apart from real placed mesh
    // regions) -- only reservations gatekeep the corridor this way; a real platform's edge still
    // only collides via ownership/airborne-proximity, unchanged from before this feature.
    for (const auto& region : simulation.track().meshRegions) {
      if (!region.oneWayRails) continue;
      if (!region.withinBounds(p.groundPos.x, p.groundPos.z, newPos.x, newPos.z, TrackCore::COLLISION_WALL_MARGIN))
        continue;
      Vec2d velocity{vel.x, vel.z};
      const double before = std::hypot(vel.x, vel.z);
      const MeshMoveResult moved = slideAlongRails(region, {p.groundPos.x, p.groundPos.z}, {newPos.x, newPos.z},
                                                   velocity, TrackCore::COLLISION_WALL_MARGIN, weightRestitution(p));
      if (!moved.hit) continue;
      railHit = true;
      newPos.x = moved.x;
      newPos.z = moved.z;
      vel.x = velocity.x;
      vel.z = velocity.y;
      // Preserve gear (forward/reverse), unlike a plain `vel.length()`/`vel.normalize()` decomposition
      // (the outer sLeft/sRight wall's own pattern, further down this function): that always yields a
      // non-negative speed and reorients moveDir to match, which forces the car into forward gear on
      // every bounce. Driving in reverse into this wall repeatedly (a median is far more likely to be
      // backed into than the track's outer edge) then oscillates forever -- brake keeps decelerating
      // the now-positive speed back through zero into reverse, sending the car back into the same
      // wall from a slightly different angle each time, denied any use of its resulting outward
      // impulse to actually escape it (physics-breaks report -- CENTRAL_RESERVATION_PLAN.md M2).
      const double gear = p.speed < 0.0 ? -1.0 : 1.0;
      const double mag = std::hypot(vel.x, vel.z);
      p.speed = gear * mag * weightSpeedRetain(p);
      addImpactJolt(p, before - mag);
      if (mag > 1e-6) p.moveDir = normalizeSafe(Vec3(vel.x * gear, 0, vel.z * gear));
    }

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

  if (!p.airborne && !hasTranslation && meshRegion) {
    p.groundPos.y = meshRegion->elevationAt(p.groundPos.x, p.groundPos.z);
    surfaceRenderPos = p.groundPos;
    surfaceNormal = UP;
  } else if (!p.airborne && !hasTranslation) {
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
        p.landingBounce += std::min(3.2, impactSpeed * 0.09);
        p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
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
  if (!p.airborne) simulation.detectZoneTriggers(ship, c, meshRegion);

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
