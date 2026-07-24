// Ship.hpp — the stateful physics struct (Ship) with the full state a golden
// trace serializes: every createPhysicsState() field plus the per-ship detection
// bookkeeping. Field names match js/track-physics.js and test/parity/state.js so
// the transliteration reads 1:1 and the harness can compare field-for-field.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Vec3.hpp"

namespace tox {

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

  Vec3 up{0, 1, 0};
  Vec3 forward{0, 0, 1};
  Vec3 right{1, 0, 0};
  Vec3 groundPos;
  Vec3 visualGroundPos;
  Vec3 visualUp{0, 1, 0};
  Vec3 moveDir{0, 0, 1};
};

struct TriggerState { bool armed{true}; double flash{0.0}; };

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
};

// The respawn fallback pose (mirror of track-game.js ship.startPose), used when
// no checkpoint has been reached yet.
struct Pose { Vec3 pos, up, forward; };

struct Ship {
  Physics physics;
  Vec3 prevTriggerPos;
  std::map<std::string, bool> zoneInside;
  std::map<std::string, TriggerState> triggerStates;
  Checkpoint lastCheckpoint;
  Race race;
  Pose startPose;
};

}  // namespace tox
