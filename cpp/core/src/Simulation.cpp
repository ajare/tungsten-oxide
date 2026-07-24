// Simulation.cpp — bodies of the physics step + helpers declared in
// include/Simulation.hpp, transliterated line-for-line from js/track-physics.js.
// See the header for the milestone scope / mesh-out-of-scope rationale.
#include "Simulation.hpp"
#include <algorithm>
#include <cmath>

namespace tox {

// --- pure helpers ----------------------------------------------------------
double effectiveMaxSpeed(const Physics& p) {
  return p.boostActive ? std::max(p.maxSpeed, p.boostEffCap) : p.maxSpeed;
}

// Start a boost for one ship (mirror of track-physics.js triggerBoost). Each ship
// owns its own lock/cap; `|| DEFAULT` guards a zone missing factor/duration.
void triggerBoost(Ship& ship, const Zone& zone) {
  Physics& p = ship.physics;
  if (p.boostActive) return;
  p.boostActive = true;
  p.boostReleasing = false;
  p.boostHold = zone.duration != 0.0 ? zone.duration : TrackCore::DEFAULT_BOOST_DURATION;
  p.boostReleaseT = Consts::ZONE_RELEASE;
  p.boostCap = (zone.factor != 0.0 ? zone.factor : TrackCore::DEFAULT_BOOST_FACTOR) * p.maxSpeed;
  p.boostEffCap = p.boostCap;
  if (p.speed > 0) p.speed = std::max(p.speed, p.boostCap);
}

void tickBoost(Ship& ship, double dt) {
  Physics& p = ship.physics;
  if (!p.boostActive) return;
  if (!p.boostReleasing) {
    p.boostHold -= dt;
    p.boostEffCap = p.boostCap;
    if (p.boostHold <= 0) {
      p.boostReleasing = true;
      p.boostReleaseT = Consts::ZONE_RELEASE;
    }
  } else {
    p.boostReleaseT -= dt;
    const double frac = std::max(0.0, std::min(1.0, p.boostReleaseT / Consts::ZONE_RELEASE));
    p.boostEffCap = p.maxSpeed + (p.boostCap - p.maxSpeed) * frac;
    if (p.boostReleaseT <= 0) {
      p.boostActive = false;
      p.boostEffCap = p.maxSpeed;
    }
  }
}

Projection projectToSurface(const Sample& s, double px, double py, double pz) {
  Projection out;
  out.er = s.edgeRight;
  out.s = (px - s.pos.x) * s.edgeRight.x + (py - s.pos.y) * s.edgeRight.y + (pz - s.pos.z) * s.edgeRight.z;
  double loS = s.sLeft + TrackCore::COLLISION_WALL_MARGIN;
  double hiS = s.sRight - TrackCore::COLLISION_WALL_MARGIN;
  if (loS > hiS) {
    const double m = (loS + hiS) / 2.0;
    loS = m;
    hiS = m;
  }
  out.loS = loS;
  out.hiS = hiS;
  return out;
}

bool corridorContains(const Sample& s, double x, double y, double z, const Projection& proj) {
  if (s.offEnd || proj.s < proj.loS || proj.s > proj.hiS) return false;
  const double along = (x - s.pos.x) * s.tangent.x + (y - s.pos.y) * s.tangent.y + (z - s.pos.z) * s.tangent.z;
  return std::fabs(along) <= Consts::CORRIDOR_ALONG_TOL;
}

SurfaceFrame curvedSurfaceFrame(const Sample& s, double sOff) {
  const double lo = s.sLeft, hi = s.sRight, span = hi - lo;
  const double v = std::fabs(span) < 1e-6 ? 0.5 : (sOff - lo) / span;
  const double lift = TrackCore::crossSectionHeight(s.crossSectionCurvature, s.crossSectionTightness, v, std::fabs(span));
  Vec3 pos = s.pos.clone().addScaledVector(s.edgeRight, sOff).addScaledVector(s.normal, lift);
  const double dhdv = TrackCore::crossSectionHeightDerivative(s.crossSectionCurvature, s.crossSectionTightness, v, std::fabs(span));
  Vec3 crossT = s.edgeRight.clone().multiplyScalar(span).addScaledVector(s.normal, dhdv);
  Vec3 normal;
  normal.crossVectors(s.tangent, crossT).normalize();
  if (normal.dot(s.normal) < 0) normal.negate();
  return {pos, normal};
}

// Re-project unit-ish v into the plane tangent to n; fall back when parallel.
Vec3& tangentize(Vec3& v, const Vec3& n, const Vec3& fallback) {
  const double d = v.dot(n);
  Vec3 tmp = v.clone();
  tmp.addScaledVector(n, -d);
  if (tmp.lengthSq() < 1e-9) {
    v.copy(fallback);
    return v;
  }
  v.copy(tmp).normalize();
  return v;
}

double signedAngleAbout(const Vec3& a, const Vec3& b, const Vec3& axis) {
  const double d = TrackCore::clamp(a.dot(b), -1.0, 1.0);
  const double ang = std::acos(d);
  Vec3 cross;
  cross.crossVectors(a, b);
  return cross.dot(axis) < 0 ? -ang : ang;
}

void beginAirborne(Ship& ship, const Vec3& vel3D) {
  Physics& p = ship.physics;
  p.airborne = true;
  p.verticalVel = vel3D.y;
  const double horiz = std::hypot(vel3D.x, vel3D.z);
  p.speed = horiz;
  if (horiz > 1e-6)
    p.moveDir.set(vel3D.x / horiz, 0, vel3D.z / horiz);
  else
    tangentize(p.moveDir, UP, p.forward);
}

void landOnSurface(Ship& ship, const Vec3& normal) {
  Physics& p = ship.physics;
  p.airborne = false;
  p.verticalVel = 0;
  tangentize(p.moveDir, normal, p.forward);
  tangentize(p.forward, normal, p.moveDir);
}

double weightRestitution(const Physics& p) {
  const double m = (p.weight != 0.0 ? p.weight : Consts::HANDLING_BASE_WEIGHT) / Consts::HANDLING_BASE_WEIGHT;
  return std::max(0.0, std::min(0.9, p.wallRestitution / m));
}
double weightSpeedRetain(const Physics& p) {
  const double m = (p.weight != 0.0 ? p.weight : Consts::HANDLING_BASE_WEIGHT) / Consts::HANDLING_BASE_WEIGHT;
  return std::max(0.85, std::min(0.999, 1.0 - 0.02 / m));
}
void addImpactJolt(Physics& p, double normalImpactSpeed) {
  const double m = (p.weight != 0.0 ? p.weight : Consts::HANDLING_BASE_WEIGHT) / Consts::HANDLING_BASE_WEIGHT;
  const double momentum = m * std::max(0.0, normalImpactSpeed);
  p.landingBounce += std::min(2.0, momentum * 0.012);
  p.landingBounceVel += std::min(10.0, momentum * 0.05);
}

// --- Simulation ------------------------------------------------------------
Simulation::Simulation(const Track& track) : track_(track) {}

Sample Simulation::sampleTrack(double x, double y, double z) const {
  const auto& paths = track_.paths;
  struct Cand {
    int path{0}, a{0}, b{1};
    double t{0.0}, d{1e300};
    bool valid{false};
  };
  Cand fallback;
  fallback.path = 0;
  fallback.d = 1e300;
  Cand bestUnder;

  for (int pi = 0; pi < (int)paths.size(); ++pi) {
    const Path& path = paths[pi];
    const auto& cl = path.centerline;
    const int M = (int)cl.size();
    const int segCount = path.closed ? M : M - 1;
    for (int i = 0; i < segCount; ++i) {
      const int j = path.closed ? (i + 1) % M : i + 1;
      const Frame& a = cl[i];
      const Frame& b = cl[j];
      const double sx = b.pos.x - a.pos.x, sy = b.pos.y - a.pos.y, sz = b.pos.z - a.pos.z;
      const double segLen2 = sx * sx + sy * sy + sz * sz;
      const double t = segLen2 > 0
                           ? TrackCore::clamp(((x - a.pos.x) * sx + (y - a.pos.y) * sy + (z - a.pos.z) * sz) / segLen2, 0.0, 1.0)
                           : 0.0;
      const double px = a.pos.x + sx * t, py = a.pos.y + sy * t, pz = a.pos.z + sz * t;
      const double dx = x - px, dy = y - py, dz = z - pz;
      const double d = dx * dx + dy * dy + dz * dz;
      if (d < fallback.d) { fallback = {pi, i, j, t, d, true}; }

      double erx = a.edgeRight.x + (b.edgeRight.x - a.edgeRight.x) * t;
      double ery = a.edgeRight.y + (b.edgeRight.y - a.edgeRight.y) * t;
      double erz = a.edgeRight.z + (b.edgeRight.z - a.edgeRight.z) * t;
      double erl = std::hypot(erx, ery, erz);
      if (erl == 0.0) erl = 1.0;
      erx /= erl;
      ery /= erl;
      erz /= erl;
      const double cx = a.pos.x + (b.pos.x - a.pos.x) * t;
      const double cy = a.pos.y + (b.pos.y - a.pos.y) * t;
      const double cz = a.pos.z + (b.pos.z - a.pos.z) * t;
      const double lateral = (x - cx) * erx + (y - cy) * ery + (z - cz) * erz;
      double loS = (a.sLeft + (b.sLeft - a.sLeft) * t) + TrackCore::COLLISION_WALL_MARGIN;
      double hiS = (a.sRight + (b.sRight - a.sRight) * t) - TrackCore::COLLISION_WALL_MARGIN;
      if (loS > hiS) {
        const double m = (loS + hiS) / 2.0;
        loS = m;
        hiS = m;
      }
      bool wouldOffEnd = false;
      if (!path.closed) {
        if (i == 0 && t <= 1e-4) {
          const Frame& e = cl[0];
          wouldOffEnd = !track_.endpointConnected(path.endpointIds.start, path.endpointIds.hasStart) &&
                        ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
        } else if (j == M - 1 && t >= 1 - 1e-4) {
          const Frame& e = cl[M - 1];
          wouldOffEnd = !track_.endpointConnected(path.endpointIds.end, path.endpointIds.hasEnd) &&
                        ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
        }
      }
      const double alongSeg = segLen2 > 0 ? ((x - px) * sx + (y - py) * sy + (z - pz) * sz) / std::sqrt(segLen2) : 0.0;
      const bool overSegment = std::fabs(alongSeg) <= Consts::SEGMENT_ALONG_TOL;
      if (overSegment && !wouldOffEnd && lateral >= loS && lateral <= hiS && (!bestUnder.valid || d < bestUnder.d))
        bestUnder = {pi, i, j, t, d, true};
    }
  }

  const Cand best = bestUnder.valid ? bestUnder : fallback;
  const Path& bestPath = paths[best.path];
  const Frame& a = bestPath.centerline[best.a];
  const Frame& b = bestPath.centerline[best.b];
  const double t = best.t;

  Sample smp;
  smp.pathIndex = best.path;
  smp.a = best.a;
  smp.b = best.b;
  smp.segT = t;
  smp.pos.copy(a.pos).lerp(b.pos, t);
  smp.tangent.copy(a.tangent).lerp(b.tangent, t).normalize();
  smp.edgeRight.copy(a.edgeRight).lerp(b.edgeRight, t).normalize();
  smp.normal.copy(a.normal).lerp(b.normal, t).normalize();
  smp.halfW = a.halfW + (b.halfW - a.halfW) * t;
  smp.crossSectionCurvature = a.crossSectionCurvature + (b.crossSectionCurvature - a.crossSectionCurvature) * t;
  smp.crossSectionTightness = a.crossSectionTightness + (b.crossSectionTightness - a.crossSectionTightness) * t;
  smp.sLeft = a.sLeft + (b.sLeft - a.sLeft) * t;
  smp.sRight = a.sRight + (b.sRight - a.sRight) * t;
  smp.offEnd = false;
  if (!bestPath.closed) {
    const int M = (int)bestPath.centerline.size();
    if (best.a == 0 && t <= 1e-4) {
      const Frame& e = bestPath.centerline[0];
      smp.offEnd = !track_.endpointConnected(bestPath.endpointIds.start, bestPath.endpointIds.hasStart) &&
                   ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) < 0;
    } else if (best.b == M - 1 && t >= 1 - 1e-4) {
      const Frame& e = bestPath.centerline[M - 1];
      smp.offEnd = !track_.endpointConnected(bestPath.endpointIds.end, bestPath.endpointIds.hasEnd) &&
                   ((x - e.pos.x) * e.tangent.x + (y - e.pos.y) * e.tangent.y + (z - e.pos.z) * e.tangent.z) > 0;
    }
  }
  return smp;
}

