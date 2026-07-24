// TrackCore.hpp — the stateless shared math the physics step calls at runtime,
// transliterated 1:1 from track-core.js / track-physics.js. Because the golden
// trace serializes the already-BAKED corridor, the C++ engine never bakes a
// track, so only the cross-section profile functions (used by curvedSurfaceFrame)
// are needed here — plus the centralized constants (CPP_PORT_PLAN.md §8).
//
// The constants stay in the header (constexpr); the function bodies live in
// src/TrackCore.cpp.
#pragma once

namespace tox {
namespace TrackCore {

// --- geometry constants owned by track-core.js ---
constexpr double COLLISION_WALL_MARGIN = 1.8;
constexpr double DEFAULT_CROSS_SECTION_TIGHTNESS = 1.0;
constexpr double DEFAULT_BOOST_FACTOR = 1.5;
constexpr double DEFAULT_BOOST_DURATION = 2.0;

// THREE.MathUtils.clamp — same formula r128 uses.
double clamp(double v, double lo, double hi);

// clampSignedUnit / clampTightness (track-core.js). Inputs here are already
// finite doubles from the baked trace, so the "non-finite -> fallback" branch is
// preserved only for faithfulness.
double clampSignedUnit(double n);
double clampTightness(double n);

// Road-surface rise above the flat chord, as a function of v (0 left .. 1 right).
double crossSectionHeight(double curvature, double tightness, double v, double chordWidth);
// d(height)/dv, used to build the surface normal across the road.
double crossSectionHeightDerivative(double curvature, double tightness, double v, double chordWidth);

// Is the ship's evaluator parameter gShip within a path zone's [gLo, gHi] window?
// For a closed path the window may straddle the wrap, so gShip is shifted by whole
// cycles into the window's neighbourhood before the range test (track-core.js).
bool zoneAlongContains(double gShip, double gLo, double gHi, double gMax, bool closed);

}  // namespace TrackCore

// --- centralized physics-loop constants (mirror of track-physics.js) ---
// Named Consts (not Physics) to avoid colliding with the Ship's `struct Physics`.
namespace Consts {
constexpr double ZONE_RELEASE = 1.0;
constexpr double CHECKPOINT_FLASH_MS = 500.0;
constexpr double TRIGGER_REARM_MARGIN = 3.0;
constexpr double SURFACE_SNAP_UP = 3.0;
constexpr double RESPAWN_FALL_DEPTH = 100.0;
constexpr double CORRIDOR_ALONG_TOL = 8.0;
constexpr double SEGMENT_ALONG_TOL = 0.5;
constexpr double MAX_PHYSICS_STEP = 1.0 / 120.0;
constexpr double HANDLING_BASE_WEIGHT = 1000.0;
}  // namespace Consts

}  // namespace tox
