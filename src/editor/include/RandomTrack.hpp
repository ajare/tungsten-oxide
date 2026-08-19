// RandomTrack.hpp — deterministic random-track generation.
//
// The closed-loop generator (N-turn loop, calibrated driven length, rolling hills,
// curvature-based banking, boost zones) is the only code path now -- the mesh-section path
// (splitting the loop into open ordinary paths joined by jump platforms/ramps generated from
// MeshAsset/MeshPlacement) was removed along with MeshRegion (DRIVABLE_MESH_OBJECTS_PLAN.md
// Milestone 2), with no interim replacement; Milestone 5's drivable mesh object placements are
// expected to bring back something in this spirit.
#pragma once

#include <cstdint>

#include "EditorTrackDefinition.hpp"

namespace editor {

struct RandomTrackRanges {
  double lengthMin{8000.0}, lengthMax{9000.0};
  int turnsMin{6}, turnsMax{22};
  double maxBanking{25.0}, maxHill{300.0};
  double widthMin{28.0}, widthMax{52.0}, maxCurvature{0.5};
  int boostMin{2}, boostMax{5};
};

// `complexity` is clamped to [1, 10]. Same seed + complexity + ranges always reproduces the same
// track (mulberry32 PRNG).
TrackDefinition generateRandomTrack(int complexity, std::uint32_t seed, const RandomTrackRanges& ranges = {});

}  // namespace editor
