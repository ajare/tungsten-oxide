// ShipFactory.hpp — the canonical native ship/roster initialization path: applyHandling,
// createRaceState, createShip/buildRoster (physics-relevant subset only; no scene-graph node,
// color, or controller assignment, which stay host-side).
#pragma once
#include <vector>
#include "Ship.hpp"
#include "Simulation.hpp"
#include "StartGrid.hpp"
#include "Track.hpp"
#include "TrackDefinition.hpp"

namespace tox {
namespace ShipFactory {

// Copies track.handling into a Physics, converting turnSpeed (authored in
// degrees/second) to turnRate (radians/second).
void applyHandling(const TrackDefinition& definition, Physics& physics);

// Derives intermediateIds/finishId from the track's compiled checkpoint
// triggers and seeds the deterministic session-time clock fields, with `now`
// standing in for the caller-supplied session time (never a platform clock).
Race createRaceState(const Track& track, double now = 0.0);

// Builds one fully-initialized ship at `startPose`: applies handling,
// initializes race/detection state, and places it (which also clears boost
// and arms triggers).
Ship makeShip(const Simulation& sim, const Track& track, const Pose& startPose, double now = 0.0);

// Builds a full roster of `count` ships on the authored starting grid
// (minus rebuilding any presentation-only checkpoint lights, which stays host-side).
std::vector<Ship> buildRoster(const Simulation& sim, const Track& track, int count = StartGrid::DEFAULT_SHIP_COUNT,
                               double now = 0.0);

}  // namespace ShipFactory
}  // namespace tox
