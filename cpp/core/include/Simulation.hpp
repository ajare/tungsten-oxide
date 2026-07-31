// Simulation.hpp — immutable track queries, placement/trigger helpers, and the
// compatibility stepPhysics facade. Per-ship integration lives in Ship.cpp.
//
// Includes spline-corridor and mesh-region ownership, collision, transitions,
// airborne landing, zones/triggers, and respawn recovery. The old baked-world
// parity corpus still exercises the exact mesh-free branches.
#pragma once
#include <functional>
#include <string>
#include "Vec3.hpp"
#include "TrackCore.hpp"
#include "Track.hpp"
#include "Ship.hpp"

namespace tox {

inline const Vec3 UP{0, 1, 0};

// sampleTrack's result.
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
  bool respawned{false}, railHit{false};
};

// Distinguishes what a trigger crossing accomplished (NATIVE_GAME_RUNTIME_PLAN.md
// §2.6): every crossing fires `Fired` (including non-checkpoint "dummy"
// triggers); a checkpoint crossing that advances the race additionally fires
// `CheckpointAccepted` or, on completing the final intermediate + finish,
// `LapCompleted`. Notices are emitted from inside fireTrigger itself, so a
// frame split into several physics sub-steps still emits each occurrence
// exactly once.
enum class TriggerNotice { Fired,
                           CheckpointAccepted,
                           LapCompleted };

// --- pure helpers ------------------------------------------------------------
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

  const Track& track() const { return track_; }

  // When true, Ship::step derives ground contact, wall collision and airborne/landing purely from
  // the baked collision BVH (Track::collisionSurface) instead of the analytic corridor/MeshRegion
  // math. On by default -- mesh physics is the shipped default mode; analytic mode remains
  // available as a live-toggleable fallback and is what the golden trace regression suite covers
  // (parity/raw_parity pin Simulation/GameSession to analytic mode explicitly rather than relying
  // on this default, so the suite's coverage is unaffected by this default's value). A GameSession
  // forwards this from the in-game debug UI (see StatePlayTungstenMonoxide's Debug tab).
  bool meshPhysicsEnabled() const { return meshPhysicsEnabled_; }
  void setMeshPhysicsEnabled(bool enabled) { meshPhysicsEnabled_ = enabled; }

  // Game-only observation hooks (injected trigger-fired callback / session-time source). Both
  // default to no-ops so existing headless callers are unaffected; a GameSession sets these to
  // surface gameplay events and a deterministic session clock. `now()` feeds the
  // Race::lapStartedAt/flashUntil timestamps on lap completion only — never
  // read for anything else in the deterministic step itself.
  std::function<void(Ship&, const Trigger&, const std::string& dir, TriggerNotice)> onTriggerFired;
  std::function<double()> now = [] { return 0.0; };

  Sample sampleTrack(double x, double y, double z) const;

  // Recovers the ship's evaluator parameter g on the path the sample landed on.
  double shipParamG(const Sample& sample) const;

  // Surface ownership and zone/trigger detection.
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

  // Gives a ship an immediate world-Y launch. Preserves its horizontal velocity,
  // guarantees at least Consts::MIN_LAUNCH_UPWARD_SPEED, and does not stack
  // upward speed on repeated calls.
  void launchShip(Ship& ship, double upwardSpeed) const;

  // Advance ONE integration sub-step.
  StepResult stepPhysics(Ship& ship, double dt, double throttle, double brake, double steer) const;

private:
  const Track& track_;
  bool meshPhysicsEnabled_{true};
};

}  // namespace tox
