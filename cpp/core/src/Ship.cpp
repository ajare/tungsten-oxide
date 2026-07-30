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
  p.forward.applyAxisAngle(steerAxis, steer * effectiveTurn * dt);
  tangentize(p.forward, steerAxis, p.forward);

  const double gripThisFrame = p.grip * (0.5 + 0.5 * (1 - std::min(std::fabs(steer) * speedRatio, 1.0)));
  const double toForward = signedAngleAbout(p.moveDir, p.forward, steerAxis);
  p.moveDir.applyAxisAngle(steerAxis, toForward * std::min(gripThisFrame * dt, 1.0));
  tangentize(p.moveDir, steerAxis, p.forward);

  Vec3 vel = p.moveDir.clone().multiplyScalar(p.speed);
  const double vx = vel.x, vz = vel.z;

  if (p.airborne) {
    double ax = vx, az = vz;
    double px = p.groundPos.x + ax * dt;
    double pz = p.groundPos.z + az * dt;
    for (const auto& region : simulation.track().meshRegions) {
      if (p.groundPos.y >= region.elevation + region.railHeight) continue;
      if (!region.withinBounds(px, pz, TrackCore::COLLISION_WALL_MARGIN)) continue;
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
      if (p.speed > 1e-6) p.moveDir.set(ax, 0, az).normalize();
    }

    p.verticalVel -= p.gravity * dt;
    p.groundPos.set(px, p.groundPos.y + p.verticalVel * dt, pz);

    const MeshRegion* landing = simulation.meshRegionAt(px, pz, p.groundPos.y);
    if (landing && p.groundPos.y <= landing->elevation) {
      const double impactSpeed = std::max(0.0, -p.verticalVel);
      landOnSurface(ship, UP);
      p.landingBounce += std::min(3.2, impactSpeed * 0.09);
      p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
      p.groundPos.set(px, landing->elevation, pz);
      surfaceRenderPos = p.groundPos;
      surfaceNormal = UP;
    } else {
      c = simulation.sampleTrack(px, p.groundPos.y, pz);
      const Projection proj = projectToSurface(c, px, p.groundPos.y, pz);
      const SurfaceFrame surface = curvedSurfaceFrame(c, proj.s);
      if (corridorContains(c, px, p.groundPos.y, pz, proj) && p.groundPos.y <= surface.pos.y) {
        const double impactSpeed = std::max(0.0, -p.verticalVel);
        landOnSurface(ship, surface.normal);
        p.landingBounce += std::min(3.2, impactSpeed * 0.09);
        p.landingBounceVel += std::min(16.0, impactSpeed * 0.35);
        p.groundPos.copy(surface.pos);
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
      p.speed = after * weightSpeedRetain(p);
      if (p.speed > 1e-6) p.moveDir.set(velocity.x, 0, velocity.y).normalize();
      addImpactJolt(p, before - after);
    }

    const MeshRegion* stillOn = meshRegion->contains(moved.x, moved.z)
                                    ? meshRegion
                                    : simulation.meshRegionAt(moved.x, moved.z, meshRegion->elevation);
    if (stillOn) {
      p.groundPos.set(moved.x, stillOn->elevation, moved.z);
      surfaceRenderPos = p.groundPos;
      surfaceNormal = UP;
    } else {
      c = simulation.sampleTrack(moved.x, meshRegion->elevation, moved.z);
      const Projection projection = projectToSurface(c, moved.x, meshRegion->elevation, moved.z);
      const bool overCorridor = corridorContains(c, moved.x, meshRegion->elevation, moved.z, projection);
      const SurfaceFrame surface = curvedSurfaceFrame(c, projection.s);
      if (overCorridor && std::fabs(surface.pos.y - meshRegion->elevation) <= Consts::SURFACE_SNAP_UP) {
        p.groundPos.copy(surface.pos);
        tangentize(p.moveDir, surface.normal, p.forward);
        tangentize(p.forward, surface.normal, p.moveDir);
        surfaceRenderPos = surface.pos;
        surfaceNormal = surface.normal;
      } else {
        beginAirborne(ship, p.moveDir.clone().multiplyScalar(p.speed));
        p.groundPos.set(moved.x, meshRegion->elevation, moved.z);
      }
    }
  } else if (hasTranslation) {
    Vec3 newPos = p.groundPos.clone().addScaledVector(vel, dt);

    // Central-reservation walls (CENTRAL_RESERVATION_PLAN.md M2): a car driving the main corridor
    // is never "on" one of these regions (they have no floor -- see reservationGeometry in
    // TrackBake.cpp) and isn't airborne, so neither of this function's other mesh-region checks
    // (the ownership branch above, the airborne-loop below) ever runs for it here. `polygons.empty()`
    // is what distinguishes a reservation's rails-only synthetic MeshRegion from a real placed mesh
    // asset's region (which always has compiled polygons/triangles) -- only the former gatekeeps the
    // corridor this way; a real platform's edge still only collides via ownership/airborne-proximity,
    // unchanged from before this feature.
    for (const auto& region : simulation.track().meshRegions) {
      if (!region.polygons.empty()) continue;
      if (!region.withinBounds(newPos.x, newPos.z, TrackCore::COLLISION_WALL_MARGIN)) continue;
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
      if (mag > 1e-6) p.moveDir.set(vel.x * gear, 0, vel.z * gear).normalize();
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
      p.groundPos.copy(newPos);
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
        Vec3 wallN = er.clone().multiplyScalar((double)hitSign);
        const double into = vel.dot(wallN);
        if (into > 0) {
          vel.addScaledVector(wallN, -into * (1 + weightRestitution(p)));
          addImpactJolt(p, into);
        }
        p.speed = vel.length() * weightSpeedRetain(p);
        if (p.speed > 1e-6) p.moveDir.copy(vel).normalize();
      }

      const SurfaceFrame surface = curvedSurfaceFrame(c, finalS);
      p.groundPos.copy(surface.pos);
      surfaceRenderPos = surface.pos;
      surfaceNormal = surface.normal;
    }
  }

  if (!p.airborne && !hasTranslation && meshRegion) {
    p.groundPos.y = meshRegion->elevation;
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
      p.groundPos.copy(contact->position);
      surfaceRenderPos = contact->position;
      surfaceNormal = contact->normal;
      tangentize(p.moveDir, surfaceNormal, p.forward);
      tangentize(p.forward, surfaceNormal, p.moveDir);
    } else if (!p.airborne) {
      beginAirborne(ship, p.moveDir.clone().multiplyScalar(p.speed));
      surfaceRenderPos = p.groundPos;
    }
  }

  tickBoost(ship, dt);
  if (!p.airborne) simulation.detectZoneTriggers(ship, c, meshRegion);

  simulation.detectTriggers(ship, ship.prevTriggerPos, p.groundPos);
  ship.prevTriggerPos.copy(p.groundPos);

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
