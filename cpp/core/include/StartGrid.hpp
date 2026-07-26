// StartGrid.hpp — pure racing-grid layout (mirror of web/js/ship-grid.js) plus the
// authored-start settling pass from web/js/track-game.js's startingGridPoses
// (lines 938-989). Longitudinal offsets are positive distances behind the
// authored start in the driven direction; lateral offsets are negative on the
// driver's left and positive on the right.
#pragma once
#include <limits>
#include <vector>
#include "Simulation.hpp"
#include "Track.hpp"

namespace tox {
namespace StartGrid {

constexpr int DEFAULT_SHIP_COUNT = 8;
constexpr int GRID_COLUMNS = 2;
constexpr double GRID_LATERAL_SPACING = 5.0;
constexpr double GRID_ROW_SPACING = 8.0;
constexpr double GRID_STAGGER = 3.0;
// Half the ship's collision footprint used to keep the grid off the walls
// (mirror of track-game.js's SHIP_HALF_WIDTH, line 905).
constexpr double SHIP_HALF_WIDTH = 1.2;

struct GridSlotOptions {
  double lateralSpacing{GRID_LATERAL_SPACING};
  double lateralLimit{std::numeric_limits<double>::infinity()};
  double rowSpacing{GRID_ROW_SPACING};
  double stagger{GRID_STAGGER};
};

struct GridSlot {
  int row{0}, column{0};
  double lateral{0.0}, behind{0.0};
};

GridSlot gridSlot(int index, const GridSlotOptions& options = {});
std::vector<GridSlot> gridSlots(int count = DEFAULT_SHIP_COUNT, const GridSlotOptions& options = {});

// Interpolates a corridor-style Sample at `distanceBehind` metres behind
// `startIndex` along `path`'s centerline, walking backward (reverse=false) or
// forward (reverse=true) through the driven direction (mirror of
// track-game.js's interpolatedGridFrame).
Sample interpolatedGridFrame(const Path& path, int startIndex, double distanceBehind, bool reverse);

// Resolves the authored start (track.definition.start) and produces one
// settled pose per grid slot: the alternating two-column staggered grid,
// compressed laterally on narrow roads, each analytically-placed slot then
// settled onto the same sampled curved surface physics uses (mirror of
// track-game.js's startingGridPoses).
std::vector<Pose> startingGridPoses(const Simulation& sim, const Track& track, int count = DEFAULT_SHIP_COUNT);

}  // namespace StartGrid
}  // namespace tox
