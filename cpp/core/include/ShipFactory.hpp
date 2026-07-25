// ShipFactory.hpp — the canonical native ship/roster initialization path
// (NATIVE_GAME_RUNTIME_PLAN.md §2.1/§2.2/§2.3), a transliteration of
// js/track-physics.js's applyHandling/createRaceState and js/track-game.js's
// createShip/buildRoster (physics-relevant subset only; no THREE.Group, color,
// or controller assignment, which stay host-side).
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
// degrees/second) to turnRate (radians/second). Mirror of
// js/track-physics.js's applyHandling.
void applyHandling(const TrackDefinition& definition, Physics& physics);

// Derives intermediateIds/finishId from the track's compiled checkpoint
// triggers and seeds the deterministic session-time clock fields. Mirror of
// js/track-physics.js's createRaceState, with `now` standing in for the
// caller-supplied session time (never a platform clock).
Race createRaceState(const Track& track, double now = 0.0);

// Builds one fully-initialized ship at `startPose`: applies handling,
// initializes race/detection state, and places it (which also clears boost
// and arms triggers). Mirror of js/track-game.js's createShip + the
// per-ship body of buildRoster.
Ship makeShip(const Simulation& sim, const Track& track, const Pose& startPose, double now = 0.0);

// Builds a full roster of `count` ships on the authored starting grid.
// Mirror of js/track-game.js's buildRoster (minus rebuildCheckpointLights,
// which is presentation-only).
std::vector<Ship> buildRoster(const Simulation& sim, const Track& track, int count = StartGrid::DEFAULT_SHIP_COUNT,
                               double now = 0.0);

}  // namespace ShipFactory
}  // namespace tox
