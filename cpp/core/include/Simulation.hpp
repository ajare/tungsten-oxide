// Simulation.hpp — declarations for the physics step, transliterated line-for-line
// from js/track-physics.js (Simulation.stepPhysics + its helpers). Bodies live in
// src/Simulation.cpp.
//
// Includes spline-corridor and mesh-region ownership, collision, transitions,
// airborne landing, zones/triggers, and respawn recovery. The old baked-world
// parity corpus still exercises the exact mesh-free branches.
#pragma once
#include "Vec3.hpp"
#include "TrackCore.hpp"
#include "Track.hpp"
#include "Ship.hpp"

namespace tox {

inline const Vec3 UP{0, 1, 0};

// sampleTrack's result (mirror of the JS _sample).
struct Sample {
  Vec3 pos, tangent, edgeRight, normal;
  double halfW{0.0}, sLeft{0.0}, sRight{0.0};
  double crossSectionCurvature{0.0}, crossSectionTightness{1.0};
  bool offEnd{false};
  int pathIndex{0}, a{0}, b{1};
  double segT{0.0};
};

struct Projection {
  Vec3 er;
  double s{0.0}, loS{0.0}, hiS{0.0};
};
struct SurfaceFrame {
  Vec3 pos, normal;
};
struct StepResult {
  Vec3 surfaceNormal, surfaceRenderPos;
  bool respawned{false};
};

// --- pure helpers (mirror of the track-physics.js exports) -----------------
double effectiveMaxSpeed(const Physics& p);
void triggerBoost(Ship& ship, const Zone& zone);
void tickBoost(Ship& ship, double dt);
Projection projectToSurface(const Sample& s, double px, double py, double pz);
bool corridorContains(const Sample& s, double x, double y, double z, const Projection& proj);
SurfaceFrame curvedSurfaceFrame(const Sample& s, double sOff);
Vec3& tangentize(Vec3& v, const Vec3& n, const Vec3& fallback);
double signedAngleAbout(const Vec3& a, const Vec3& b, const Vec3& axis);
void beginAirborne(Ship& ship, const Vec3& vel3D);
void landOnSurface(Ship& ship, const Vec3& normal);
double weightRestitution(const Physics& p);
double weightSpeedRetain(const Physics& p);
void addImpactJolt(Physics& p, double normalImpactSpeed);

// ---------------------------------------------------------------------------
class Simulation {
public:
  explicit Simulation(const Track& track);

  Sample sampleTrack(double x, double y, double z) const;

  // Recovers the ship's evaluator parameter g on the path the sample landed on.
  double shipParamG(const Sample& sample) const;

  // Surface ownership and zone/trigger detection (mirror of track-physics.js).
  const MeshRegion* meshRegionAt(double x, double z, double shipY) const;
  const MeshRegion* surfaceOwnerAt(double x, double z, double shipY, const Sample& corridorSample) const;
  void detectZoneTriggers(Ship& ship, const Sample& sample, const MeshRegion* meshRegion) const;
  void detectTriggers(Ship& ship, const Vec3& p0, const Vec3& p1) const;
  void fireTrigger(Ship& ship, const Trigger& rec, const std::string& dir) const;

  // Ship placement / respawn recovery.
  void clearBoost(Ship& ship) const;
  void resetTriggers(Ship& ship, const std::string& disarmedId) const;
  void placeShipAtPose(Ship& ship, const Pose& pose, const std::string& disarmedId) const;
  void respawn(Ship& ship) const;

  // Advance ONE integration sub-step.
  StepResult stepPhysics(Ship& ship, double dt, double throttle, double brake, double steer) const;

private:
  const Track& track_;
};

}  // namespace tox
