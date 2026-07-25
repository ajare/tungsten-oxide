// RandomTrack.hpp — deterministic random-track generation (EDITOR_CPP_PORT_PLAN.md M7a), ported
// from js/editor.js's generateRandomTrack.
//
// Scope: this ports the closed-loop generator (N-turn loop, calibrated driven length, rolling
// hills, curvature-based banking, boost zones) -- the exact code path editor.js itself takes when
// no mesh sections are rolled (its `if (!cuts.length)` branch). editor.js's mesh-SECTION path
// (splitting the loop with jump platforms/ramps and an iterative spline-endpoint-blend solve to
// land each drop exactly, ~180 more lines) is NOT ported here; every generated track is the
// single-path loop variant. A future milestone can add mesh sections once there's more mileage on
// this simpler generator. See EDITOR_CPP_PORT_PLAN.md for the scope note.
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
// track (mulberry32, matching editor.js's PRNG bit-for-bit in spirit, not necessarily in the exact
// stream -- see RandomTrack.cpp's Mulberry32).
TrackDefinition generateRandomTrack(int complexity, std::uint32_t seed, const RandomTrackRanges& ranges = {});

}  // namespace editor
