// GameSession.cpp — bodies for include/GameSession.hpp.
#include "GameSession.hpp"

#include <algorithm>
#include <cmath>

namespace tox {

namespace {
GameEventType toEventType(TriggerNotice notice) {
  switch (notice) {
    case TriggerNotice::CheckpointAccepted:
      return GameEventType::CheckpointAccepted;
    case TriggerNotice::LapCompleted:
      return GameEventType::LapCompleted;
    case TriggerNotice::Fired:
    default:
      return GameEventType::TriggerFired;
  }
}
}  // namespace

GameSession::GameSession(std::shared_ptr<Track> track, int shipCount)
    : track_(std::move(track)), simulation_(*track_) {
  simulation_.now = [this] { return sessionTime_; };
  simulation_.onTriggerFired = [this](Ship& ship, const Trigger& rec, const std::string& dir, TriggerNotice notice) {
    // ships_ is a contiguous vector<Ship>; `ship` is always a reference to one
    // of its elements (Simulation::stepPhysics is only ever called on those
    // below), so pointer arithmetic recovers the ship index without a
    // separate id lookup.
    const int shipIndex = static_cast<int>(&ship - ships_.data());
    GameEvent event;
    event.type = toEventType(notice);
    event.shipIndex = shipIndex;
    event.triggerId = rec.id;
    event.direction = dir;
    events_.push_back(event);
  };
  ships_ = ShipFactory::buildRoster(simulation_, *track_, shipCount, sessionTime_);
}

void GameSession::step(const std::vector<ControlIntent>& intents, double dt) {
  events_.clear();
  const double clamped = std::min(dt, MAX_FRAME_DELTA);
  sessionTime_ += clamped;

  static const ControlIntent kIdle{};
  for (size_t i = 0; i < ships_.size(); i++) {
    Ship& ship = ships_[i];
    const ControlIntent& intent = i < intents.size() ? intents[i] : kIdle;

    if (intent.respawn) {
      simulation_.respawn(ship);
      GameEvent event;
      event.type = GameEventType::Respawned;
      event.shipIndex = static_cast<int>(i);
      event.automatic = false;
      events_.push_back(event);
      continue;
    }

    const int subSteps = std::max(1, static_cast<int>(std::ceil(clamped / Consts::MAX_PHYSICS_STEP)));
    const double sdt = clamped / subSteps;
    for (int s = 0; s < subSteps; s++) {
      const StepResult r = simulation_.stepPhysics(ship, sdt, intent.throttle, intent.brake, intent.steer);
      if (r.railHit) {
        GameEvent event;
        event.type = GameEventType::RailHit;
        event.shipIndex = static_cast<int>(i);
        events_.push_back(event);
      }
      if (r.respawned) {
        GameEvent event;
        event.type = GameEventType::Respawned;
        event.shipIndex = static_cast<int>(i);
        event.automatic = true;
        events_.push_back(event);
        break;  // ship already reset; skip the rest of this frame's sub-steps
      }
    }
  }
}

}  // namespace tox