double Simulation::shipParamG(const Sample& sample) const {
  const Path& pth = track_.paths[sample.pathIndex];
  const int M = (int)pth.centerline.size();
  const int CP_N = (int)pth.anchors.size();
  auto gAt = [&](int i) -> double {
    return pth.closed ? ((double)i / M) * CP_N
                      : (M > 1 ? ((double)i / (M - 1)) * (CP_N - 1) : 0.0);
  };
  const double ga = gAt(sample.a);
  double gb = gAt(sample.b);
  if (pth.closed && sample.b < sample.a) gb += CP_N;  // the wrap segment M-1 -> 0
  return ga + (gb - ga) * sample.segT;
}

void Simulation::detectZoneTriggers(Ship& ship, const Sample& sample, bool meshRegion) const {
  Physics& p = ship.physics;
  for (const Zone& z : track_.zones) {
    bool inside = false;
    if (z.kind == "path") {
      if (!meshRegion && sample.pathIndex == z.hostPathIndex) {
        const Projection proj = projectToSurface(sample, p.groundPos.x, p.groundPos.y, p.groundPos.z);
        inside = TrackCore::zoneAlongContains(shipParamG(sample), z.gLo, z.gHi, z.gMax, z.closed) &&
                 std::fabs(proj.s - z.lateral) <= z.halfWidth;
      }
    }
    const bool wasInside = ship.zoneInside.count(z.id) ? ship.zoneInside[z.id] : false;
    if (inside && !wasInside && z.effect == "velocityChange") triggerBoost(ship, z);
    ship.zoneInside[z.id] = inside;
  }
}

