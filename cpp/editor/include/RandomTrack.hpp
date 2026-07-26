// RandomTrack.hpp — deterministic random-track generation (EDITOR_CPP_PORT_PLAN.md M7a/M7c),
// ported from web/js/editor.js's generateRandomTrack.
//
// M7a ported the closed-loop generator (N-turn loop, calibrated driven length, rolling hills,
// curvature-based banking, boost zones) -- the code path editor.js itself takes when no mesh
// sections are rolled (its `if (!cuts.length)` branch). M7c ports the remaining mesh-section
// branch: splitting the loop into open ordinary paths joined by jump platforms/ramps, with an
// iterative endpoint-blend solve to land each drop exactly. Where editor.js solves that blend with
// its own hand-rolled spline evaluator (TrackCore.makeEvaluator), this instead bakes each
// candidate path through core's real tox::Track::fromJson and reads the baked centerline's first/
// last frame -- exactly the evaluator's endpoint (buildCenterline samples an open path's parameter
// range [0, CP_N-1] inclusive of both ends), so this is the same value core would compute, not an
// approximation, while keeping the "reuse core as a black box" rule the rest of this port follows.
// See RandomTrack.cpp's bakeOpenPathEndpoint.
//
// Known gap: editor.js's generateRandomTrack finishes by calling TrackCore.normalizeTriggers,
// which injects an explicit "finish" checkpoint into the authored track if none of the generated
// triggers claim that role (via automaticFinishDescriptor, itself built on triggerPathFrame/
// frameFromSample gate math). That auto-finish behavior is a general editor-wide feature -- it
// would apply to any hand-authored track missing a finish trigger, not just generated ones -- and
// isn't ported anywhere in this editor yet (buildStarterTrack in main.cpp seeds one explicitly
// instead). A track generated here with mesh sections therefore carries only "intermediate"
// checkpoints in its own authored JSON. It is still fully correct once loaded through the real
// game runtime: cpp/core's own tox::Track::fromJson (used for every preview bake in this editor)
// already ports the identical auto-finish injection at load time, so nothing is missing from
// gameplay -- only from what this editor session's own in-memory/exported JSON shows before a
// save+reload round-trip (which itself has no UI yet).
#pragma once

#include <cstdint>

#include "EditorTrackDefinition.hpp"

namespace editor {

struct RandomTrackRanges {
  double lengthMin{8000.0}, lengthMax{9000.0};
  int turnsMin{6}, turnsMax{22};
  double maxBanking{25.0}, maxHill{300.0};
  double widthMin{28.0}, widthMax{52.0}, maxCurvature{0.5};
  // M7c: mesh-section generation controls, mirroring RANDOM_RANGE_DEFAULTS.
  double meshChanceMin{15.0}, meshChanceMax{45.0};
  double sequenceChance{20.0};
  int maxMeshSections{2};
  double meshLengthMin{120.0}, meshLengthMax{300.0};
  double endDropMin{15.0}, endDropMax{40.0};
  int boostMin{2}, boostMax{5};
};

// `complexity` is clamped to [1, 10]. Same seed + complexity + ranges always reproduces the same
// track (mulberry32, matching editor.js's PRNG bit-for-bit). When no mesh sections are rolled
// (same probabilistic gate as editor.js, `meshChanceMin`/`meshChanceMax`/`maxMeshSections`), the
// result is the single-loop variant from M7a; otherwise it's the full mesh-section track with
// jump platforms, ramps, and intermediate checkpoints, mirroring editor.js's `cuts.length` branch.
TrackDefinition generateRandomTrack(int complexity, std::uint32_t seed, const RandomTrackRanges& ranges = {});

}  // namespace editor
