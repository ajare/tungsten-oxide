// Ship.hpp — behavior-owning ship plus the complete state serialized by golden
// traces. Ship::step performs integration/collision; Simulation supplies shared
// immutable track queries and keeps a compatibility facade for existing callers.
#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "Vec3.hpp"

namespace tox {

class Simulation;
struct StepResult;
struct Obb;

struct Physics {
  double heading{0.0};
  double speed{0.0};
  double maxSpeed{140.0};
  double maxReverse{-33.0};
  double accel{71.0};
  double brakeDecel{115.0};
  double friction{55.0};
  double turnRate{2.4};
  double grip{3.2};
  double wallRestitution{0.75};
  double weight{1000.0};
  double bobTime{0.0};
  double visualBank{0.0};
  double visualPitch{0.0};
  bool airborne{false};
  double verticalVel{0.0};
  double gravity{60.0};
  double landingBounce{0.0};
  double landingBounceVel{0.0};
  bool boostActive{false};
  bool boostReleasing{false};
  double boostHold{0.0};
  double boostReleaseT{0.0};
  double boostCap{0.0};
  double boostEffCap{0.0};

  // Collision hull half-extents, along right/up/forward respectively (docs/
  // OBB_SHIP_COLLISION_PLAN.md Milestone 3). Used only by mesh-mode OBB wall collision; ground
  // contact remains a point probe at groundPos, and analytic corridor mode ignores these entirely.
  //
  // The defaults are the rendered ship's actual dimensions, not a guess: box.mppmodel is a unit
  // cube and StatePlayTungstenMonoxide::applyShipTransform scales it by (2.4, 0.8, 4.0) in
  // (right, up, forward) -- so half of each. The width half independently agrees with
  // StartGrid::SHIP_HALF_WIDTH = 1.2, which the starting grid has always used as "half the ship's
  // collision footprint". Worth a sanity check with whoever owns ship art if the model changes;
  // they are per-ship fields precisely so a future ship class can differ.
  //
  // Additive to the golden-trace serialized state in the same way Race's session-time fields are:
  // the trace readers name every physics field they load (parity_main.cpp's loadShip), so a trace
  // recorded before these existed simply leaves them at these defaults.
  double hullHalfLength{2.0};
  double hullHalfWidth{1.2};
  double hullHalfHeight{0.4};

  Vec3 up{0, 1, 0};
  Vec3 forward{0, 0, 1};
  Vec3 right{1, 0, 0};
  Vec3 groundPos;
  Vec3 visualGroundPos;
  Vec3 visualUp{0, 1, 0};
  Vec3 moveDir{0, 0, 1};
};

// The ship's collision hull as an oriented box, for a ship standing at `groundPos` on a surface
// whose normal is `up` (docs/OBB_SHIP_COLLISION_PLAN.md Milestone 3.2).
//
// The basis is rebuilt here from `up` and physics.forward rather than read off Physics::right/up:
// those two are written once at spawn/respawn and then stay frozen for the whole run (see
// renderNormal's comment below), so on any banked or rolled section they no longer describe how
// the ship is actually sitting. Callers in mesh mode pass the live surface normal they are already
// probing with (ship.renderNormal, or the contact normal they just resolved).
//
// `groundPos` is a contact point ON the surface, not the hull's centre, so the box is lifted half
// its height along `up` -- i.e. the hull rests on the ground rather than being buried to its
// waist. (The renderer separately floats the ship's *model* a unit above groundPos as hover; that
// is a visual flourish, and hanging collision off it would let the hull sail over anything shorter
// than a metre.)
Obb hullObb(const Physics& physics, const Vec3& groundPos, const Vec3& up);

struct TriggerState {
  bool armed{true};
  double flash{0.0};
};

struct Checkpoint {
  bool valid{false};
  std::string triggerId;
  Vec3 pos, forward, up{0, 1, 0};
};

struct Race {
  int laps{0};
  std::set<std::string> hit;
  // Constant across a run, carried in the trace so fireTrigger's lap gate is
  // reconstructable from a step loaded in isolation (mirror of createRaceState).
  std::vector<std::string> intermediateIds;
  std::string finishId;

  // Deterministic session-time clock fields (mirror of createRaceState's
  // totalStartedAt/lapStartedAt/flashUntil). Seeded from the caller-supplied
  // session time, never a platform clock; the baked-world/raw-track parity
  // traces never serialize these, so they are additive and gate-inert there.
  double totalStartedAt{0.0};
  double lapStartedAt{0.0};
  double flashUntil{0.0};
};

// The respawn fallback pose, used when no checkpoint has been reached yet.
struct Pose {
  Vec3 pos, up, forward;
};

struct Ship {
  // Canonical per-ship behavior entry points. Simulation remains a shared
  // track-query/compatibility facade for existing parity consumers.
  //
  // meshModeOverride, when set, picks analytic (false) or mesh (true) physics for this call
  // regardless of Simulation::meshPhysicsEnabled() -- the mode every other caller implicitly gets.
  // Exists for GameSession::stepGhost's debug "what would the other method have done" projection;
  // ordinary gameplay callers should leave it unset.
  StepResult step(const Simulation& simulation, double dt, double throttle, double brake, double steer,
                  std::optional<bool> meshModeOverride = std::nullopt);
  void respawn(const Simulation& simulation);
  void placeAt(const Simulation& simulation, const Pose& pose, const std::string& disarmedId = {});

  Physics physics;
  Vec3 prevTriggerPos;
  std::map<std::string, bool> zoneInside;
  std::map<std::string, TriggerState> triggerStates;
  Checkpoint lastCheckpoint;
  Race race;
  Pose startPose;

  // Render-only: the last surface normal Ship::step actually resolved this frame (StepResult's own
  // one, freshly recomputed every step from wherever the ship currently sits -- corridor, mesh
  // region, or exported collision surface). Deliberately NOT `physics.up`: that field only ever
  // gets written at spawn/respawn (Simulation::placeShipAtPose) and stays frozen for the rest of a
  // run -- a deliberate, golden-trace-pinned characteristic of the physics model (verified: the raw
  // session traces show `up` bit-identical across every step of a run), not something safe to
  // change there. A renderer chasing that frozen value on a rolled/banked section fights a stale
  // target while the ship's position correctly follows the bank, which reads as jitter. This field
  // exists purely so a renderer has a live, correct-every-frame value to chase instead.
  Vec3 renderNormal{0, 1, 0};
};

}  // namespace tox