void Simulation::detectTriggers(Ship& ship, const Vec3& p0, const Vec3& p1) const {
  for (const Trigger& tr : track_.triggers) {
    TriggerState& state = ship.triggerStates[tr.id];
    const Vec3& c = tr.center;
    const double d0 = (p0.x - c.x) * tr.fwd.x + (p0.y - c.y) * tr.fwd.y + (p0.z - c.z) * tr.fwd.z;
    const double d1 = (p1.x - c.x) * tr.fwd.x + (p1.y - c.y) * tr.fwd.y + (p1.z - c.z) * tr.fwd.z;
    const double rr = (p1.x - c.x) * tr.right.x + (p1.y - c.y) * tr.right.y + (p1.z - c.z) * tr.right.z;
    const double uu = (p1.x - c.x) * tr.up.x + (p1.y - c.y) * tr.up.y + (p1.z - c.z) * tr.up.z;
    if (!state.armed && (std::fabs(rr) > tr.halfWidth || uu < 0 || uu > tr.height || std::fabs(d1) > Consts::TRIGGER_REARM_MARGIN)) state.armed = true;
    if (state.armed && d0 != d1 && ((d0 <= 0 && d1 > 0) || (d0 >= 0 && d1 < 0))) {
      const double t = d0 / (d0 - d1);
      const double xr = (p0.x + (p1.x - p0.x) * t - c.x), yr = (p0.y + (p1.y - p0.y) * t - c.y), zr = (p0.z + (p1.z - p0.z) * t - c.z);
      const double lr = xr * tr.right.x + yr * tr.right.y + zr * tr.right.z;
      const double lu = xr * tr.up.x + yr * tr.up.y + zr * tr.up.z;
      if (std::fabs(lr) <= tr.halfWidth && lu >= 0 && lu <= tr.height) {
        const std::string dir = d1 > d0 ? "forward" : "backward";
        if (tr.direction == "both" || tr.direction == dir) {
          fireTrigger(ship, tr, dir);
          state.armed = false;
        }
      }
    }
  }
}

