// GameSession.hpp — the renderer-neutral session/roster layer. Owns a compiled Track and
// its Simulation, a fixed ship roster, a deterministic session clock, and the
// gameplay events fired since the last step.
//
// No renderer, DOM, image, audio, or platform-input dependency: platform code
// translates its own input into ControlIntent outside this class.
#pragma once
#include <memory>
#include <vector>
#include "ShipFactory.hpp"
#include "Simulation.hpp"
#include "Track.hpp"

namespace tox {

// One frame's worth of a single ship's input, sampled by the host once per
// rendered frame per ship.
struct ControlIntent {
  double throttle{0.0};
  double brake{0.0};
  double steer{0.0};
  bool respawn{false};
};

enum class GameEventType { TriggerFired, CheckpointAccepted, LapCompleted, Respawned, RailHit };

struct GameEvent {
  GameEventType type{GameEventType::TriggerFired};
  int shipIndex{0};
  std::string triggerId;  // empty where not applicable (Respawned, RailHit)
  std::string direction;  // "forward"/"backward", TriggerFired/CheckpointAccepted/LapCompleted only
  // Respawned only: true when fallen off track mid-substep, false when the
  // caller's ControlIntent::respawn requested it explicitly.
  bool automatic{false};
};

class GameSession {
public:
  // Largest frame delta accepted before clamping, so a debugger pause or a
  // dropped frame cannot inject a huge, tunneling-prone physics step.
  static constexpr double MAX_FRAME_DELTA = 0.05;

  explicit GameSession(std::shared_ptr<Track> track, int shipCount = StartGrid::DEFAULT_SHIP_COUNT);

  // Advances every ship by one rendered frame: clamps `dt`, divides moving
  // ships into equal MAX_PHYSICS_STEP-sized sub-steps, processes explicit and
  // automatic respawns, and collects this frame's events. Replaces the
  // previous frame's event list.
  void step(const std::vector<ControlIntent>& intents, double dt);

  const std::vector<Ship>& ships() const { return ships_; }
  std::vector<Ship>& ships() { return ships_; }
  const std::vector<GameEvent>& events() const { return events_; }
  double sessionTime() const { return sessionTime_; }
  // Test/parity-only: overwrites the deterministic session clock so a harness
  // can replay a recorded frame from its exact "before" session time rather
  // than the value this session accumulated on its own. Production callers
  // never need this — sessionTime_ is otherwise only ever advanced by step().
  void setSessionTime(double t) { sessionTime_ = t; }
  const Simulation& simulation() const { return simulation_; }
  const Track& track() const { return *track_; }

private:
  std::shared_ptr<Track> track_;
  Simulation simulation_;
  std::vector<Ship> ships_;
  std::vector<GameEvent> events_;
  double sessionTime_{0.0};
};

}  // namespace tox
