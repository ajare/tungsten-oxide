// ShipFactory.cpp — bodies for include/ShipFactory.hpp.
#include "ShipFactory.hpp"

#include <cmath>

namespace tox {
namespace ShipFactory {

namespace {
// Mirror of TrackBake.cpp's local PI/DEG2RAD — MSVC's <cmath> does not define
// M_PI without _USE_MATH_DEFINES, so the codebase spells the constant out.
constexpr double PI = 3.14159265358979323846;
}  // namespace

void applyHandling(const TrackDefinition& definition, Physics& physics) {
  const auto& h = definition.handling;
  physics.maxSpeed = h.maxSpeed;
  physics.accel = h.accel;
  physics.turnRate = h.turnSpeed * PI / 180.0;
  physics.weight = h.weight;
}

Race createRaceState(const Track& track, double now) {
  Race race;
  for (const Trigger& tr : track.triggers) {
    if (tr.type != "checkpoint") continue;
    if (tr.role == "finish") {
      if (race.finishId.empty()) race.finishId = tr.id;
    } else {
      race.intermediateIds.push_back(tr.id);
    }
  }
  race.totalStartedAt = now;
  race.lapStartedAt = now;
  return race;
}

Ship makeShip(const Simulation& sim, const Track& track, const Pose& startPose, double now) {
  Ship ship;
  applyHandling(track.definition, ship.physics);
  ship.race = createRaceState(track, now);
  ship.startPose = startPose;
  sim.placeShipAtPose(ship, startPose, "");
  return ship;
}

std::vector<Ship> buildRoster(const Simulation& sim, const Track& track, int count, double now) {
  const std::vector<Pose> poses = StartGrid::startingGridPoses(sim, track, count);
  std::vector<Ship> ships;
  ships.reserve(poses.size());
  for (const Pose& pose : poses) ships.push_back(makeShip(sim, track, pose, now));
  return ships;
}

}  // namespace ShipFactory
}  // namespace tox