void Simulation::fireTrigger(Ship& ship, const Trigger& rec, const std::string& dir) const {
  // The onTriggerFired hook (console log, player checkpoint flash) is game-only;
  // the portable checkpoint/lap logic runs regardless.
  if (rec.type != "checkpoint") return;
  Checkpoint& checkpoint = ship.lastCheckpoint;
  checkpoint.valid = true;
  checkpoint.triggerId = rec.id;
  checkpoint.pos.copy(rec.center);
  checkpoint.up.copy(rec.up);
  checkpoint.forward.copy(rec.fwd).multiplyScalar(dir == "backward" ? -1.0 : 1.0);

  Race& race = ship.race;
  if (rec.role != "finish") {
    race.hit.insert(rec.id);
    return;
  }
  for (const std::string& id : race.intermediateIds)
    if (!race.hit.count(id)) return;
  race.laps++;
  race.hit.clear();
  // race.lapStartedAt/flashUntil are wall-clock fields (this.now()) the parity
  // Race does not carry, so they are intentionally not tracked here.
}

void Simulation::clearBoost(Ship& ship) const {
  Physics& p = ship.physics;
  p.boostActive = false;
  p.boostReleasing = false;
  p.boostHold = 0;
  p.boostReleaseT = 0;
  p.boostCap = 0;
  p.boostEffCap = 0;
  ship.zoneInside.clear();
}

void Simulation::resetTriggers(Ship& ship, const std::string& disarmedId) const {
  ship.prevTriggerPos.copy(ship.physics.groundPos);
  for (const Trigger& tr : track_.triggers) ship.triggerStates[tr.id] = TriggerState{tr.id != disarmedId, 0.0};
}

void Simulation::placeShipAtPose(Ship& ship, const Pose& pose, const std::string& disarmedId) const {
  Physics& p = ship.physics;
  p.groundPos.copy(pose.pos);
  p.visualGroundPos.copy(pose.pos);
  p.forward.copy(pose.forward);
  p.moveDir.copy(pose.forward);
  p.up.copy(pose.up);
  p.visualUp.copy(pose.up);
  p.right.crossVectors(pose.up, pose.forward).normalize();
  p.heading = std::atan2(pose.forward.x, pose.forward.z);
  p.speed = 0;
  p.airborne = false;
  p.verticalVel = 0;
  p.visualBank = 0;
  p.visualPitch = 0;
  p.landingBounce = 0;
  p.landingBounceVel = 0;
  // Rendered group placement is the host's concern; headless ships skip it.
  clearBoost(ship);
  resetTriggers(ship, disarmedId);
}

void Simulation::respawn(Ship& ship) const {
  const Checkpoint& cp = ship.lastCheckpoint;
  if (cp.valid)
    placeShipAtPose(ship, Pose{cp.pos, cp.up, cp.forward}, cp.triggerId);
  else
    placeShipAtPose(ship, ship.startPose, std::string());  // no id disarmed
}

StepResult Simulation::stepPhysics(Ship& ship, double dt, double throttle, double brake, double steer) const {
  Physics& p = ship.physics;
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

  Sample c = sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
  Vec3 surfaceNormal = c.normal;
  Vec3 surfaceRenderPos = p.groundPos;

  // No mesh regions in scope -> surfaceOwnerAt is always null.
  const bool meshRegion = false;

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
    // (mesh-rail loop over empty meshRegions omitted)

    p.verticalVel -= p.gravity * dt;
    p.groundPos.set(px, p.groundPos.y + p.verticalVel * dt, pz);

    // (mesh landing omitted) -> corridor landing only.
    c = sampleTrack(px, p.groundPos.y, pz);
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
  } else if (hasTranslation) {  // (mesh-region translation branch omitted)
    Vec3 newPos = p.groundPos.clone().addScaledVector(vel, dt);

    const Sample current = c;
    Projection projection = projectToSurface(current, newPos.x, newPos.y, newPos.z);
    const bool forceCurrentWall = !current.offEnd && (projection.s > projection.hiS || projection.s < projection.loS);

    if (!forceCurrentWall) {
      c = sampleTrack(newPos.x, newPos.y, newPos.z);
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

  if (!p.airborne && !hasTranslation) {  // (parked-on-mesh branch omitted)
    c = sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
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

  tickBoost(ship, dt);
  if (!p.airborne) detectZoneTriggers(ship, c, meshRegion);

  detectTriggers(ship, ship.prevTriggerPos, p.groundPos);
  ship.prevTriggerPos.copy(p.groundPos);

  if (p.airborne && p.groundPos.y < track_.trackFloorY) {
    respawn(ship);
    return {surfaceNormal, surfaceRenderPos, true};
  }
  return {surfaceNormal, surfaceRenderPos, false};
}

}  // namespace tox
